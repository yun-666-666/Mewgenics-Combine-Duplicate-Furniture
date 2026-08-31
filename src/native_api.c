#include "native_api.h"

#include <windows.h>

#include <stdint.h>
#include <string.h>

enum {
    CDF_ENHANCED_VALUE_PATCH_COUNT = 6,
    CDF_SCENE_COMPONENT_LIST_OFFSET = 0x18,
    CDF_FURNITURE_REBUILD_DIRTY_OFFSET = 0x58,
    CDF_FURNITURE_MODE_ACTIVE_OFFSET = 0x78,
    CDF_FURNITURE_UI_CONTEXT_OFFSET = 0x18,
    CDF_FURNITURE_UI_ROOT_OFFSET = 0x08,
    CDF_FURNITURE_UI_COMPONENT_TABLE_OFFSET = 0x20,
    CDF_FURNITURE_ROW_COLLECTION_OFFSET = 0x3EF0,
    CDF_FURNITURE_ROW_COLLECTION_COUNT_OFFSET = 0x0C,
    CDF_FURNITURE_ROW_COLLECTION_DATA_OFFSET = 0x10,
    CDF_FURNITURE_ROW_STABLE_KEY_OFFSET = 0x48,
    CDF_COMPONENT_CONTEXT_OFFSET = 0x18,
    CDF_CONTEXT_DELETE_STATE_OFFSET = 0x18,
    CDF_PIECE_GRID_OFFSET = 0x48,
    CDF_PIECE_ENTRY_OFFSET = 0x2D8,
    CDF_ENTRY_ITEM_OFFSET = 0x08,
    CDF_ENTRY_FLAGS_OFFSET = 0x28,
    CDF_ENTRY_ROOM_OFFSET = 0x30,
    CDF_STORAGE_COUNT_OFFSET = 0x3C,
    CDF_STORAGE_DATA_OFFSET = 0x40
};

typedef struct CdfGameLayout {
    const char* build_name;
    uintptr_t scene_ready_update_rva;
    uintptr_t furniture_storage_delete_rva;
    uintptr_t furniture_store_piece_rva;
    uintptr_t furniture_storage_global_rva;
    uintptr_t furniture_list_effect_count_rva;
    uintptr_t furniture_value_flags_rvas[CDF_ENHANCED_VALUE_PATCH_COUNT];
    uintptr_t furniture_rebuild_rva;
    uintptr_t furniture_row_cache_rva;
    uintptr_t furniture_row_key_assign_rva;
    uintptr_t furniture_stale_row_cleanup_rva;
    uintptr_t furniture_mode_enter_function_rva;
    uintptr_t furniture_mode_enter_rebuild_rva;
    uintptr_t furniture_piece_vtable_rva;
    uintptr_t furniture_building_ui_vtable_rva;
} CdfGameLayout;

static const CdfGameLayout g_public_layout = {
    "1.1.b21039",
    0x962820,
    0x2052E0,
    0x2EF0D0,
    0x13D16E0,
    0x2EA2D7,
    {0x1A0012, 0x1A0097, 0x1A11F6, 0x1A1276, 0x1A58AD, 0x1A5922},
    0x1A5470,
    0x1A5D6C,
    0x1A0FC9,
    0x1A614A,
    0x1AB940,
    0x1AB95E,
    0xEDE690,
    0xF082A8};

static const CdfGameLayout g_beta_layout = {
    "1.1.b21220-beta",
    0x96ABD0,
    0x205D50,
    0x2EFBF0,
    0x13D99E0,
    0x2EADF7,
    {0x1A0A32, 0x1A0AB7, 0x1A1C16, 0x1A1C96, 0x1A62CD, 0x1A6342},
    0x1A5E90,
    0x1A678C,
    0x1A19E9,
    0x1A6B6A,
    0x1AC360,
    0x1AC37E,
    0xEE5858,
    0xF0F3C0};

static const CdfGameLayout* g_layout;

typedef struct CdfNarrowString {
    union {
        char inline_data[16];
        char* heap_data;
    } storage;
    uint64_t size;
    uint64_t capacity;
} CdfNarrowString;

typedef struct CdfPointerVector {
    uint32_t capacity;
    uint32_t size;
    void** data;
} CdfPointerVector;

typedef void (__fastcall* CdfStorageDeleteFn)(void*, uint64_t);
typedef void (__fastcall* CdfStorePieceFn)(void*);

static int cdf_readable(const void* pointer, size_t byte_count);

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

static int cdf_signature(
    const uint8_t* address,
    const uint8_t* expected,
    size_t size) {
    return cdf_readable(address, size) &&
        memcmp(address, expected, size) == 0;
}

