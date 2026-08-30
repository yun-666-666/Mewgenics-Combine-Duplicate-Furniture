#include "combine_duplicate_furniture/native_runtime.hpp"

#include "native_api.h"

#include <algorithm>
#include <vector>

namespace cdf {

ScanSnapshot NativeTransactionPort::Scan() {
    std::vector<CdfNativeFurniture> native(4096);
    const auto scanned = cdf_native_scan(native.data(), native.size());
    runtime_available_ = scanned.runtime_available != 0;
    ScanSnapshot result;
    result.complete = scanned.complete != 0 && runtime_available_;
    result.furniture.reserve(scanned.count);
    for (std::size_t index = 0; index < scanned.count; ++index) {
        const auto& instance = native[index];
        result.furniture.push_back({
            instance.stable_key,
            instance.item_id,
            instance.room_id,
            instance.placement_flags});
    }
    return result;
}

bool NativeTransactionPort::SetRare(
    std::uint64_t stable_key,
    const std::string& item_id,
    std::uint64_t expected_flags) {
    const auto result = cdf_native_set_rare(
        stable_key, item_id.c_str(), expected_flags, 1U);
    last_failure_ = {result.seh_code, result.exception_rva};
    return result.success != 0;
}

bool NativeTransactionPort::ClearRare(
    std::uint64_t stable_key,
    const std::string& item_id) {
    const auto snapshot = Scan();
    const auto found = std::ranges::find_if(
        snapshot.furniture,
        [stable_key, &item_id](const auto& instance) {
            return instance.stable_key == stable_key &&
                instance.item_id == item_id;
        });
    if (found == snapshot.furniture.end()) {
        return false;
    }
    const auto result = cdf_native_set_rare(
        stable_key, item_id.c_str(), found->placement_flags, 0U);
    last_failure_ = {result.seh_code, result.exception_rva};
    return result.success != 0;
}

bool NativeTransactionPort::SetEnhanced(
    std::uint64_t stable_key,
    const std::string& item_id,
    std::uint64_t expected_flags) {
    const auto result = cdf_native_set_enhanced(
        stable_key, item_id.c_str(), expected_flags, 1U);
    last_failure_ = {result.seh_code, result.exception_rva};
    return result.success != 0;
}

bool NativeTransactionPort::ClearEnhanced(
    std::uint64_t stable_key,
    const std::string& item_id) {
    const auto snapshot = Scan();
    const auto found = std::ranges::find_if(
        snapshot.furniture,
        [stable_key, &item_id](const auto& instance) {
            return instance.stable_key == stable_key &&
                instance.item_id == item_id;
        });
    if (found == snapshot.furniture.end()) {
        return false;
    }
    const auto result = cdf_native_set_enhanced(
        stable_key, item_id.c_str(), found->placement_flags, 0U);
    last_failure_ = {result.seh_code, result.exception_rva};
    return result.success != 0;
}

bool NativeTransactionPort::Consume(
    std::uint64_t stable_key,
    const std::string& item_id,
    std::uint64_t expected_flags) {
    const auto result = cdf_native_consume(
        stable_key, item_id.c_str(), expected_flags);
    last_failure_ = {result.seh_code, result.exception_rva};
    return result.success != 0;
}

bool NativeTransactionPort::RuntimeAvailable() const noexcept {
    return runtime_available_;
}

NativeFailure NativeTransactionPort::LastFailure() const noexcept {
    return last_failure_;
}

RuntimeResolution ResolveRuntime() noexcept {
    const auto resolution = cdf_native_resolve_runtime();
    return {
        resolution.scene_ready_hook_rva,
        resolution.furniture_mode_enter_hook_rva,
        resolution.core_available != 0,
        resolution.ui_refresh_available != 0};
}

void* FurnitureModeComponent(void* mode_enter_context) noexcept {
    return cdf_native_furniture_mode_component(mode_enter_context);
}

bool FurnitureModeActive(void* component) noexcept {
    return cdf_native_furniture_mode_active(component) != 0;
}

FurnitureUiRebuildResult PrepareFurnitureUiRebuildOnEnter(
    void* mode_enter_context,
    std::span<const std::uint64_t> stale_row_keys) noexcept {
    const auto result = cdf_native_prepare_furniture_ui_rebuild_on_enter(
        mode_enter_context,
        stale_row_keys.data(),
        stale_row_keys.size());
    return {
        static_cast<FurnitureUiRebuildStatus>(result.status),
        result.rows_scanned,
        result.rows_invalidated};
}

EnhancedPatchAudit EnsureEnhancedFurniturePatches() noexcept {
    const auto audit = cdf_native_ensure_enhanced_patches();
    return {
        audit.success != 0,
        audit.patched_mask,
        audit.repaired_mask,
        audit.conflict_mask};
}

}  // namespace cdf
