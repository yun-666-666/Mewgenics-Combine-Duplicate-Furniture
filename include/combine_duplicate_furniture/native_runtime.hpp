#pragma once

#include "combine_duplicate_furniture/domain.hpp"

#include <cstdint>
#include <string>

namespace cdf {

struct NativeFailure {
    std::uint32_t seh_code{};
    std::uintptr_t exception_rva{};
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
[[nodiscard]] void* FindFurnitureUi(void* scene_manager) noexcept;
[[nodiscard]] bool RequestFurnitureUiRefresh(void* furniture_ui) noexcept;
[[nodiscard]] bool SceneContainsComponent(
    void* scene_manager,
    const void* component) noexcept;
[[nodiscard]] bool InstallEnhancedFurniturePatches() noexcept;

}  // namespace cdf