static uintptr_t cdf_relative_target(const uint8_t* instruction) {
    int32_t displacement;
    if (!cdf_readable(instruction, 5U) || instruction[0] != 0xE8) {
        return 0U;
    }
    memcpy(&displacement, instruction + 1U, sizeof(displacement));
    return (uintptr_t)(instruction + 5U) + displacement;
}

static uintptr_t cdf_rip_target(const uint8_t* instruction) {
    int32_t displacement;
    if (!cdf_readable(instruction, 7U) || instruction[0] != 0x48 ||
        instruction[1] != 0x8B ||
        (instruction[2] & 0xC7U) != 0x05U) {
        return 0U;
    }
    memcpy(&displacement, instruction + 3U, sizeof(displacement));
    return (uintptr_t)(instruction + 7U) + displacement;
}

static int cdf_executable_address(const void* pointer) {
    MEMORY_BASIC_INFORMATION memory;
    DWORD protection;
    if (!pointer || !VirtualQuery(pointer, &memory, sizeof(memory)) ||
        memory.State != MEM_COMMIT ||
        (memory.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0U) {
        return 0;
    }
    protection = memory.Protect & 0xFFU;
    return protection == PAGE_EXECUTE ||
        protection == PAGE_EXECUTE_READ ||
        protection == PAGE_EXECUTE_READWRITE ||
        protection == PAGE_EXECUTE_WRITECOPY;
}

static int cdf_vtable_rtti_valid(
    const uint8_t* executable,
    uint32_t image_size,
    uintptr_t vtable_rva,
    const char* expected_name) {
    typedef struct CdfCompleteObjectLocator {
        uint32_t signature;
        uint32_t offset;
        uint32_t constructor_displacement;
        uint32_t type_descriptor_rva;
        uint32_t class_descriptor_rva;
        uint32_t self_rva;
    } CdfCompleteObjectLocator;
    const void* const* vtable;
    const CdfCompleteObjectLocator* locator;
    const char* name;
    uintptr_t locator_rva;
    size_t index;
    if (!executable || !expected_name || vtable_rva < sizeof(void*) ||
        vtable_rva >= image_size) {
        return 0;
    }
    vtable = (const void* const*)(executable + vtable_rva);
    if (!cdf_readable(vtable - 1, sizeof(void*) * 5U)) {
        return 0;
    }
    locator = (const CdfCompleteObjectLocator*)vtable[-1];
    if (!locator || (const uint8_t*)locator < executable ||
        (uintptr_t)((const uint8_t*)locator - executable) >= image_size ||
        !cdf_readable(locator, sizeof(*locator))) {
        return 0;
    }
    locator_rva = (uintptr_t)((const uint8_t*)locator - executable);
    if (locator->signature != 1U || locator->self_rva != locator_rva ||
        locator->type_descriptor_rva >= image_size) {
        return 0;
    }
    name = (const char*)(executable + locator->type_descriptor_rva + 16U);
    if (!cdf_readable(name, strlen(expected_name) + 1U) ||
        strcmp(name, expected_name) != 0) {
        return 0;
    }
    for (index = 0U; index < 4U; ++index) {
        if (!cdf_executable_address(vtable[index])) {
            return 0;
        }
    }
    return 1;
}

static int cdf_scene_ready_signature_valid(
    const uint8_t* executable,
    const CdfGameLayout* layout) {
    static const uint8_t prologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x10,
        0x48, 0x89, 0x6C, 0x24, 0x18,
        0x57, 0x48, 0x83, 0xEC, 0x20,
        0x48, 0x8B, 0xF9,
        0x48, 0x89, 0x0D};
    static const uint8_t body[] = {
        0x48, 0x81, 0xC1, 0xE0, 0x04, 0x00, 0x00,
        0x48, 0x89, 0x74, 0x24, 0x30,
        0xC7, 0x05};
    const uint8_t* function = executable + layout->scene_ready_update_rva;
    return cdf_signature(function, prologue, sizeof(prologue)) &&
        cdf_signature(function + 25U, body, sizeof(body));
}

static int cdf_storage_signatures_valid(
    const uint8_t* executable,
    const CdfGameLayout* layout) {
    static const uint8_t delete_signature[] = {
        0x40, 0x57, 0x48, 0x83, 0xEC, 0x20,
        0x44, 0x8B, 0x51, 0x3C,
        0x45, 0x33, 0xC0};
    static const uint8_t store_prologue[] = {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9};
    static const uint8_t store_entry[] = {
        0x48, 0x8B, 0x93, 0xE0, 0x02, 0x00, 0x00};
    const uint8_t* store = executable + layout->furniture_store_piece_rva;
    return
        cdf_signature(
            executable + layout->furniture_storage_delete_rva,
            delete_signature,
            sizeof(delete_signature)) &&
        cdf_signature(store, store_prologue, sizeof(store_prologue)) &&
        cdf_signature(store + 14U, store_entry, sizeof(store_entry)) &&
        cdf_rip_target(store + 21U) ==
            (uintptr_t)(executable + layout->furniture_storage_global_rva) &&
        cdf_relative_target(store + 28U) ==
            (uintptr_t)(executable + layout->furniture_storage_delete_rva);
}

