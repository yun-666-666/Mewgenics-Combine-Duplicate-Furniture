#pragma once

#include "combine_duplicate_furniture/domain.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace cdf {

struct NativeFailure {
    std::uint32_t seh_code{};
    std::uintptr_t exception_rva{};
};

struct EnhancedPatchAudit {
    bool success{};
    std::uint32_t patched_mask{};
    std::uint32_t repaired_mask{};
    std::uint32_t conflict_mask{};
};

enum class FurnitureUiRebuildStatus : int {
    NotAttempted = 0,
    Armed = 1,
    EnterContextUnavailable = -1,
    LayoutUnavailable = -2,
    ComponentUnavailable = -3,
    ComponentUnreadable = -4,
    ModeActive = -5,
    WriteFailed = -6,
    Exception = -7,
    ComponentInvalid = -8,
    RowCacheUnreadable = -9,
};

struct FurnitureUiRebuildResult {
    FurnitureUiRebuildStatus status{FurnitureUiRebuildStatus::NotAttempted};
    std::size_t rows_scanned{};
    std::size_t rows_invalidated{};
};

class NativeTransactionPort final : public TransactionPort {
public:
    [[nodiscard]] ScanSnapshot Scan() override;
    [[nodiscard]] bool SetRare(
        std::uint64_t stable_key,
        const std::string& item_id,
        std::uint64_t expected_flags) override;
    [[nodiscard]] bool ClearRare(
        std::uint64_t stable_key,
        const std::string& item_id) override;
    [[nodiscard]] bool SetEnhanced(
        std::uint64_t stable_key,
        const std::string& item_id,
        std::uint64_t expected_flags) override;
    [[nodiscard]] bool ClearEnhanced(
        std::uint64_t stable_key,
        const std::string& item_id) override;
    [[nodiscard]] bool Consume(
        std::uint64_t stable_key,
        const std::string& item_id,
        std::uint64_t expected_flags) override;

    [[nodiscard]] bool RuntimeAvailable() const noexcept;
    [[nodiscard]] NativeFailure LastFailure() const noexcept;

private:
    bool runtime_available_{};
    NativeFailure last_failure_{};
};

struct RuntimeResolution {
    std::uintptr_t scene_ready_hook_rva{};
    std::uintptr_t furniture_mode_enter_hook_rva{};
    bool core_available{};
    bool ui_refresh_available{};
};

[[nodiscard]] RuntimeResolution ResolveRuntime() noexcept;
[[nodiscard]] void* FurnitureModeComponent(void* mode_enter_context) noexcept;
[[nodiscard]] bool FurnitureModeActive(void* component) noexcept;
[[nodiscard]] FurnitureUiRebuildResult PrepareFurnitureUiRebuildOnEnter(
    void* mode_enter_context,
    std::span<const std::uint64_t> stale_row_keys) noexcept;
[[nodiscard]] EnhancedPatchAudit EnsureEnhancedFurniturePatches() noexcept;

}  // namespace cdf
