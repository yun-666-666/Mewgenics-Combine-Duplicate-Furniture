#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace cdf {

inline constexpr std::uint64_t kRareFlag = 0x2;

struct RoomAttributes {
    double comfort{};
    double stimulation{};
    double health{};
    double evolution{};
    double appeal{};

    [[nodiscard]] RoomAttributes Rare() const noexcept;
    bool operator==(const RoomAttributes&) const = default;
};

struct FurnitureDefinition {
    std::string item_id;
    std::string display_name;
    bool can_be_rare{true};
    RoomAttributes attributes;
};

using FurnitureCatalog =
    std::unordered_map<std::string, FurnitureDefinition>;

struct FurnitureInstance {
    std::uint64_t stable_key{};
    std::string item_id;
    std::string room_id;
    std::uint64_t placement_flags{};
    bool delete_pending{};
    std::uint32_t runtime_match_count{};

    [[nodiscard]] bool IsRare() const noexcept {
        return (placement_flags & kRareFlag) != 0;
    }

    [[nodiscard]] bool HasOnlyKnownFlags() const noexcept {
        return (placement_flags & ~kRareFlag) == 0;
    }
};

struct ScanSnapshot {
    std::vector<FurnitureInstance> furniture;
    bool complete{};
};

struct CombineCandidate {
    std::string item_id;
    std::uint64_t keep_key{};
    std::uint64_t consume_key{};
    std::uint64_t keep_flags{};
    std::uint64_t consume_flags{};
    std::size_t scanned_count{};
    bool consume_placed{};
};

[[nodiscard]] bool HasDuplicateStableKeys(
    const std::vector<FurnitureInstance>& furniture);

[[nodiscard]] std::vector<CombineCandidate> FindCandidates(
    const ScanSnapshot& snapshot,
    const FurnitureCatalog& catalog);

enum class ExecuteStatus {
    Success,
    PreconditionFailed,
    RareConversionFailed,
    ConsumeFailed,
    VerificationFailed
};

struct ExecuteResult {
    ExecuteStatus status{ExecuteStatus::PreconditionFailed};
    std::string stage;
    std::size_t before_count{};
    std::size_t after_count{};
    bool rare_rollback_attempted{};
    bool rare_rollback_succeeded{};
};

class TransactionPort {
public:
    virtual ~TransactionPort() = default;
    [[nodiscard]] virtual ScanSnapshot Scan() = 0;
    [[nodiscard]] virtual bool SetRare(
        std::uint64_t stable_key,
        const std::string& item_id,
        std::uint64_t expected_flags) = 0;
    [[nodiscard]] virtual bool ClearRare(
        std::uint64_t stable_key,
        const std::string& item_id) = 0;
    [[nodiscard]] virtual bool Consume(
        std::uint64_t stable_key,
        const std::string& item_id) = 0;
};

[[nodiscard]] ExecuteResult ExecuteCandidate(
    const CombineCandidate& candidate,
    TransactionPort& port);

}  // namespace cdf
