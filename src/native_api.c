#include "native_api.h"

#include <windows.h>

#include <stdint.h>
#include <string.h>

enum {
    CDF_ENTRY_ITEM_OFFSET = 0x08,
    CDF_ENTRY_FLAGS_OFFSET = 0x28,
    CDF_ENTRY_ROOM_OFFSET = 0x30,
    CDF_COLLECTION_COUNT_OFFSET = 0x0C,
    CDF_COLLECTION_DATA_OFFSET = 0x10,
    CDF_VALUE_PATCH_SITE_COUNT = 6
};

typedef struct CdfNarrowString {
    union {
        char inline_data[16];
        char* heap_data;
    } storage;
    uint64_t size;
    uint64_t capacity;
} CdfNarrowString;

typedef void (__fastcall* CdfStorageDeleteFn)(void*, uint64_t);

typedef struct CdfExecutableRange {
    uint8_t* data;
    size_t size;
} CdfExecutableRange;

typedef struct CdfRuntimeState {
    int attempted;
    uint8_t* executable;
    uint32_t image_size;
    uintptr_t scene_ready_hook_rva;
    uintptr_t furniture_mode_enter_hook_rva;
    CdfStorageDeleteFn storage_delete;
    void** storage_global_slot;
    size_t storage_count_offset;
    size_t storage_data_offset;
    size_t furniture_mode_active_offset;
    size_t furniture_rebuild_dirty_offset;
    size_t furniture_ui_context_offset;
    size_t furniture_ui_root_offset;
    size_t furniture_ui_table_offset;
    size_t furniture_row_collection_offset;
    size_t furniture_row_stable_key_offset;
    uint8_t* value_patch_sites[CDF_VALUE_PATCH_SITE_COUNT];
    size_t value_patch_site_count;
    uint8_t* count_patch_site;
    int core_available;
    int ui_refresh_available;
} CdfRuntimeState;

static CdfRuntimeState g_runtime;