static int cdf_enhanced_patch_signatures_valid(
    const uint8_t* executable,
    const CdfGameLayout* layout) {
    static const uint8_t value_expected[] = {0x80, 0xE3, 0x01};
    static const uint8_t value_replacement[] = {0x80, 0xE3, 0x03};
    static const uint8_t count_prefix[] = {
        0x49, 0x8B, 0x96, 0xD8, 0x02, 0x00, 0x00};
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
    size_t index;
    for (index = 0U; index < CDF_ENHANCED_VALUE_PATCH_COUNT; ++index) {
        const uint8_t* site =
            executable + layout->furniture_value_flags_rvas[index];
        if (!cdf_readable(site - 3U, sizeof(value_expected) + 3U) ||
            site[-3] != 0x28 || site[-2] != 0xD0 || site[-1] != 0xEB ||
            (memcmp(site, value_expected, sizeof(value_expected)) != 0 &&
             memcmp(site, value_replacement, sizeof(value_replacement)) != 0)) {
            return 0;
        }
    }
    {
        const uint8_t* site =
            executable + layout->furniture_list_effect_count_rva;
        return cdf_signature(site - sizeof(count_prefix),
                count_prefix, sizeof(count_prefix)) &&
            (cdf_signature(site, count_expected, sizeof(count_expected)) ||
             cdf_signature(
                site, count_replacement, sizeof(count_replacement)));
    }
}

static int cdf_furniture_rebuild_runtime_signatures_valid_for(
    const uint8_t* executable,
    const CdfGameLayout* layout) {
    static const uint8_t rebuild_signature[] = {
        0x40, 0x57, 0x48, 0x83, 0xEC, 0x40,
        0x80, 0x79, 0x58, 0x00};
    static const uint8_t mode_enter_signature[] = {
        0xC6, 0x40, 0x78, 0x01,
        0x48, 0x8B, 0x79, 0x08,
        0x48, 0x8B, 0xCF,
        0xE8};
    static const uint8_t row_cache_prefix[] = {
        0x49, 0x8B, 0x46, 0x18,
        0x48, 0x8B, 0x58, 0x08,
        0xBA, 0xEF, 0x03, 0x00, 0x00,
        0x48, 0x8B, 0xCB,
        0xE8};
    static const uint8_t row_cache_suffix[] = {
        0x48, 0x8B, 0x43, 0x20,
        0x48, 0x8B, 0x88, 0xF0, 0x3E, 0x00, 0x00};
    static const uint8_t row_key_signature[] = {
        0x49, 0x89, 0x5E, 0x48};
    static const uint8_t stale_row_cleanup_signature[] = {
        0x48, 0x8B, 0x7C, 0x24, 0x28,
        0x48, 0x8B, 0x1F,
        0x48, 0x3B, 0xDF,
        0x74, 0x16};
    const uint8_t* rebuild = executable + layout->furniture_rebuild_rva;
    const uint8_t* mode_enter =
        executable + layout->furniture_mode_enter_rebuild_rva;
    const uint8_t* row_cache =
        executable + layout->furniture_row_cache_rva;
    return
        cdf_signature(
            rebuild,
            rebuild_signature,
            sizeof(rebuild_signature)) &&
        cdf_rip_target(rebuild + 30U) ==
            (uintptr_t)(executable + layout->furniture_storage_global_rva) &&
        cdf_signature(
            mode_enter,
            mode_enter_signature,
            sizeof(mode_enter_signature)) &&
        cdf_relative_target(mode_enter + 11U) ==
            (uintptr_t)(executable + layout->furniture_rebuild_rva) &&
        cdf_signature(
            row_cache,
            row_cache_prefix,
            sizeof(row_cache_prefix)) &&
        cdf_signature(
            row_cache + 21U,
            row_cache_suffix,
            sizeof(row_cache_suffix)) &&
        cdf_signature(
            executable + layout->furniture_row_key_assign_rva,
            row_key_signature,
            sizeof(row_key_signature)) &&
        cdf_signature(
            executable + layout->furniture_stale_row_cleanup_rva,
            stale_row_cleanup_signature,
            sizeof(stale_row_cleanup_signature));
}

