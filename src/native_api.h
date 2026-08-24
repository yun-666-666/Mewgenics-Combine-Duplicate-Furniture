#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CDF_NATIVE_TEXT_CAPACITY 128U

#define CDF_STORE_PROBE_INPUT_VALID             (1U << 0)
#define CDF_STORE_PROBE_SIGNATURES_VALID        (1U << 1)
#define CDF_STORE_PROBE_ENTRY_FOUND             (1U << 2)
#define CDF_STORE_PROBE_PIECE_FOUND             (1U << 3)
#define CDF_STORE_PROBE_PIECE_COUNT_ONE         (1U << 4)
#define CDF_STORE_PROBE_FLAGS_VALID             (1U << 5)
#define CDF_STORE_PROBE_BEFORE_ROOM_READ        (1U << 6)
#define CDF_STORE_PROBE_BEFORE_ROOM_NONEMPTY    (1U << 7)
#define CDF_STORE_PROBE_BEFORE_GRID_PRESENT     (1U << 8)
#define CDF_STORE_PROBE_BEFORE_ENTRY_MATCH      (1U << 9)
#define CDF_STORE_PROBE_CALL_COMPLETED          (1U << 10)
#define CDF_STORE_PROBE_AFTER_DELETE_QUEUED     (1U << 11)
#define CDF_STORE_PROBE_AFTER_ROOM_READ         (1U << 12)
#define CDF_STORE_PROBE_AFTER_ROOM_EMPTY        (1U << 13)
#define CDF_STORE_PROBE_AFTER_GRID_NULL         (1U << 14)
#define CDF_STORE_PROBE_AFTER_ENTRY_NULL        (1U << 15)
#define CDF_STORE_PROBE_AFTER_STORAGE_ENTRY_SAME (1U << 16)
#define CDF_STORE_PROBE_AFTER_SCENE_CONTAINS    (1U << 17)

typedef struct CdfNativeFurniture {
    uint64_t stable_key;
    uint64_t placement_flags;
    void* entry;
    void* piece;
    uint32_t runtime_match_count;
    uint8_t delete_pending;
    char item_id[CDF_NATIVE_TEXT_CAPACITY];
    char room_id[CDF_NATIVE_TEXT_CAPACITY];
} CdfNativeFurniture;

typedef struct CdfNativeScanResult {
    size_t count;
    uint8_t complete;
    uint8_t signatures_valid;
} CdfNativeScanResult;

typedef struct CdfNativeMutationResult {
    uint8_t success;
    uint32_t seh_code;
    uintptr_t exception_rva;
    void* pending_component;
} CdfNativeMutationResult;

typedef struct CdfNativeStoreResult {
    uint8_t success;
    uint32_t seh_code;
    uintptr_t exception_rva;
    void* pending_component;
    uint32_t probe_flags;
    uint32_t piece_count;
    uint64_t entry_flags;
    char before_room[CDF_NATIVE_TEXT_CAPACITY];
    char after_room[CDF_NATIVE_TEXT_CAPACITY];
} CdfNativeStoreResult;

int cdf_native_furniture_mode_active(void* scene_manager);

CdfNativeScanResult cdf_native_scan(
    void* scene_manager,
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
    void* scene_manager,
    uint64_t stable_key,
    const char* expected_item,
    uint64_t expected_flags);

CdfNativeStoreResult cdf_native_store(
    void* scene_manager,
    uint64_t stable_key,
    const char* expected_item);

int cdf_native_scene_contains_component(
    void* scene_manager,
    const void* component);

int cdf_native_install_enhanced_patches(void);

#ifdef __cplusplus
}
#endif
