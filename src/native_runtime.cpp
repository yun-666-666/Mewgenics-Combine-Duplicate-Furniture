#include "combine_duplicate_furniture/native_runtime.hpp"

#include "native_api.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <vector>

namespace cdf {

namespace {

std::string FormatStoreProbe(const CdfNativeStoreResult& result) {
    const auto has = [&result](std::uint32_t flag) {
        return (result.probe_flags & flag) != 0U;
    };
    std::ostringstream output;
    output << "probe=0x" << std::hex << result.probe_flags << std::dec
           << " input=" << has(CDF_STORE_PROBE_INPUT_VALID)
           << " signatures=" << has(CDF_STORE_PROBE_SIGNATURES_VALID)
           << " entry=" << has(CDF_STORE_PROBE_ENTRY_FOUND)
           << " piece=" << has(CDF_STORE_PROBE_PIECE_FOUND)
           << " piece_count=" << result.piece_count
           << " count_one=" << has(CDF_STORE_PROBE_PIECE_COUNT_ONE)
           << " entry_flags=0x" << std::hex << result.entry_flags << std::dec
           << " flags_valid=" << has(CDF_STORE_PROBE_FLAGS_VALID)
           << " before_room_read=" << has(CDF_STORE_PROBE_BEFORE_ROOM_READ)
           << " before_room_nonempty="
           << has(CDF_STORE_PROBE_BEFORE_ROOM_NONEMPTY)
           << " before_room=\"" << result.before_room << '"'
           << " before_grid=" << has(CDF_STORE_PROBE_BEFORE_GRID_PRESENT)
           << " before_entry_match="
           << has(CDF_STORE_PROBE_BEFORE_ENTRY_MATCH)
           << " call_completed=" << has(CDF_STORE_PROBE_CALL_COMPLETED)
           << " after_delete_queued="
           << has(CDF_STORE_PROBE_AFTER_DELETE_QUEUED)
           << " after_room_read=" << has(CDF_STORE_PROBE_AFTER_ROOM_READ)
           << " after_room_empty=" << has(CDF_STORE_PROBE_AFTER_ROOM_EMPTY)
           << " after_room=\"" << result.after_room << '"'
           << " after_grid_null=" << has(CDF_STORE_PROBE_AFTER_GRID_NULL)
           << " after_entry_null=" << has(CDF_STORE_PROBE_AFTER_ENTRY_NULL)
           << " after_storage_entry_same="
           << has(CDF_STORE_PROBE_AFTER_STORAGE_ENTRY_SAME)
           << " after_scene_contains="
           << has(CDF_STORE_PROBE_AFTER_SCENE_CONTAINS);
    return output.str();
}

}  // namespace

NativeTransactionPort::NativeTransactionPort(void* scene_manager) noexcept
    : scene_manager_(scene_manager) {}

ScanSnapshot NativeTransactionPort::Scan() {
    std::vector<CdfNativeFurniture> native(4096);
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
            instance.room_id,
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
        scene_manager_, stable_key, item_id.c_str(), expected_flags);
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
    last_store_probe_summary_ = FormatStoreProbe(result);
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

const std::string& NativeTransactionPort::LastStoreProbeSummary() const noexcept {
    return last_store_probe_summary_;
}

bool FurnitureModeActive(void* scene_manager) noexcept {
    return cdf_native_furniture_mode_active(scene_manager) != 0;
}

bool FurnitureModeEnterRefreshSupported() noexcept {
    return cdf_native_furniture_mode_enter_refresh_supported() != 0;
}

FurnitureUiRebuildStatus ArmFurnitureUiRebuildOnEnter(
    void* mode_enter_context) noexcept {
    return static_cast<FurnitureUiRebuildStatus>(
        cdf_native_arm_furniture_ui_rebuild_on_enter(mode_enter_context));
}

bool SceneContainsComponent(
    void* scene_manager,
    const void* component) noexcept {
    return cdf_native_scene_contains_component(scene_manager, component) != 0;
}

bool InstallEnhancedFurniturePatches() noexcept {
    return cdf_native_install_enhanced_patches() != 0;
}

}  // namespace cdf