static int cdf_furniture_mode_enter_function_signature_valid_for(
    const uint8_t* executable,
    const CdfGameLayout* layout) {
    static const uint8_t mode_enter_function_signature[] = {
        0x48, 0x89, 0x5C, 0x24, 0x10,
        0x48, 0x89, 0x6C, 0x24, 0x18,
        0x48, 0x89, 0x74, 0x24, 0x20};
    return cdf_signature(
        executable + layout->furniture_mode_enter_function_rva,
        mode_enter_function_signature,
        sizeof(mode_enter_function_signature));
}

static int cdf_runtime_signatures_valid_for(
    const uint8_t* executable,
    uint32_t image_size,
    const CdfGameLayout* layout) {
    return cdf_storage_signatures_valid(executable, layout) &&
        cdf_furniture_rebuild_runtime_signatures_valid_for(
            executable, layout) &&
        cdf_enhanced_patch_signatures_valid(executable, layout) &&
        cdf_vtable_rtti_valid(
            executable,
            image_size,
            layout->furniture_piece_vtable_rva,
            ".?AVFurniturePiece@glaiel@@") &&
        cdf_vtable_rtti_valid(
            executable,
            image_size,
            layout->furniture_building_ui_vtable_rva,
            ".?AVFurnitureBuildingUI@glaiel@@");
}

static int cdf_layout_signatures_valid(const CdfGameLayout* layout) {
    const uint8_t* executable = (const uint8_t*)GetModuleHandleW(NULL);
    const uint32_t image_size = cdf_image_size((HMODULE)executable);
    return executable && layout && image_size != 0U &&
        cdf_scene_ready_signature_valid(executable, layout) &&
        cdf_furniture_mode_enter_function_signature_valid_for(
            executable, layout) &&
        cdf_runtime_signatures_valid_for(executable, image_size, layout);
}

static const CdfGameLayout* cdf_current_layout(void) {
    if (!g_layout) {
        if (cdf_layout_signatures_valid(&g_public_layout)) {
            g_layout = &g_public_layout;
        } else if (cdf_layout_signatures_valid(&g_beta_layout)) {
            g_layout = &g_beta_layout;
        }
    }
    return g_layout;
}

static int cdf_signatures_valid(void) {
    const uint8_t* executable = (const uint8_t*)GetModuleHandleW(NULL);
    const uint32_t image_size = cdf_image_size((HMODULE)executable);
    const CdfGameLayout* layout = cdf_current_layout();
    return executable && layout && image_size != 0U &&
        cdf_runtime_signatures_valid_for(executable, image_size, layout);
}

static int cdf_furniture_rebuild_runtime_signatures_valid(void) {
    const uint8_t* executable = (const uint8_t*)GetModuleHandleW(NULL);
    const CdfGameLayout* layout = cdf_current_layout();
    return executable && layout &&
        cdf_furniture_rebuild_runtime_signatures_valid_for(
            executable, layout);
}

static int cdf_furniture_mode_enter_hook_signature_valid(void) {
    const uint8_t* executable = (const uint8_t*)GetModuleHandleW(NULL);
    const CdfGameLayout* layout = cdf_current_layout();
    return executable && layout &&
        cdf_furniture_mode_enter_function_signature_valid_for(
            executable, layout) &&
        cdf_furniture_rebuild_runtime_signatures_valid_for(
            executable, layout);
}

static int cdf_valid_vtable(
    const void* object,
    uintptr_t expected_rva) {
    const uint8_t* executable = (const uint8_t*)GetModuleHandleW(NULL);
    return executable && cdf_readable(object, sizeof(void*)) &&
        *(void* const*)object == executable + expected_rva;
}

