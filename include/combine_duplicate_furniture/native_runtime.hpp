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
    SignatureMismatch = -2,
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
    explicit NativeTransactionPort(void* scene_manager) noexcept;

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
    [[nodiscard]] bool Store(
        std::uint64_t stable_key,
        const std::string& item_id);

    [[nodiscard]] bool SignaturesValid() const noexcept;
    [[nodiscard]] NativeFailure LastFailure() const noexcept;
    [[nodiscard]] void* LastStoredComponent() const noexcept;
    [[nodiscard]] const std::string& LastStoreProbeSummary() const noexcept;

private:
    void* scene_manager_{};
    bool signatures_valid_{};
    NativeFailure last_failure_{};
    void* last_stored_component_{};
    std::string last_store_probe_summary_;
};

[[nodiscard]] bool FurnitureModeActive(void* scene_manager) noexcept;
[[nodiscard]] bool FurnitureModeEnterRefreshSupported() noexcept;
[[nodiscard]] FurnitureUiRebuildResult PrepareFurnitureUiRebuildOnEnter(
    void* mode_enter_context,
    std::span<const std::uint64_t> stale_row_keys) noexcept;
[[nodiscard]] bool SceneContainsComponent(
    void* scene_manager,
    const void* component) noexcept;
[[nodiscard]] EnhancedPatchAudit EnsureEnhancedFurniturePatches() noexcept;

}  // namespace cdf
