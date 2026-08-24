#include "combine_duplicate_furniture/domain.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace cdf {

RoomAttributes RoomAttributes::Rare() const noexcept {
    return {
        comfort * 2.0,
        stimulation * 2.0,
        health * 2.0,
        evolution * 2.0,
        appeal * 2.0};
}

bool HasDuplicateStableKeys(
    const std::vector<FurnitureInstance>& furniture) {
    std::set<std::uint64_t> keys;
    for (const auto& instance : furniture) {
        if (instance.stable_key == 0 ||
            !keys.insert(instance.stable_key).second) {
            return true;
        }
    }
    return false;
}

std::vector<CombineCandidate> FindCandidates(
    const ScanSnapshot& snapshot,
    const FurnitureCatalog& catalog) {
    std::vector<CombineCandidate> result;
    if (!snapshot.complete || HasDuplicateStableKeys(snapshot.furniture)) {
        return result;
    }

    std::map<std::string, std::vector<const FurnitureInstance*>> by_item;
    for (const auto& instance : snapshot.furniture) {
        const auto definition = catalog.find(instance.item_id);
        if (instance.stable_key == 0 || instance.delete_pending ||
            instance.IsRare() || !instance.HasOnlyKnownFlags() ||
            instance.runtime_match_count > 1 ||
            definition == catalog.end() ||
            !definition->second.can_be_rare) {
            continue;
        }
        by_item[instance.item_id].push_back(&instance);
    }

    for (auto& [item_id, instances] : by_item) {
        std::ranges::sort(instances, [](const auto* left, const auto* right) {
            return left->stable_key < right->stable_key;
        });
        for (std::size_t index = 0; index + 1 < instances.size(); index += 2) {
            const auto* keep = instances[index];
            const auto* consume = instances[index + 1];
            result.push_back({
                item_id,
                keep->stable_key,
                consume->stable_key,
                keep->placement_flags,
                consume->placement_flags,
                snapshot.furniture.size()});
        }
    }
    return result;
}

namespace {

const FurnitureInstance* FindUnique(
    const ScanSnapshot& snapshot,
    std::uint64_t stable_key) {
    const FurnitureInstance* found{};
    for (const auto& instance : snapshot.furniture) {
        if (instance.stable_key != stable_key) {
            continue;
        }
        if (found) {
            return nullptr;
        }
        found = &instance;
    }
    return found;
}

bool MatchesSealed(
    const FurnitureInstance* instance,
    std::string_view item_id,
    std::uint64_t flags) {
    return instance && !instance->delete_pending &&
        instance->runtime_match_count <= 1 &&
        instance->item_id == item_id &&
        instance->placement_flags == flags &&
        !instance->IsRare() && instance->HasOnlyKnownFlags();
}

}  // namespace

ExecuteResult ExecuteCandidate(
    const CombineCandidate& candidate,
    TransactionPort& port) {
    ExecuteResult result;
    const auto before = port.Scan();
    result.before_count = before.furniture.size();
    if (!before.complete || HasDuplicateStableKeys(before.furniture) ||
        before.furniture.size() != candidate.scanned_count) {
        result.stage = "pre_scan";
        return result;
    }
    const auto* keep = FindUnique(before, candidate.keep_key);
    const auto* consume = FindUnique(before, candidate.consume_key);
    if (candidate.keep_key == candidate.consume_key ||
        !MatchesSealed(keep, candidate.item_id, candidate.keep_flags) ||
        !MatchesSealed(
            consume, candidate.item_id, candidate.consume_flags)) {
        result.stage = "sealed_pair";
        return result;
    }

    if (!port.SetRare(
            candidate.keep_key,
            candidate.item_id,
            candidate.keep_flags)) {
        result.status = ExecuteStatus::RareConversionFailed;
        result.stage = "set_rare";
        return result;
    }

    if (!port.Consume(candidate.consume_key, candidate.item_id)) {
        result.status = ExecuteStatus::ConsumeFailed;
        result.stage = "consume";
        result.rare_rollback_attempted = true;
        result.rare_rollback_succeeded =
            port.ClearRare(candidate.keep_key, candidate.item_id);
        return result;
    }

    const auto after = port.Scan();
    result.after_count = after.furniture.size();
    const auto* kept = FindUnique(after, candidate.keep_key);
    const auto* consumed = FindUnique(after, candidate.consume_key);
    if (!after.complete || HasDuplicateStableKeys(after.furniture) ||
        after.furniture.size() + 1 != before.furniture.size() ||
        !kept || kept->item_id != candidate.item_id ||
        !kept->IsRare() || consumed != nullptr) {
        result.status = ExecuteStatus::VerificationFailed;
        result.stage = "post_scan";
        return result;
    }

    result.status = ExecuteStatus::Success;
    result.stage = "complete";
    return result;
}

}  // namespace cdf
