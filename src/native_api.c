#include "native_api.h"

#include <windows.h>

#include <stdint.h>
#include <string.h>

enum {
    CDF_SCENE_READY_UPDATE_RVA = 0x962820,
    CDF_FURNITURE_STORAGE_DELETE_RVA = 0x2052E0,
    CDF_FURNITURE_TRASH_RVA = 0x2EF0D0,
    CDF_FURNITURE_STORAGE_GLOBAL_RVA = 0x13D16E0,
    CDF_FURNITURE_PIECE_VTABLE_RVA = 0xEDE690,
    CDF_FURNITURE_BUILDING_UI_VTABLE_RVA = 0xF082A8,
    CDF_SCENE_COMPONENT_LIST_OFFSET = 0x18,
    CDF_FURNITURE_MODE_ACTIVE_OFFSET = 0x78,
    CDF_COMPONENT_CONTEXT_OFFSET = 0x18,
    CDF_CONTEXT_DELETE_STATE_OFFSET = 0x18,
    CDF_PIECE_GRID_OFFSET = 0x48,
    CDF_PIECE_ENTRY_OFFSET = 0x2D8,
    CDF_ENTRY_ITEM_OFFSET = 0x08,
    CDF_ENTRY_FLAGS_OFFSET = 0x28,
    CDF_STORAGE_COUNT_OFFSET = 0x3C,
    CDF_STORAGE_DATA_OFFSET = 0x40
};

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
typedef void (__fastcall* CdfTrashPieceFn)(void*);

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
    CdfNativeMutationResult* result) {
    HMODULE executable = GetModuleHandleW(NULL);
    if (exception && exception->ExceptionRecord && result) {
        const uintptr_t address =
            (uintptr_t)exception->ExceptionRecord->ExceptionAddress;
        const uintptr_t base = (uintptr_t)executable;
        const uint32_t image_size = cdf_image_size(executable);
        result->seh_code = exception->ExceptionRecord->ExceptionCode;
        result->exception_rva = address >= base && address - base < image_size
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

static int cdf_signatures_valid(void) {
    static const uint8_t delete_signature[] = {
        0x40, 0x57, 0x48, 0x83, 0xEC, 0x20,
        0x44, 0x8B, 0x51, 0x3C,
        0x45, 0x33, 0xC0};
    static const uint8_t trash_signature[] = {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20,
        0x48, 0x8B, 0xD9,
        0xE8, 0xF2, 0xF2, 0xFF, 0xFF};
    const uint8_t* executable = (const uint8_t*)GetModuleHandleW(NULL);
    return executable &&
        cdf_signature(
            executable + CDF_FURNITURE_STORAGE_DELETE_RVA,
            delete_signature,
            sizeof(delete_signature)) &&
        cdf_signature(
            executable + CDF_FURNITURE_TRASH_RVA,
            trash_signature,
            sizeof(trash_signature));
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
    if (!executable || !cdf_readable(
            executable + CDF_FURNITURE_STORAGE_GLOBAL_RVA,
            sizeof(void*))) {
        return NULL;
    }
    __try {
        return *(void**)(executable + CDF_FURNITURE_STORAGE_GLOBAL_RVA);
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
    *match_count = 0U;
    if (!cdf_scene_components(scene_manager, &components, &component_count)) {
        return NULL;
    }
    for (index = 0; index < component_count; ++index) {
        void* component = components[index];
        void* entry;
        if (!cdf_valid_vtable(component, CDF_FURNITURE_PIECE_VTABLE_RVA) ||
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

int cdf_native_furniture_mode_active(void* scene_manager) {
    void** components;
    uint32_t count;
    uint32_t index;
    if (!cdf_scene_components(scene_manager, &components, &count)) {
        return 0;
    }
    for (index = 0; index < count; ++index) {
        void* component = components[index];
        if (!cdf_valid_vtable(
                component, CDF_FURNITURE_BUILDING_UI_VTABLE_RVA) ||
            !cdf_readable(
                component, CDF_FURNITURE_MODE_ACTIVE_OFFSET + 1U)) {
            continue;
        }
        __try {
            return *(uint8_t*)((uint8_t*)component +
                CDF_FURNITURE_MODE_ACTIVE_OFFSET) != 0U;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
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
                sizeof(current.item_id))) {
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
    if (!entry || flags != expected_flags || (flags & ~2ULL) != 0ULL) {
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
    __except (cdf_capture_exception(GetExceptionInformation(), &result)) {
        result.success = 0U;
    }
    return result;
}

CdfNativeMutationResult cdf_native_consume(
    void* scene_manager,
    uint64_t stable_key,
    const char* expected_item) {
    CdfNativeMutationResult result;
    uint8_t* executable = (uint8_t*)GetModuleHandleW(NULL);
    void* storage;
    void* entry;
    void* piece;
    uint64_t flags;
    uint32_t piece_count;
    memset(&result, 0, sizeof(result));
    if (!scene_manager || !cdf_signatures_valid()) {
        return result;
    }
    storage = cdf_storage();
    entry = cdf_find_entry(stable_key, expected_item, &flags);
    piece = cdf_find_piece(scene_manager, stable_key, &piece_count);
    if (!storage || !entry || (flags & ~2ULL) != 0ULL ||
        piece_count > 1U) {
        return result;
    }
    __try {
        if (piece_count == 1U && piece) {
            ((CdfTrashPieceFn)(executable + CDF_FURNITURE_TRASH_RVA))(piece);
        } else {
            ((CdfStorageDeleteFn)(
                executable + CDF_FURNITURE_STORAGE_DELETE_RVA))(
                    storage, stable_key);
        }
        result.success = (uint8_t)(
            cdf_find_entry(stable_key, expected_item, NULL) == NULL);
    }
    __except (cdf_capture_exception(GetExceptionInformation(), &result)) {
        result.success = 0U;
    }
    return result;
}
