#include "combine_duplicate_furniture/native_runtime.hpp"

#include "native_api.h"

#include <algorithm>
#include <array>

namespace cdf {

NativeTransactionPort::NativeTransactionPort(void* scene_manager) noexcept
    : scene_manager_(scene_manager) {}

ScanSnapshot NativeTransactionPort::Scan() {
    std::array<CdfNativeFurniture, 4096> native{};
    const auto scanned = cdf_native_scan(
        scene_manager_, native.data(), native.size());
    signatures_valid_ = scanned.signatures_valid != 0;
    ScanSnapshot result;
    result.complete = scanned.complete != 0 && signatures_valid_;
    result.furniture.reserve(scanned.count);
    for (std::size_t index = 0; index < scanned.count; ++index) {
        const auto& instance = native[index];
        result.furniture.push_back({
            instance.stable_key,
            instance.item_id,
            instance.placement_flags,
            instance.delete_pending != 0,
            instance.runtime_match_count});
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

bool NativeTransactionPort::Consume(
    std::uint64_t stable_key,
    const std::string& item_id) {
    const auto result = cdf_native_consume(
        scene_manager_, stable_key, item_id.c_str());
    last_failure_ = {result.seh_code, result.exception_rva};
    return result.success != 0;
}

bool NativeTransactionPort::Store(
    std::uint64_t stable_key,
    const std::string& item_id) {
    const auto result = cdf_native_store(
        scene_manager_, stable_key, item_id.c_str());
    last_failure_ = {result.seh_code, result.exception_rva};
    last_stored_component_ = result.pending_component;
    return result.success != 0;
}

bool NativeTransactionPort::SignaturesValid() const noexcept {
    return signatures_valid_;
}

NativeFailure NativeTransactionPort::LastFailure() const noexcept {
    return last_failure_;
}

void* NativeTransactionPort::LastStoredComponent() const noexcept {
    return last_stored_component_;
}

bool FurnitureModeActive(void* scene_manager) noexcept {
    return cdf_native_furniture_mode_active(scene_manager) != 0;
}

bool SceneContainsComponent(
    void* scene_manager,
    const void* component) noexcept {
    return cdf_native_scene_contains_component(scene_manager, component) != 0;
}

}  // namespace cdf
