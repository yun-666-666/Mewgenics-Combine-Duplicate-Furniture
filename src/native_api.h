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
    void* piece;
    uint32_t runtime_match_count;
    uint8_t delete_pending;
    char item_id[CDF_NATIVE_TEXT_CAPACITY];
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

CdfNativeMutationResult cdf_native_consume(
    void* scene_manager,
    uint64_t stable_key,
    const char* expected_item);

CdfNativeMutationResult cdf_native_store(
    void* scene_manager,
    uint64_t stable_key,
    const char* expected_item);

int cdf_native_scene_contains_component(
    void* scene_manager,
    const void* component);

#ifdef __cplusplus
}
#endif
