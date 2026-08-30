#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CDF_NATIVE_TEXT_CAPACITY 128U

typedef struct CdfNativeFurniture {
    uint64_t stable_key;
    uint64_t placement_flags;
    void* entry;
    char item_id[CDF_NATIVE_TEXT_CAPACITY];
    char room_id[CDF_NATIVE_TEXT_CAPACITY];
} CdfNativeFurniture;

typedef struct CdfNativeScanResult {
    size_t count;
    uint8_t complete;
    uint8_t runtime_available;
} CdfNativeScanResult;

typedef struct CdfNativeMutationResult {
    uint8_t success;
    uint32_t seh_code;
    uintptr_t exception_rva;
} CdfNativeMutationResult;

typedef struct CdfRuntimeResolution {
    uintptr_t scene_ready_hook_rva;
    uintptr_t furniture_mode_enter_hook_rva;
    uint8_t core_available;
    uint8_t ui_refresh_available;
} CdfRuntimeResolution;

typedef struct CdfEnhancedPatchAudit {
    uint8_t success;
    uint32_t patched_mask;
    uint32_t repaired_mask;
    uint32_t conflict_mask;
} CdfEnhancedPatchAudit;

typedef enum CdfFurnitureUiRebuildStatus {
    CDF_FURNITURE_UI_REBUILD_NOT_ATTEMPTED = 0,
    CDF_FURNITURE_UI_REBUILD_ARMED = 1,
    CDF_FURNITURE_UI_REBUILD_ENTER_CONTEXT_UNAVAILABLE = -1,
    CDF_FURNITURE_UI_REBUILD_LAYOUT_UNAVAILABLE = -2,
    CDF_FURNITURE_UI_REBUILD_COMPONENT_UNAVAILABLE = -3,
    CDF_FURNITURE_UI_REBUILD_COMPONENT_UNREADABLE = -4,
    CDF_FURNITURE_UI_REBUILD_MODE_ACTIVE = -5,
    CDF_FURNITURE_UI_REBUILD_WRITE_FAILED = -6,
    CDF_FURNITURE_UI_REBUILD_EXCEPTION = -7,
    CDF_FURNITURE_UI_REBUILD_COMPONENT_INVALID = -8,
    CDF_FURNITURE_UI_REBUILD_ROW_CACHE_UNREADABLE = -9
} CdfFurnitureUiRebuildStatus;

typedef struct CdfFurnitureUiRebuildResult {
    CdfFurnitureUiRebuildStatus status;
    size_t rows_scanned;
    size_t rows_invalidated;
} CdfFurnitureUiRebuildResult;

CdfRuntimeResolution cdf_native_resolve_runtime(void);
void* cdf_native_furniture_mode_component(void* mode_enter_context);
int cdf_native_furniture_mode_active(void* component);
CdfFurnitureUiRebuildResult cdf_native_prepare_furniture_ui_rebuild_on_enter(
    void* mode_enter_context,
    const uint64_t* stale_row_keys,
    size_t stale_row_key_count);

CdfNativeScanResult cdf_native_scan(
    CdfNativeFurniture* output,
    size_t output_capacity);

CdfNativeMutationResult cdf_native_set_rare(
    uint64_t stable_key,
    const char* expected_item,
    uint64_t expected_flags,
    uint8_t enabled);

CdfNativeMutationResult cdf_native_set_enhanced(
    uint64_t stable_key,
    const char* expected_item,
    uint64_t expected_flags,
    uint8_t enabled);

CdfNativeMutationResult cdf_native_consume(
    uint64_t stable_key,
    const char* expected_item,
    uint64_t expected_flags);

CdfEnhancedPatchAudit cdf_native_ensure_enhanced_patches(void);

#ifdef __cplusplus
}
#endif