static int cdf_readable(const void* pointer, size_t byte_count) {
    uintptr_t current;
    uintptr_t end;
    if (!pointer || byte_count == 0U) {
        return 0;
    }
    current = (uintptr_t)pointer;
    if (byte_count > UINTPTR_MAX - current) {
        return 0;
    }
    end = current + byte_count;
    while (current < end) {
        MEMORY_BASIC_INFORMATION memory;
        uintptr_t region_end;
        if (!VirtualQuery((const void*)current, &memory, sizeof(memory)) ||
            memory.State != MEM_COMMIT ||
            (memory.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0U) {
            return 0;
        }
        region_end = (uintptr_t)memory.BaseAddress + memory.RegionSize;
        if (region_end <= current) {
            return 0;
        }
        current = region_end < end ? region_end : end;
    }
    return 1;
}

static int cdf_copy_string(
    const CdfNarrowString* value,
    char* output,
    size_t capacity) {
    const char* data;
    size_t size;
    if (!value || !output || capacity == 0U ||
        !cdf_readable(value, sizeof(*value))) {
        return 0;
    }
    __try {
        if (value->size >= capacity || value->size > value->capacity) {
            return 0;
        }
        size = (size_t)value->size;
        data = value->capacity > 15U
            ? value->storage.heap_data
            : value->storage.inline_data;
        if (!data || !cdf_readable(data, size == 0U ? 1U : size)) {
            return 0;
        }
        if (size != 0U) {
            memcpy(output, data, size);
        }
        output[size] = '\0';
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

static uint32_t cdf_image_size(HMODULE executable) {
    IMAGE_DOS_HEADER* dos;
    IMAGE_NT_HEADERS64* nt;
    if (!executable) {
        return 0U;
    }
    dos = (IMAGE_DOS_HEADER*)executable;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return 0U;
    }
    nt = (IMAGE_NT_HEADERS64*)((uint8_t*)executable + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        return 0U;
    }
    return nt->OptionalHeader.SizeOfImage;
}

static LONG cdf_capture_exception(
    EXCEPTION_POINTERS* exception,
    uint32_t* seh_code,
    uintptr_t* exception_rva) {
    HMODULE executable = GetModuleHandleW(NULL);
    if (exception && exception->ExceptionRecord && seh_code && exception_rva) {
        const uintptr_t address =
            (uintptr_t)exception->ExceptionRecord->ExceptionAddress;
        const uintptr_t base = (uintptr_t)executable;
        const uint32_t image_size = cdf_image_size(executable);
        *seh_code = exception->ExceptionRecord->ExceptionCode;
        *exception_rva = address >= base && address - base < image_size
            ? address - base
            : 0U;
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

static size_t cdf_executable_ranges(
    uint8_t* executable,
    CdfExecutableRange* ranges,
    size_t capacity) {
    IMAGE_DOS_HEADER* dos;
    IMAGE_NT_HEADERS64* nt;
    IMAGE_SECTION_HEADER* section;
    size_t count = 0U;
    uint16_t index;
    if (!executable || !ranges || capacity == 0U) {
        return 0U;
    }
    dos = (IMAGE_DOS_HEADER*)executable;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return 0U;
    }
    nt = (IMAGE_NT_HEADERS64*)(executable + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        return 0U;
    }
    section = IMAGE_FIRST_SECTION(nt);
    for (index = 0; index < nt->FileHeader.NumberOfSections; ++index) {
        size_t size;
        if ((section[index].Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0U) {
            continue;
        }
        size = section[index].Misc.VirtualSize;
        if (size == 0U) {
            size = section[index].SizeOfRawData;
        }
        if (size == 0U || count == capacity) {
            continue;
        }
        ranges[count].data = executable + section[index].VirtualAddress;
        ranges[count].size = size;
        ++count;
    }
    return count;
}

static int cdf_pattern_matches(
    const uint8_t* address,
    const int16_t* pattern,
    size_t pattern_size) {
    size_t index;
    for (index = 0; index < pattern_size; ++index) {
        if (pattern[index] >= 0 && address[index] != (uint8_t)pattern[index]) {
            return 0;
        }
    }
    return 1;
}

static size_t cdf_find_pattern(
    const int16_t* pattern,
    size_t pattern_size,
    uint8_t** output,
    size_t output_capacity) {
    CdfExecutableRange ranges[16];
    size_t range_count;
    size_t found = 0U;
    size_t range_index;
    range_count = cdf_executable_ranges(
        g_runtime.executable, ranges, sizeof(ranges) / sizeof(ranges[0]));
    for (range_index = 0; range_index < range_count; ++range_index) {
        size_t offset;
        if (ranges[range_index].size < pattern_size) {
            continue;
        }
        for (offset = 0;
             offset <= ranges[range_index].size - pattern_size;
             ++offset) {
            uint8_t* address = ranges[range_index].data + offset;
            if (!cdf_pattern_matches(address, pattern, pattern_size)) {
                continue;
            }
            if (found < output_capacity) {
                output[found] = address;
            }
            ++found;
        }
    }
    return found;
}

static uint8_t* cdf_find_unique(
    const int16_t* pattern,
    size_t pattern_size) {
    uint8_t* result = NULL;
    return cdf_find_pattern(pattern, pattern_size, &result, 1U) == 1U
        ? result
        : NULL;
}

static uint8_t* cdf_rip_target(
    uint8_t* instruction,
    size_t displacement_offset,
    size_t instruction_size) {
    int32_t displacement;
    memcpy(
        &displacement,
        instruction + displacement_offset,
        sizeof(displacement));
    return instruction + instruction_size + displacement;
}

static void cdf_collect_value_patch_sites(void) {
    static const int16_t list_pattern[] = {
        0x80, 0xE3, -1, 0x49, 0x8D, 0x56, 0x08,
        0x48, 0x8D, 0x4D, 0xE0, 0xE8};
    static const int16_t detail_pattern[] = {
        0x80, 0xE3, -1, 0x48, 0x8D, 0x56, 0x08,
        0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8};
    static const int16_t sort_pattern[] = {
        0x80, 0xE3, -1, 0x48, 0x83, 0xC2, 0x08,
        0x48, 0x8D, 0x4D, 0xC0, 0xE8};
    const int16_t* patterns[] = {
        list_pattern, detail_pattern, sort_pattern};
    const size_t sizes[] = {
        sizeof(list_pattern) / sizeof(list_pattern[0]),
        sizeof(detail_pattern) / sizeof(detail_pattern[0]),
        sizeof(sort_pattern) / sizeof(sort_pattern[0])};
    size_t pattern_index;
    g_runtime.value_patch_site_count = 0U;
    for (pattern_index = 0;
         pattern_index < sizeof(patterns) / sizeof(patterns[0]);
         ++pattern_index) {
        uint8_t* matches[CDF_VALUE_PATCH_SITE_COUNT];
        size_t found = cdf_find_pattern(
            patterns[pattern_index],
            sizes[pattern_index],
            matches,
            CDF_VALUE_PATCH_SITE_COUNT);
        size_t match_index;
        if (found > CDF_VALUE_PATCH_SITE_COUNT) {
            g_runtime.value_patch_site_count = 0U;
            return;
        }
        for (match_index = 0; match_index < found; ++match_index) {
            if ((matches[match_index][2] != 0x01U &&
                 matches[match_index][2] != 0x03U) ||
                g_runtime.value_patch_site_count ==
                    CDF_VALUE_PATCH_SITE_COUNT) {
                g_runtime.value_patch_site_count = 0U;
                return;
            }
            g_runtime.value_patch_sites[
                g_runtime.value_patch_site_count++] = matches[match_index];
        }
    }
    if (g_runtime.value_patch_site_count != CDF_VALUE_PATCH_SITE_COUNT) {
        g_runtime.value_patch_site_count = 0U;
    }
}

static void cdf_resolve_runtime_once(void) {
    static const int16_t scene_ready_pattern[] = {
        0x48, 0x89, 0x5C, 0x24, 0x10,
        0x48, 0x89, 0x6C, 0x24, 0x18,
        0x57, 0x48, 0x83, 0xEC, 0x20,
        0x48, 0x8B, 0xF9, 0x48, 0x89, 0x0D,
        -1, -1, -1, -1,
        0x48, 0x81, 0xC1, 0xE0, 0x04, 0x00, 0x00,
        0x48, 0x89, 0x74, 0x24, 0x30,
        0xC7, 0x05, -1, -1, -1, -1,
        0x01, 0x00, 0x00, 0x00};
    static const int16_t mode_enter_pattern[] = {
        0x48, 0x89, 0x5C, 0x24, 0x10,
        0x48, 0x89, 0x6C, 0x24, 0x18,
        0x48, 0x89, 0x74, 0x24, 0x20,
        0x57, 0x48, 0x81, 0xEC, -1, -1, -1, -1,
        0x48, 0x8B, 0x41, 0x08,
        0x48, 0x8B, 0xE9, 0xC6, 0x40, -1, 0x01};
    static const int16_t delete_pattern[] = {
        0x40, 0x57, 0x48, 0x83, 0xEC, 0x20,
        0x44, 0x8B, 0x51, 0x3C, 0x45, 0x33, 0xC0};
    static const int16_t storage_pattern[] = {
        0x49, 0x89, 0x5E, -1,
        0x48, 0x8B, 0x05, -1, -1, -1, -1,
        0x48, 0x8B, 0x48, -1,
        0x8B, 0x40, -1,
        0x48, 0x8D, 0x14, 0xC1,
        0x48, 0x3B, 0xCA, 0x74, 0x11};
    static const int16_t rebuild_pattern[] = {
        0x40, 0x57, 0x48, 0x83, 0xEC, -1,
        0x80, 0x79, -1, 0x00,
        0x48, 0x8B, 0xF9,
        0x0F, 0x84, -1, -1, -1, -1,
        0xC6, 0x41, -1, 0x00};
    static const int16_t row_cache_pattern[] = {
        0x49, 0x8B, 0x46, -1,
        0x48, 0x8B, 0x58, -1,
        0xBA, 0xEF, 0x03, 0x00, 0x00,
        0x48, 0x8B, 0xCB, 0xE8, -1, -1, -1, -1,
        0x48, 0x8B, 0x43, -1,
        0x48, 0x8B, 0x88, -1, -1, -1, -1};
    static const int16_t count_original_pattern[] = {
        0xF6, 0x42, 0x28, 0x02,
        0xB8, 0x00, 0x00, 0x00, 0x00,
        0x0F, 0x95, 0xC0, 0xFF, 0xC0};
    static const int16_t count_patched_pattern[] = {
        0x8B, 0x42, 0x28,
        0x83, 0xE0, 0x06,
        0xD1, 0xE8,
        0xFF, 0xC0,
        0x90, 0x90, 0x90, 0x90};
    uint8_t* scene_ready;
    uint8_t* mode_enter;
    uint8_t* storage_anchor;
    uint8_t* rebuild;
    uint8_t* row_cache;
    uint8_t* count_original;
    uint8_t* count_patched;
    int32_t collection_offset;

    if (g_runtime.attempted) {
        return;
    }
    memset(&g_runtime, 0, sizeof(g_runtime));
    g_runtime.attempted = 1;
    g_runtime.executable = (uint8_t*)GetModuleHandleW(NULL);
    g_runtime.image_size = cdf_image_size((HMODULE)g_runtime.executable);
    if (!g_runtime.executable || g_runtime.image_size == 0U) {
        return;
    }

    scene_ready = cdf_find_unique(
        scene_ready_pattern,
        sizeof(scene_ready_pattern) / sizeof(scene_ready_pattern[0]));
    mode_enter = cdf_find_unique(
        mode_enter_pattern,
        sizeof(mode_enter_pattern) / sizeof(mode_enter_pattern[0]));
    g_runtime.storage_delete = (CdfStorageDeleteFn)cdf_find_unique(
        delete_pattern,
        sizeof(delete_pattern) / sizeof(delete_pattern[0]));
    storage_anchor = cdf_find_unique(
        storage_pattern,
        sizeof(storage_pattern) / sizeof(storage_pattern[0]));
    rebuild = cdf_find_unique(
        rebuild_pattern,
        sizeof(rebuild_pattern) / sizeof(rebuild_pattern[0]));
    row_cache = cdf_find_unique(
        row_cache_pattern,
        sizeof(row_cache_pattern) / sizeof(row_cache_pattern[0]));

    if (scene_ready) {
        g_runtime.scene_ready_hook_rva =
            (uintptr_t)(scene_ready - g_runtime.executable);
    }
    if (mode_enter) {
        g_runtime.furniture_mode_enter_hook_rva =
            (uintptr_t)(mode_enter - g_runtime.executable);
        g_runtime.furniture_mode_active_offset = mode_enter[32];
    }
    if (storage_anchor) {
        g_runtime.furniture_row_stable_key_offset = storage_anchor[3];
        g_runtime.storage_global_slot = (void**)cdf_rip_target(
            storage_anchor + 4U, 3U, 7U);
        g_runtime.storage_data_offset = storage_anchor[14];
        g_runtime.storage_count_offset = storage_anchor[17];
    }
    if (rebuild && rebuild[8] == rebuild[21]) {
        g_runtime.furniture_rebuild_dirty_offset = rebuild[8];
    }
    if (row_cache) {
        g_runtime.furniture_ui_context_offset = row_cache[3];
        g_runtime.furniture_ui_root_offset = row_cache[7];
        g_runtime.furniture_ui_table_offset = row_cache[24];
        memcpy(&collection_offset, row_cache + 28U, sizeof(collection_offset));
        if (collection_offset > 0) {
            g_runtime.furniture_row_collection_offset =
                (size_t)collection_offset;
        }
    }

    g_runtime.core_available =
        g_runtime.scene_ready_hook_rva != 0U &&
        g_runtime.furniture_mode_enter_hook_rva != 0U &&
        g_runtime.storage_delete != NULL &&
        cdf_readable(g_runtime.storage_global_slot, sizeof(void*)) &&
        g_runtime.storage_data_offset != 0U &&
        g_runtime.storage_count_offset != 0U;
    g_runtime.ui_refresh_available =
        g_runtime.furniture_mode_active_offset != 0U &&
        g_runtime.furniture_rebuild_dirty_offset != 0U &&
        g_runtime.furniture_ui_context_offset != 0U &&
        g_runtime.furniture_ui_root_offset != 0U &&
        g_runtime.furniture_ui_table_offset != 0U &&
        g_runtime.furniture_row_collection_offset != 0U &&
        g_runtime.furniture_row_stable_key_offset != 0U;

    cdf_collect_value_patch_sites();
    count_original = cdf_find_unique(
        count_original_pattern,
        sizeof(count_original_pattern) / sizeof(count_original_pattern[0]));
    count_patched = cdf_find_unique(
        count_patched_pattern,
        sizeof(count_patched_pattern) / sizeof(count_patched_pattern[0]));
    if ((count_original == NULL) != (count_patched == NULL)) {
        g_runtime.count_patch_site = count_original
            ? count_original
            : count_patched;
    }
}

static int cdf_patch_bytes(
    uint8_t* address,
    const uint8_t* expected,
    const uint8_t* replacement,
    size_t size) {
    DWORD old_protect;
    DWORD ignored;
    if (!address || !expected || !replacement || size == 0U ||
        !cdf_readable(address, size) ||
        memcmp(address, expected, size) != 0 ||
        !VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &old_protect)) {
        return 0;
    }
    memcpy(address, replacement, size);
    FlushInstructionCache(GetCurrentProcess(), address, size);
    VirtualProtect(address, size, old_protect, &ignored);
    return memcmp(address, replacement, size) == 0;
}

CdfRuntimeResolution cdf_native_resolve_runtime(void) {
    CdfRuntimeResolution result;
    cdf_resolve_runtime_once();
    memset(&result, 0, sizeof(result));
    result.scene_ready_hook_rva = g_runtime.scene_ready_hook_rva;
    result.furniture_mode_enter_hook_rva =
        g_runtime.furniture_mode_enter_hook_rva;
    result.core_available = (uint8_t)g_runtime.core_available;
    result.ui_refresh_available = (uint8_t)g_runtime.ui_refresh_available;
    return result;
}

static void* cdf_storage(void) {
    cdf_resolve_runtime_once();
    if (!cdf_readable(g_runtime.storage_global_slot, sizeof(void*))) {
        return NULL;
    }
    __try {
        return *g_runtime.storage_global_slot;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return NULL;
    }
}

static int cdf_storage_entries(
    void* storage,
    void*** data,
    uint32_t* count) {
    size_t required;
    if (!storage || !data || !count || !g_runtime.core_available) {
        return 0;
    }
    required = g_runtime.storage_data_offset + sizeof(void*);
    if (!cdf_readable(storage, required)) {
        return 0;
    }
    __try {
        *count = *(uint32_t*)((uint8_t*)storage +
            g_runtime.storage_count_offset);
        *data = *(void***)((uint8_t*)storage +
            g_runtime.storage_data_offset);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    return *count <= 100000U &&
        (*count == 0U || cdf_readable(*data, (size_t)*count * sizeof(void*)));
}

static void* cdf_find_entry(
    uint64_t stable_key,
    const char* expected_item,
    uint64_t* flags) {
    void* storage = cdf_storage();
    void** entries;
    uint32_t count;
    uint32_t index;
    void* found = NULL;
    if (!stable_key || !expected_item ||
        !cdf_storage_entries(storage, &entries, &count)) {
        return NULL;
    }
    for (index = 0; index < count; ++index) {
        void* entry = entries[index];
        char item[CDF_NATIVE_TEXT_CAPACITY];
        if (!entry || !cdf_readable(entry, 0x68U)) {
            return NULL;
        }
        __try {
            if (*(uint64_t*)entry != stable_key) {
                continue;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return NULL;
        }
        if (found || !cdf_copy_string(
                (const CdfNarrowString*)((uint8_t*)entry +
                    CDF_ENTRY_ITEM_OFFSET),
                item,
                sizeof(item)) ||
            strcmp(item, expected_item) != 0) {
            return NULL;
        }
        found = entry;
        if (flags) {
            __try {
                *flags = *(uint64_t*)((uint8_t*)entry +
                    CDF_ENTRY_FLAGS_OFFSET);
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                return NULL;
            }
        }
    }
    return found;
}

void* cdf_native_furniture_mode_component(void* mode_enter_context) {
    void* component;
    cdf_resolve_runtime_once();
    if (!mode_enter_context || !cdf_readable(
            mode_enter_context, sizeof(void*) * 2U)) {
        return NULL;
    }
    __try {
        component = *(void**)((uint8_t*)mode_enter_context + sizeof(void*));
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return NULL;
    }
    return component && g_runtime.furniture_mode_active_offset != 0U &&
        cdf_readable(
            component, g_runtime.furniture_mode_active_offset + 1U)
        ? component
        : NULL;
}

int cdf_native_furniture_mode_active(void* component) {
    cdf_resolve_runtime_once();
    if (!component || g_runtime.furniture_mode_active_offset == 0U ||
        !cdf_readable(
            component, g_runtime.furniture_mode_active_offset + 1U)) {
        return 0;
    }
    __try {
        return *(uint8_t*)((uint8_t*)component +
            g_runtime.furniture_mode_active_offset) != 0U;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

static int cdf_furniture_ui_rows(
    void* component,
    void*** rows,
    uint32_t* count) {
    void* context;
    void* root;
    void* table;
    void* collection;
    if (!component || !rows || !count || !g_runtime.ui_refresh_available ||
        !cdf_readable(
            component, g_runtime.furniture_ui_context_offset + sizeof(void*))) {
        return 0;
    }
    *rows = NULL;
    *count = 0U;
    __try {
        context = *(void**)((uint8_t*)component +
            g_runtime.furniture_ui_context_offset);
        if (!context) {
            return 1;
        }
        if (!cdf_readable(
                context, g_runtime.furniture_ui_root_offset + sizeof(void*))) {
            return 0;
        }
        root = *(void**)((uint8_t*)context +
            g_runtime.furniture_ui_root_offset);
        if (!root) {
            return 1;
        }
        if (!cdf_readable(
                root, g_runtime.furniture_ui_table_offset + sizeof(void*))) {
            return 0;
        }
        table = *(void**)((uint8_t*)root +
            g_runtime.furniture_ui_table_offset);
        if (!table) {
            return 1;
        }
        if (!cdf_readable(
                table,
                g_runtime.furniture_row_collection_offset + sizeof(void*))) {
            return 0;
        }
        collection = *(void**)((uint8_t*)table +
            g_runtime.furniture_row_collection_offset);
        if (!collection) {
            return 1;
        }
        if (!cdf_readable(
                collection, CDF_COLLECTION_DATA_OFFSET + sizeof(void*))) {
            return 0;
        }
        *count = *(uint32_t*)((uint8_t*)collection +
            CDF_COLLECTION_COUNT_OFFSET);
        *rows = *(void***)((uint8_t*)collection +
            CDF_COLLECTION_DATA_OFFSET);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    return *count <= 100000U &&
        (*count == 0U || cdf_readable(
            *rows, (size_t)*count * sizeof(void*)));
}

static int cdf_contains_stable_key(
    const uint64_t* stable_keys,
    size_t count,
    uint64_t stable_key) {
    size_t index;
    for (index = 0; index < count; ++index) {
        if (stable_keys[index] == stable_key) {
            return 1;
        }
    }
    return 0;
}

CdfFurnitureUiRebuildResult cdf_native_prepare_furniture_ui_rebuild_on_enter(
    void* mode_enter_context,
    const uint64_t* stale_row_keys,
    size_t stale_row_key_count) {
    CdfFurnitureUiRebuildResult result;
    void* component;
    void** rows = NULL;
    uint32_t row_count = 0U;
    uint32_t index;
    memset(&result, 0, sizeof(result));
    cdf_resolve_runtime_once();
    if (!mode_enter_context ||
        (stale_row_key_count != 0U && !stale_row_keys)) {
        result.status = CDF_FURNITURE_UI_REBUILD_ENTER_CONTEXT_UNAVAILABLE;
        return result;
    }
    if (!g_runtime.ui_refresh_available) {
        result.status = CDF_FURNITURE_UI_REBUILD_LAYOUT_UNAVAILABLE;
        return result;
    }
    component = cdf_native_furniture_mode_component(mode_enter_context);
    if (!component) {
        result.status = CDF_FURNITURE_UI_REBUILD_COMPONENT_UNAVAILABLE;
        return result;
    }
    __try {
        if (*(uint8_t*)((uint8_t*)component +
                g_runtime.furniture_mode_active_offset) != 0U) {
            result.status = CDF_FURNITURE_UI_REBUILD_MODE_ACTIVE;
            return result;
        }
        *(uint8_t*)((uint8_t*)component +
            g_runtime.furniture_rebuild_dirty_offset) = 1U;
        if (*(uint8_t*)((uint8_t*)component +
                g_runtime.furniture_rebuild_dirty_offset) == 0U) {
            result.status = CDF_FURNITURE_UI_REBUILD_WRITE_FAILED;
            return result;
        }
        if (stale_row_key_count != 0U &&
            !cdf_furniture_ui_rows(component, &rows, &row_count)) {
            result.status = CDF_FURNITURE_UI_REBUILD_ROW_CACHE_UNREADABLE;
            return result;
        }
        result.rows_scanned = row_count;
        for (index = 0; index < row_count; ++index) {
            void* row = rows[index];
            uint64_t stable_key;
            if (!cdf_readable(
                    row, g_runtime.furniture_row_stable_key_offset +
                        sizeof(uint64_t))) {
                result.status = CDF_FURNITURE_UI_REBUILD_ROW_CACHE_UNREADABLE;
                return result;
            }
            stable_key = *(uint64_t*)((uint8_t*)row +
                g_runtime.furniture_row_stable_key_offset);
            if (cdf_contains_stable_key(
                    stale_row_keys, stale_row_key_count, stable_key)) {
                *(uint64_t*)((uint8_t*)row +
                    g_runtime.furniture_row_stable_key_offset) =
                    stable_key ^ (1ULL << 63U);
                ++result.rows_invalidated;
            }
        }
        result.status = CDF_FURNITURE_UI_REBUILD_ARMED;
        return result;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        result.status = CDF_FURNITURE_UI_REBUILD_EXCEPTION;
        return result;
    }
}

CdfNativeScanResult cdf_native_scan(
    CdfNativeFurniture* output,
    size_t output_capacity) {
    CdfNativeScanResult result;
    void* storage;
    void** entries;
    uint32_t count;
    uint32_t index;
    memset(&result, 0, sizeof(result));
    cdf_resolve_runtime_once();
    result.runtime_available = (uint8_t)g_runtime.core_available;
    if (!output || output_capacity == 0U || !g_runtime.core_available) {
        return result;
    }
    storage = cdf_storage();
    if (!cdf_storage_entries(storage, &entries, &count)) {
        return result;
    }
    result.complete = 1U;
    for (index = 0; index < count; ++index) {
        CdfNativeFurniture current;
        void* entry = entries[index];
        memset(&current, 0, sizeof(current));
        if (!entry || !cdf_readable(entry, 0x68U) ||
            result.count == output_capacity) {
            result.complete = 0U;
            continue;
        }
        __try {
            current.stable_key = *(uint64_t*)entry;
            current.placement_flags = *(uint64_t*)((uint8_t*)entry +
                CDF_ENTRY_FLAGS_OFFSET);
            current.entry = entry;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            result.complete = 0U;
            continue;
        }
        if (!current.stable_key || !cdf_copy_string(
                (const CdfNarrowString*)((uint8_t*)entry +
                    CDF_ENTRY_ITEM_OFFSET),
                current.item_id,
                sizeof(current.item_id)) ||
            !cdf_copy_string(
                (const CdfNarrowString*)((uint8_t*)entry +
                    CDF_ENTRY_ROOM_OFFSET),
                current.room_id,
                sizeof(current.room_id))) {
            result.complete = 0U;
            continue;
        }
        output[result.count++] = current;
    }
    return result;
}

CdfNativeMutationResult cdf_native_set_rare(
    uint64_t stable_key,
    const char* expected_item,
    uint64_t expected_flags,
    uint8_t enabled) {
    CdfNativeMutationResult result;
    uint64_t flags;
    void* entry;
    memset(&result, 0, sizeof(result));
    cdf_resolve_runtime_once();
    if (!g_runtime.core_available) {
        return result;
    }
    entry = cdf_find_entry(stable_key, expected_item, &flags);
    if (!entry || flags != expected_flags || (flags & ~6ULL) != 0ULL) {
        return result;
    }
    __try {
        if (enabled) {
            *(uint64_t*)((uint8_t*)entry + CDF_ENTRY_FLAGS_OFFSET) =
                flags | 2ULL;
        } else {
            *(uint64_t*)((uint8_t*)entry + CDF_ENTRY_FLAGS_OFFSET) =
                flags & ~2ULL;
        }
        result.success = (uint8_t)(
            ((*(uint64_t*)((uint8_t*)entry + CDF_ENTRY_FLAGS_OFFSET) & 2ULL)
             != 0ULL) == (enabled != 0U));
    }
    __except (cdf_capture_exception(
        GetExceptionInformation(), &result.seh_code, &result.exception_rva)) {
        result.success = 0U;
    }
    return result;
}

CdfNativeMutationResult cdf_native_set_enhanced(
    uint64_t stable_key,
    const char* expected_item,
    uint64_t expected_flags,
    uint8_t enabled) {
    CdfNativeMutationResult result;
    uint64_t flags;
    void* entry;
    memset(&result, 0, sizeof(result));
    cdf_resolve_runtime_once();
    if (!g_runtime.core_available) {
        return result;
    }
    entry = cdf_find_entry(stable_key, expected_item, &flags);
    if (!entry || flags != expected_flags || (flags & ~6ULL) != 0ULL ||
        (enabled && (flags & 2ULL) == 0ULL)) {
        return result;
    }
    __try {
        if (enabled) {
            *(uint64_t*)((uint8_t*)entry + CDF_ENTRY_FLAGS_OFFSET) =
                flags | 4ULL;
        } else {
            *(uint64_t*)((uint8_t*)entry + CDF_ENTRY_FLAGS_OFFSET) =
                flags & ~4ULL;
        }
        result.success = (uint8_t)(
            ((*(uint64_t*)((uint8_t*)entry + CDF_ENTRY_FLAGS_OFFSET) & 4ULL)
             != 0ULL) == (enabled != 0U));
    }
    __except (cdf_capture_exception(
        GetExceptionInformation(), &result.seh_code, &result.exception_rva)) {
        result.success = 0U;
    }
    return result;
}

CdfNativeMutationResult cdf_native_consume(
    uint64_t stable_key,
    const char* expected_item,
    uint64_t expected_flags) {
    CdfNativeMutationResult result;
    void* storage;
    void* entry;
    uint64_t flags;
    memset(&result, 0, sizeof(result));
    cdf_resolve_runtime_once();
    if (!g_runtime.core_available) {
        return result;
    }
    storage = cdf_storage();
    entry = cdf_find_entry(stable_key, expected_item, &flags);
    if (!storage || !entry || flags != expected_flags ||
        (flags & ~6ULL) != 0ULL) {
        return result;
    }
    __try {
        g_runtime.storage_delete(storage, stable_key);
        result.success = (uint8_t)(
            cdf_find_entry(stable_key, expected_item, NULL) == NULL);
    }
    __except (cdf_capture_exception(
        GetExceptionInformation(), &result.seh_code, &result.exception_rva)) {
        result.success = 0U;
    }
    return result;
}

CdfEnhancedPatchAudit cdf_native_ensure_enhanced_patches(void) {
    static const uint8_t value_expected[] = {0x80, 0xE3, 0x01};
    static const uint8_t value_replacement[] = {0x80, 0xE3, 0x03};
    static const uint8_t count_expected[] = {
        0xF6, 0x42, 0x28, 0x02,
        0xB8, 0x00, 0x00, 0x00, 0x00,
        0x0F, 0x95, 0xC0,
        0xFF, 0xC0};
    static const uint8_t count_replacement[] = {
        0x8B, 0x42, 0x28,
        0x83, 0xE0, 0x06,
        0xD1, 0xE8,
        0xFF, 0xC0,
        0x90, 0x90, 0x90, 0x90};
    CdfEnhancedPatchAudit result;
    const uint32_t count_site_bit = 1U << CDF_VALUE_PATCH_SITE_COUNT;
    const uint32_t all_sites_mask = (count_site_bit << 1U) - 1U;
    size_t index;
    memset(&result, 0, sizeof(result));
    cdf_resolve_runtime_once();
    if (g_runtime.value_patch_site_count != CDF_VALUE_PATCH_SITE_COUNT ||
        !g_runtime.count_patch_site) {
        result.conflict_mask = all_sites_mask;
        return result;
    }
    for (index = 0; index < CDF_VALUE_PATCH_SITE_COUNT; ++index) {
        uint8_t* site = g_runtime.value_patch_sites[index];
        const uint32_t site_bit = 1U << index;
        if (memcmp(site, value_expected, sizeof(value_expected)) == 0) {
            if (!cdf_patch_bytes(
                    site,
                    value_expected,
                    value_replacement,
                    sizeof(value_expected))) {
                result.conflict_mask |= site_bit;
                continue;
            }
            result.repaired_mask |= site_bit;
        }
        if (memcmp(site, value_replacement, sizeof(value_replacement)) == 0) {
            result.patched_mask |= site_bit;
        } else {
            result.conflict_mask |= site_bit;
        }
    }
    if (memcmp(
            g_runtime.count_patch_site,
            count_expected,
            sizeof(count_expected)) == 0) {
        if (!cdf_patch_bytes(
                g_runtime.count_patch_site,
                count_expected,
                count_replacement,
                sizeof(count_expected))) {
            result.conflict_mask |= count_site_bit;
        } else {
            result.repaired_mask |= count_site_bit;
        }
    }
    if (memcmp(
            g_runtime.count_patch_site,
            count_replacement,
            sizeof(count_replacement)) == 0) {
        result.patched_mask |= count_site_bit;
    } else {
        result.conflict_mask |= count_site_bit;
    }
    result.success = (uint8_t)(
        result.conflict_mask == 0U &&
        result.patched_mask == all_sites_mask);
    return result;
}