static int cdf_delete_queued(void* component) {
    void* context;
    if (!component || !cdf_readable(
            component, CDF_COMPONENT_CONTEXT_OFFSET + sizeof(void*))) {
        return 0;
    }
    __try {
        context = *(void**)((uint8_t*)component +
            CDF_COMPONENT_CONTEXT_OFFSET);
        return context && cdf_readable(
                context, CDF_CONTEXT_DELETE_STATE_OFFSET + 1U) &&
            *(uint8_t*)((uint8_t*)context +
                CDF_CONTEXT_DELETE_STATE_OFFSET) != 0U;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

static int cdf_scene_components(
    void* scene_manager,
    void*** data,
    uint32_t* count) {
    CdfPointerVector* components;
    if (!scene_manager || !data || !count || !cdf_readable(
            scene_manager, CDF_SCENE_COMPONENT_LIST_OFFSET + sizeof(void*))) {
        return 0;
    }
    __try {
        components = *(CdfPointerVector**)((uint8_t*)scene_manager +
            CDF_SCENE_COMPONENT_LIST_OFFSET);
        if (!components || !cdf_readable(components, sizeof(*components)) ||
            components->size > components->capacity) {
            return 0;
        }
        *count = components->size;
        *data = components->data;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    return *count <= 100000U &&
        (*count == 0U || cdf_readable(*data, (size_t)*count * sizeof(void*)));
}

static void* cdf_storage(void) {
    uint8_t* executable = (uint8_t*)GetModuleHandleW(NULL);
    const CdfGameLayout* layout = cdf_current_layout();
    if (!executable || !layout || !cdf_readable(
            executable + layout->furniture_storage_global_rva,
            sizeof(void*))) {
        return NULL;
    }
    __try {
        return *(void**)(
            executable + layout->furniture_storage_global_rva);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return NULL;
    }
}

static int cdf_storage_entries(
    void* storage,
    void*** data,
    uint32_t* count) {
    if (!storage || !data || !count || !cdf_readable(
            storage, CDF_STORAGE_DATA_OFFSET + sizeof(void*))) {
        return 0;
    }
    __try {
        *count = *(uint32_t*)((uint8_t*)storage + CDF_STORAGE_COUNT_OFFSET);
        *data = *(void***)((uint8_t*)storage + CDF_STORAGE_DATA_OFFSET);
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

static void* cdf_find_piece(
    void* scene_manager,
    uint64_t stable_key,
    uint32_t* match_count) {
    void** components;
    uint32_t component_count;
    uint32_t index;
    void* found = NULL;
    const CdfGameLayout* layout = cdf_current_layout();
    *match_count = 0U;
    if (!layout ||
        !cdf_scene_components(scene_manager, &components, &component_count)) {
        return NULL;
    }
    for (index = 0; index < component_count; ++index) {
        void* component = components[index];
        void* entry;
        if (!cdf_valid_vtable(
                component, layout->furniture_piece_vtable_rva) ||
            cdf_delete_queued(component) || !cdf_readable(
                component, CDF_PIECE_ENTRY_OFFSET + sizeof(void*))) {
            continue;
        }
        __try {
            entry = *(void**)((uint8_t*)component + CDF_PIECE_ENTRY_OFFSET);
            if (!entry || !cdf_readable(entry, sizeof(uint64_t)) ||
                *(uint64_t*)entry != stable_key) {
                continue;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            continue;
        }
        ++*match_count;
        found = component;
    }
    return found;
}

static void* cdf_find_furniture_ui(void* scene_manager) {
    void** components;
    uint32_t count;
    uint32_t index;
    const CdfGameLayout* layout = cdf_current_layout();
    if (!layout || !cdf_scene_components(scene_manager, &components, &count)) {
        return NULL;
    }
    for (index = 0; index < count; ++index) {
        void* component = components[index];
        if (cdf_valid_vtable(
                component, layout->furniture_building_ui_vtable_rva)) {
            return component;
        }
    }
    return NULL;
}

int cdf_native_resolve_layout(CdfNativeLayoutInfo* output) {
    const CdfGameLayout* layout;
    if (!output) {
        return 0;
    }
    memset(output, 0, sizeof(*output));
    layout = cdf_current_layout();
    if (!layout || !cdf_layout_signatures_valid(layout)) {
        return 0;
    }
    output->build_name = layout->build_name;
    output->scene_ready_update_rva = layout->scene_ready_update_rva;
    output->furniture_mode_enter_rva =
        layout->furniture_mode_enter_function_rva;
    output->scene_ready_stolen_bytes = 15;
    output->furniture_mode_enter_stolen_bytes = 15;
    return 1;
}

int cdf_native_furniture_mode_active(void* scene_manager) {
    void* component = cdf_find_furniture_ui(scene_manager);
    if (!component || !cdf_readable(
            component, CDF_FURNITURE_MODE_ACTIVE_OFFSET + 1U)) {
        return 0;
    }
    __try {
        return *(uint8_t*)((uint8_t*)component +
            CDF_FURNITURE_MODE_ACTIVE_OFFSET) != 0U;
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
    if (!component || !rows || !count || !cdf_readable(
            component, CDF_FURNITURE_UI_CONTEXT_OFFSET + sizeof(void*))) {
        return 0;
    }
    *rows = NULL;
    *count = 0U;
    __try {
        context = *(void**)((uint8_t*)component +
            CDF_FURNITURE_UI_CONTEXT_OFFSET);
        if (!context) {
            return 1;
        }
        if (!cdf_readable(context, CDF_FURNITURE_UI_ROOT_OFFSET +
                sizeof(void*))) {
            return 0;
        }
        root = *(void**)((uint8_t*)context + CDF_FURNITURE_UI_ROOT_OFFSET);
        if (!root) {
            return 1;
        }
        if (!cdf_readable(root, CDF_FURNITURE_UI_COMPONENT_TABLE_OFFSET +
                sizeof(void*))) {
            return 0;
        }
        table = *(void**)((uint8_t*)root +
            CDF_FURNITURE_UI_COMPONENT_TABLE_OFFSET);
        if (!table) {
            return 1;
        }
        if (!cdf_readable(table,
                CDF_FURNITURE_ROW_COLLECTION_OFFSET + sizeof(void*))) {
            return 0;
        }
        collection = *(void**)((uint8_t*)table +
            CDF_FURNITURE_ROW_COLLECTION_OFFSET);
        if (!collection) {
            return 1;
        }
        if (!cdf_readable(collection,
                CDF_FURNITURE_ROW_COLLECTION_DATA_OFFSET + sizeof(void*))) {
            return 0;
        }
        *count = *(uint32_t*)((uint8_t*)collection +
            CDF_FURNITURE_ROW_COLLECTION_COUNT_OFFSET);
        *rows = *(void***)((uint8_t*)collection +
            CDF_FURNITURE_ROW_COLLECTION_DATA_OFFSET);
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
    const CdfGameLayout* layout = cdf_current_layout();
    memset(&result, 0, sizeof(result));
    if (!layout || !mode_enter_context || !cdf_readable(
            mode_enter_context, sizeof(void*) * 2U) ||
        (stale_row_key_count != 0U && !stale_row_keys)) {
        result.status = CDF_FURNITURE_UI_REBUILD_ENTER_CONTEXT_UNAVAILABLE;
        return result;
    }
    if (!cdf_furniture_rebuild_runtime_signatures_valid()) {
        result.status = CDF_FURNITURE_UI_REBUILD_SIGNATURE_MISMATCH;
        return result;
    }
    __try {
        component = *(void**)((uint8_t*)mode_enter_context + sizeof(void*));
        if (!component) {
            result.status = CDF_FURNITURE_UI_REBUILD_COMPONENT_UNAVAILABLE;
            return result;
        }
        if (!cdf_valid_vtable(
                component, layout->furniture_building_ui_vtable_rva)) {
            result.status = CDF_FURNITURE_UI_REBUILD_COMPONENT_INVALID;
            return result;
        }
        if (!cdf_readable(
                component, CDF_FURNITURE_MODE_ACTIVE_OFFSET + 1U)) {
            result.status = CDF_FURNITURE_UI_REBUILD_COMPONENT_UNREADABLE;
            return result;
        }
        if (*(uint8_t*)((uint8_t*)component +
                CDF_FURNITURE_MODE_ACTIVE_OFFSET) != 0U) {
            result.status = CDF_FURNITURE_UI_REBUILD_MODE_ACTIVE;
            return result;
        }
        *(uint8_t*)((uint8_t*)component +
            CDF_FURNITURE_REBUILD_DIRTY_OFFSET) = 1U;
        if (*(uint8_t*)((uint8_t*)component +
                CDF_FURNITURE_REBUILD_DIRTY_OFFSET) == 0U) {
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
                    row, CDF_FURNITURE_ROW_STABLE_KEY_OFFSET +
                        sizeof(uint64_t))) {
                result.status = CDF_FURNITURE_UI_REBUILD_ROW_CACHE_UNREADABLE;
                return result;
            }
            stable_key = *(uint64_t*)((uint8_t*)row +
                CDF_FURNITURE_ROW_STABLE_KEY_OFFSET);
            if (cdf_contains_stable_key(
                    stale_row_keys, stale_row_key_count, stable_key)) {
                *(uint64_t*)((uint8_t*)row +
                    CDF_FURNITURE_ROW_STABLE_KEY_OFFSET) =
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

int cdf_native_furniture_mode_enter_refresh_supported(void) {
    return cdf_furniture_mode_enter_hook_signature_valid();
}

int cdf_native_scene_contains_component(
    void* scene_manager,
    const void* component) {
    void** components;
    uint32_t count;
    uint32_t index;
    if (!component ||
        !cdf_scene_components(scene_manager, &components, &count)) {
        return 0;
    }
    for (index = 0; index < count; ++index) {
        if (components[index] == component) {
            return 1;
        }
    }
    return 0;
}

CdfNativeScanResult cdf_native_scan(
    void* scene_manager,
    CdfNativeFurniture* output,
    size_t output_capacity) {
    CdfNativeScanResult result;
    void* storage;
    void** entries;
    uint32_t count;
    uint32_t index;
    memset(&result, 0, sizeof(result));
    result.signatures_valid = (uint8_t)cdf_signatures_valid();
    if (!output || output_capacity == 0U || !scene_manager) {
        return result;
    }
    storage = cdf_storage();
    if (!cdf_storage_entries(storage, &entries, &count)) {
        return result;
    }
    result.complete = 1U;
    for (index = 0; index < count; ++index) {
        CdfNativeFurniture current;
        uint32_t match_count;
        void* piece;
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
        piece = cdf_find_piece(
            scene_manager, current.stable_key, &match_count);
        current.piece = piece;
        current.runtime_match_count = match_count;
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
    if (!cdf_signatures_valid()) {
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
    if (!cdf_signatures_valid()) {
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
    void* scene_manager,
    uint64_t stable_key,
    const char* expected_item,
    uint64_t expected_flags) {
    CdfNativeMutationResult result;
    uint8_t* executable = (uint8_t*)GetModuleHandleW(NULL);
    const CdfGameLayout* layout = cdf_current_layout();
    void* storage;
    void* entry;
    uint64_t flags;
    uint32_t piece_count;
    memset(&result, 0, sizeof(result));
    if (!scene_manager || !layout || !cdf_signatures_valid()) {
        return result;
    }
    storage = cdf_storage();
    entry = cdf_find_entry(stable_key, expected_item, &flags);
    cdf_find_piece(scene_manager, stable_key, &piece_count);
    if (!storage || !entry || flags != expected_flags ||
        (flags & ~6ULL) != 0ULL ||
        piece_count != 0U) {
        return result;
    }
    __try {
        ((CdfStorageDeleteFn)(
            executable + layout->furniture_storage_delete_rva))(
                storage, stable_key);
        result.success = (uint8_t)(
            cdf_find_entry(stable_key, expected_item, NULL) == NULL);
    }
    __except (cdf_capture_exception(
        GetExceptionInformation(), &result.seh_code, &result.exception_rva)) {
        result.success = 0U;
    }
    return result;
}

CdfNativeStoreResult cdf_native_store(
    void* scene_manager,
    uint64_t stable_key,
    const char* expected_item) {
    CdfNativeStoreResult result;
    uint8_t* executable = (uint8_t*)GetModuleHandleW(NULL);
    const CdfGameLayout* layout = cdf_current_layout();
    void* entry;
    void* piece;
    uint64_t flags;
    uint32_t piece_count;
    void* before_grid;
    void* before_piece_entry;
    void* after_grid;
    void* after_piece_entry;
    memset(&result, 0, sizeof(result));
    if (!layout || !scene_manager || !expected_item ||
        expected_item[0] == '\0') {
        return result;
    }
    result.probe_flags |= CDF_STORE_PROBE_INPUT_VALID;
    if (!cdf_signatures_valid()) {
        return result;
    }
    result.probe_flags |= CDF_STORE_PROBE_SIGNATURES_VALID;
    entry = cdf_find_entry(stable_key, expected_item, &flags);
    if (entry) {
        result.probe_flags |= CDF_STORE_PROBE_ENTRY_FOUND;
        result.entry_flags = flags;
    }
    piece = cdf_find_piece(scene_manager, stable_key, &piece_count);
    result.piece_count = piece_count;
    if (piece) {
        result.probe_flags |= CDF_STORE_PROBE_PIECE_FOUND;
    }
    if (piece_count == 1U) {
        result.probe_flags |= CDF_STORE_PROBE_PIECE_COUNT_ONE;
    }
    if (entry && (flags & ~6ULL) == 0ULL) {
        result.probe_flags |= CDF_STORE_PROBE_FLAGS_VALID;
    }
    if (!entry || !piece || piece_count != 1U ||
        (flags & ~6ULL) != 0ULL) {
        return result;
    }
    __try {
        before_grid = *(void**)((uint8_t*)piece + CDF_PIECE_GRID_OFFSET);
        before_piece_entry =
            *(void**)((uint8_t*)piece + CDF_PIECE_ENTRY_OFFSET);
        if (before_grid) {
            result.probe_flags |= CDF_STORE_PROBE_BEFORE_GRID_PRESENT;
        }
        if (before_piece_entry == entry) {
            result.probe_flags |= CDF_STORE_PROBE_BEFORE_ENTRY_MATCH;
        }
        if (cdf_copy_string(
                (const CdfNarrowString*)((uint8_t*)entry +
                    CDF_ENTRY_ROOM_OFFSET),
                result.before_room,
                sizeof(result.before_room))) {
            result.probe_flags |= CDF_STORE_PROBE_BEFORE_ROOM_READ;
            if (result.before_room[0] != '\0') {
                result.probe_flags |= CDF_STORE_PROBE_BEFORE_ROOM_NONEMPTY;
            }
        }
        ((CdfStorePieceFn)(
            executable + layout->furniture_store_piece_rva))(piece);
        result.probe_flags |= CDF_STORE_PROBE_CALL_COMPLETED;
        result.pending_component = piece;
        if (cdf_delete_queued(piece)) {
            result.probe_flags |= CDF_STORE_PROBE_AFTER_DELETE_QUEUED;
        }
        if (cdf_copy_string(
                (const CdfNarrowString*)((uint8_t*)entry +
                    CDF_ENTRY_ROOM_OFFSET),
                result.after_room,
                sizeof(result.after_room))) {
            result.probe_flags |= CDF_STORE_PROBE_AFTER_ROOM_READ;
            if (result.after_room[0] == '\0') {
                result.probe_flags |= CDF_STORE_PROBE_AFTER_ROOM_EMPTY;
            }
        }
        if (cdf_readable(
                piece, CDF_PIECE_ENTRY_OFFSET + sizeof(void*))) {
            after_grid =
                *(void**)((uint8_t*)piece + CDF_PIECE_GRID_OFFSET);
            after_piece_entry =
                *(void**)((uint8_t*)piece + CDF_PIECE_ENTRY_OFFSET);
            if (!after_grid) {
                result.probe_flags |= CDF_STORE_PROBE_AFTER_GRID_NULL;
            }
            if (!after_piece_entry) {
                result.probe_flags |= CDF_STORE_PROBE_AFTER_ENTRY_NULL;
            }
        }
        if (cdf_find_entry(stable_key, expected_item, NULL) == entry) {
            result.probe_flags |= CDF_STORE_PROBE_AFTER_STORAGE_ENTRY_SAME;
        }
        if (cdf_native_scene_contains_component(scene_manager, piece)) {
            result.probe_flags |= CDF_STORE_PROBE_AFTER_SCENE_CONTAINS;
        }
        result.success = (uint8_t)(
            (result.probe_flags & CDF_STORE_PROBE_AFTER_DELETE_QUEUED) != 0U &&
            (result.probe_flags & CDF_STORE_PROBE_AFTER_ROOM_EMPTY) != 0U &&
            (result.probe_flags & CDF_STORE_PROBE_AFTER_ENTRY_NULL) != 0U &&
            (result.probe_flags & CDF_STORE_PROBE_AFTER_STORAGE_ENTRY_SAME) != 0U);
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
    uint8_t* executable = (uint8_t*)GetModuleHandleW(NULL);
    const CdfGameLayout* layout = cdf_current_layout();
    CdfEnhancedPatchAudit result;
    const uint32_t count_site_bit =
        1U << CDF_ENHANCED_VALUE_PATCH_COUNT;
    const uint32_t all_sites_mask = (count_site_bit << 1U) - 1U;
    size_t index;
    memset(&result, 0, sizeof(result));
    if (!executable || !layout || !cdf_signatures_valid()) {
        result.conflict_mask = all_sites_mask;
        return result;
    }
    for (index = 0; index < CDF_ENHANCED_VALUE_PATCH_COUNT; ++index) {
        const uint8_t* site =
            executable + layout->furniture_value_flags_rvas[index];
        const uint32_t site_bit = 1U << index;
        if (!cdf_readable(site, sizeof(value_expected)) ||
            (memcmp(site, value_expected, sizeof(value_expected)) != 0 &&
             memcmp(site, value_replacement, sizeof(value_replacement)) != 0)) {
            result.conflict_mask |= site_bit;
        }
    }
    {
        const uint8_t* count_site =
            executable + layout->furniture_list_effect_count_rva;
        if (!cdf_readable(count_site, sizeof(count_expected)) ||
            (memcmp(count_site, count_expected, sizeof(count_expected)) != 0 &&
             memcmp(
                 count_site,
                 count_replacement,
                 sizeof(count_replacement)) != 0)) {
            result.conflict_mask |= count_site_bit;
        }
    }
    if (result.conflict_mask != 0U) {
        return result;
    }
    for (index = 0; index < CDF_ENHANCED_VALUE_PATCH_COUNT; ++index) {
        uint8_t* site =
            executable + layout->furniture_value_flags_rvas[index];
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
        }
    }
    {
        uint8_t* count_site =
            executable + layout->furniture_list_effect_count_rva;
        if (memcmp(count_site, count_expected, sizeof(count_expected)) == 0) {
            if (!cdf_patch_bytes(
                    count_site,
                    count_expected,
                    count_replacement,
                    sizeof(count_expected))) {
                result.conflict_mask |= count_site_bit;
            } else {
                result.repaired_mask |= count_site_bit;
            }
        }
        if (memcmp(
                count_site,
                count_replacement,
                sizeof(count_replacement)) == 0) {
            result.patched_mask |= count_site_bit;
        }
    }
    result.success = (uint8_t)(
        result.conflict_mask == 0U &&
        result.patched_mask == all_sites_mask);
    return result;
}
