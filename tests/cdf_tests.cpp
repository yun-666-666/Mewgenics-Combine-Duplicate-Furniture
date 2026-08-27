#include "combine_duplicate_furniture/domain.hpp"
#include "combine_duplicate_furniture/prompt_model.hpp"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace {

using cdf::CombineCandidate;
using cdf::ExecuteStatus;
using cdf::FurnitureCatalog;
using cdf::FurnitureDefinition;
using cdf::FurnitureInstance;
using cdf::RoomAttributes;
using cdf::ScanSnapshot;

int g_failures{};

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::cerr << __FUNCTION__ << ':' << __LINE__                    \
                      << " check failed: " #condition "\n";                \
            ++g_failures;                                                    \
        }                                                                    \
    } while (false)

FurnitureCatalog Catalog(bool can_be_rare = true) {
    FurnitureCatalog catalog;
    catalog.emplace("chair", FurnitureDefinition{
        "chair", "Chair", can_be_rare, {1, 0, 0, 0, 0}});
    catalog.emplace("table", FurnitureDefinition{
        "table", "Table", true, {0, 1, 0, 0, 0}});
    catalog.emplace("set_90s_frige", FurnitureDefinition{
        "set_90s_frige", "90s Fridge", true, {1, 1, 0, 0, 0}});
    return catalog;
}

FurnitureInstance Item(
    std::uint64_t key,
    std::string id = "chair",
    std::uint64_t flags = 0,
    bool pending = false,
    std::uint32_t matches = 0,
    std::string room = {}) {
    if (matches == 1 && room.empty()) {
        room = "TestRoom";
    }
    return {
        key,
        std::move(id),
        std::move(room),
        flags,
        pending,
        matches};
}

ScanSnapshot Snapshot(std::vector<FurnitureInstance> items) {
    return {std::move(items), true};
}

class FakePort final : public cdf::TransactionPort {
public:
    ScanSnapshot current;
    bool set_rare_ok{true};
    bool consume_ok{true};
    bool clear_rare_ok{true};
    bool set_enhanced_ok{true};
    bool clear_enhanced_ok{true};
    int set_rare_calls{};
    int set_enhanced_calls{};
    int consume_calls{};

    ScanSnapshot Scan() override { return current; }

    bool SetRare(
        std::uint64_t key,
        const std::string& id,
        std::uint64_t expected_flags) override {
        ++set_rare_calls;
        if (!set_rare_ok) {
            return false;
        }
        auto found = Find(key, id);
        if (found == current.furniture.end() ||
            found->placement_flags != expected_flags) {
            return false;
        }
        found->placement_flags |= cdf::kRareFlag;
        return true;
    }

    bool ClearRare(
        std::uint64_t key,
        const std::string& id) override {
        if (!clear_rare_ok) {
            return false;
        }
        auto found = Find(key, id);
        if (found == current.furniture.end()) {
            return false;
        }
        found->placement_flags &= ~cdf::kRareFlag;
        return true;
    }

    bool SetEnhanced(
        std::uint64_t key,
        const std::string& id,
        std::uint64_t expected_flags) override {
        ++set_enhanced_calls;
        if (!set_enhanced_ok) {
            return false;
        }
        auto found = Find(key, id);
        if (found == current.furniture.end() ||
            found->placement_flags != expected_flags ||
            !found->IsRare()) {
            return false;
        }
        found->placement_flags |= cdf::kEnhancedFlag;
        return true;
    }

    bool ClearEnhanced(
        std::uint64_t key,
        const std::string& id) override {
        if (!clear_enhanced_ok) {
            return false;
        }
        auto found = Find(key, id);
        if (found == current.furniture.end()) {
            return false;
        }
        found->placement_flags &= ~cdf::kEnhancedFlag;
        return true;
    }

    bool Consume(
        std::uint64_t key,
        const std::string& id,
        std::uint64_t expected_flags) override {
        ++consume_calls;
        if (!consume_ok) {
            return false;
        }
        const auto found = Find(key, id);
        if (found == current.furniture.end() ||
            found->placement_flags != expected_flags) {
            return false;
        }
        current.furniture.erase(found);
        return true;
    }

private:
    auto Find(std::uint64_t key, const std::string& id) {
        return std::ranges::find_if(
            current.furniture,
            [key, &id](const auto& item) {
                return item.stable_key == key && item.item_id == id;
            });
    }
};

CombineCandidate FirstCandidate(
    const ScanSnapshot& snapshot,
    const FurnitureCatalog& catalog) {
    const auto candidates = cdf::FindCandidates(snapshot, catalog);
    CHECK(!candidates.empty());
    return candidates.empty() ? CombineCandidate{} : candidates.front();
}

void SameOrdinaryCreatesCandidate() {
    const auto candidates = cdf::FindCandidates(
        Snapshot({Item(2), Item(1)}), Catalog());
    CHECK(candidates.size() == 1);
    CHECK(candidates[0].keep_key == 1);
    CHECK(candidates[0].consume_key == 2);
}

void MultipleOrdinaryCreatesAllPairs() {
    const auto candidates = cdf::FindCandidates(
        Snapshot({Item(5), Item(1), Item(4), Item(2), Item(3)}), Catalog());
    CHECK(candidates.size() == 2);
    CHECK(candidates[0].keep_key == 1);
    CHECK(candidates[0].consume_key == 2);
    CHECK(candidates[1].keep_key == 3);
    CHECK(candidates[1].consume_key == 4);
}

void StoredMaterialIsPreferred() {
    const auto candidates = cdf::FindCandidates(
        Snapshot({Item(8, "chair", 0, false, 1), Item(3)}), Catalog());
    CHECK(candidates.size() == 1);
    CHECK(candidates[0].keep_key == 8);
    CHECK(candidates[0].consume_key == 3);
    CHECK(!candidates[0].consume_placed);
}

void TwoPlacedRequireStoreConfirmation() {
    const auto candidates = cdf::FindCandidates(
        Snapshot({
            Item(8, "chair", 0, false, 1),
            Item(3, "chair", 0, false, 1)}),
        Catalog());
    CHECK(candidates.size() == 1);
    CHECK(candidates[0].consume_placed);
}

void UnavailablePlacedIsNeverConsumed() {
    const auto candidates = cdf::FindCandidates(
        Snapshot({
            Item(8, "chair", 0, false, 0, "Floor2_Large"),
            Item(3, "chair", 0, false, 1)}),
        Catalog());
    CHECK(candidates.size() == 1);
    CHECK(candidates[0].keep_key == 8);
    CHECK(candidates[0].consume_key == 3);
    CHECK(candidates[0].consume_placed);
}

void TwoUnavailablePlacedAreSkipped() {
    CHECK(cdf::FindCandidates(
        Snapshot({
            Item(8, "chair", 0, false, 0, "Floor2_Large"),
            Item(3, "chair", 0, false, 0, "Floor2_Large")}),
        Catalog()).empty());
}

void StoredConsumeStillHasPriority() {
    const auto candidates = cdf::FindCandidates(
        Snapshot({
            Item(8, "chair", 0, false, 0, "Floor2_Large"),
            Item(5, "chair", 0, false, 1),
            Item(3)}),
        Catalog());
    CHECK(candidates.size() == 1);
    CHECK(candidates[0].consume_key == 3);
    CHECK(!candidates[0].consume_placed);
}

void DifferentItemsDoNotCombine() {
    CHECK(cdf::FindCandidates(
        Snapshot({Item(1, "chair"), Item(2, "table")}),
        Catalog()).empty());
}

void OrdinaryAndRareDoNotCombine() {
    CHECK(cdf::FindCandidates(
        Snapshot({Item(1), Item(2, "chair", cdf::kRareFlag)}),
        Catalog()).empty());
}

void TwoRareCreateConsolidationCandidate() {
    const auto candidates = cdf::FindCandidates(
        Snapshot({
            Item(1, "chair", cdf::kRareFlag),
            Item(2, "chair", cdf::kRareFlag)}),
        Catalog());
    CHECK(candidates.size() == 1);
    CHECK(candidates[0].keep_key == 1);
    CHECK(candidates[0].consume_key == 2);
    CHECK(!candidates[0].promote_to_rare);
    CHECK(candidates[0].promote_to_enhanced);
}

void CanBeRareFalseDoesNotCombine() {
    CHECK(cdf::FindCandidates(
        Snapshot({Item(1), Item(2)}), Catalog(false)).empty());
}

void EnhancedFurnitureDoesNotCombineAgain() {
    CHECK(cdf::FindCandidates(
        Snapshot({
            Item(1, "chair", cdf::kRareFlag | cdf::kEnhancedFlag),
            Item(2, "chair", cdf::kRareFlag)}),
        Catalog()).empty());
}

void UnknownPlacementFlagDoesNotCombine() {
    CHECK(cdf::FindCandidates(
        Snapshot({Item(1), Item(2, "chair", 0x8)}),
        Catalog()).empty());
}

void CandidateOrderIsDeterministic() {
    const auto candidates = cdf::FindCandidates(
        Snapshot({Item(9, "table"), Item(8, "table"),
                  Item(4, "chair", 0, false, 1), Item(7, "chair")}),
        Catalog());
    CHECK(candidates.size() == 2);
    CHECK(candidates[0].item_id == "chair");
    CHECK(candidates[0].keep_key == 4);
    CHECK(candidates[0].consume_key == 7);
    CHECK(candidates[1].item_id == "table");
}

void FridgePreviewAttributesDouble() {
    const auto catalog = Catalog();
    const auto rare = catalog.at("set_90s_frige").attributes.Rare();
    CHECK(rare.comfort == 2);
    CHECK(rare.stimulation == 2);
}

void NegativeAttributesDouble() {
    const RoomAttributes normal{-2, 1, -3, 0, -0.5};
    const auto rare = normal.Rare();
    CHECK(rare.comfort == -4);
    CHECK(rare.health == -6);
    CHECK(rare.appeal == -1);
}

void OneExecutionConsumesOnlyOneGroup() {
    auto snapshot = Snapshot({Item(1), Item(2), Item(3), Item(4)});
    auto candidate = FirstCandidate(snapshot, Catalog());
    FakePort port;
    port.current = snapshot;
    const auto result = cdf::ExecuteCandidate(candidate, port);
    CHECK(result.status == ExecuteStatus::Success);
    CHECK(port.consume_calls == 1);
    CHECK(port.current.furniture.size() == 3);
}

void RareExecutionConsumesDuplicateWithoutConversion() {
    const auto snapshot = Snapshot({
        Item(1, "chair", cdf::kRareFlag),
        Item(2, "chair", cdf::kRareFlag)});
    auto candidate = FirstCandidate(snapshot, Catalog());
    FakePort port;
    port.current = snapshot;
    const auto result = cdf::ExecuteCandidate(candidate, port);
    CHECK(result.status == ExecuteStatus::Success);
    CHECK(port.set_rare_calls == 0);
    CHECK(port.set_enhanced_calls == 1);
    CHECK(port.consume_calls == 1);
    CHECK(port.current.furniture.size() == 1);
    CHECK(port.current.furniture[0].IsRare());
    CHECK(port.current.furniture[0].IsEnhanced());
}

void StableKeyChangeCancels() {
    const auto sealed = Snapshot({Item(1), Item(2)});
    auto candidate = FirstCandidate(sealed, Catalog());
    FakePort port;
    port.current = Snapshot({Item(1), Item(3)});
    const auto result = cdf::ExecuteCandidate(candidate, port);
    CHECK(result.status == ExecuteStatus::PreconditionFailed);
    CHECK(port.set_rare_calls == 0);
    CHECK(port.consume_calls == 0);
}

void ConsumeFailureIsNotSuccess() {
    const auto snapshot = Snapshot({Item(1), Item(2)});
    auto candidate = FirstCandidate(snapshot, Catalog());
    FakePort port;
    port.current = snapshot;
    port.consume_ok = false;
    const auto result = cdf::ExecuteCandidate(candidate, port);
    CHECK(result.status == ExecuteStatus::ConsumeFailed);
    CHECK(result.rare_rollback_attempted);
    CHECK(result.rare_rollback_succeeded);
    CHECK(!port.current.furniture[0].IsRare());
}

void RareConsumeFailureRollsBackEnhancement() {
    const auto snapshot = Snapshot({
        Item(1, "chair", cdf::kRareFlag),
        Item(2, "chair", cdf::kRareFlag)});
    auto candidate = FirstCandidate(snapshot, Catalog());
    FakePort port;
    port.current = snapshot;
    port.consume_ok = false;
    const auto result = cdf::ExecuteCandidate(candidate, port);
    CHECK(result.status == ExecuteStatus::ConsumeFailed);
    CHECK(result.enhanced_rollback_attempted);
    CHECK(result.enhanced_rollback_succeeded);
    CHECK(port.current.furniture.size() == 2);
    CHECK(std::ranges::all_of(
        port.current.furniture,
        [](const auto& item) { return item.IsRare(); }));
    CHECK(std::ranges::none_of(
        port.current.furniture,
        [](const auto& item) { return item.IsEnhanced(); }));
}

void EnhancedConversionFailureLeavesBothRare() {
    const auto snapshot = Snapshot({
        Item(1, "chair", cdf::kRareFlag),
        Item(2, "chair", cdf::kRareFlag)});
    auto candidate = FirstCandidate(snapshot, Catalog());
    FakePort port;
    port.current = snapshot;
    port.set_enhanced_ok = false;
    const auto result = cdf::ExecuteCandidate(candidate, port);
    CHECK(result.status == ExecuteStatus::EnhancedConversionFailed);
    CHECK(port.current.furniture.size() == 2);
    CHECK(std::ranges::all_of(
        port.current.furniture,
        [](const auto& item) { return item.IsRare(); }));
    CHECK(std::ranges::none_of(
        port.current.furniture,
        [](const auto& item) { return item.IsEnhanced(); }));
}

void RareConversionFailureLeavesBothOrdinary() {
    const auto snapshot = Snapshot({Item(1), Item(2)});
    auto candidate = FirstCandidate(snapshot, Catalog());
    FakePort port;
    port.current = snapshot;
    port.set_rare_ok = false;
    const auto result = cdf::ExecuteCandidate(candidate, port);
    CHECK(result.status == ExecuteStatus::RareConversionFailed);
    CHECK(port.current.furniture.size() == 2);
    CHECK(std::ranges::none_of(
        port.current.furniture,
        [](const auto& item) { return item.IsRare(); }));
}

void DeletePendingIsNotCandidate() {
    CHECK(cdf::FindCandidates(
        Snapshot({Item(1), Item(2, "chair", 0, true)}),
        Catalog()).empty());
}

void DuplicateStableKeysAreRejected() {
    const auto snapshot = Snapshot({Item(1), Item(1)});
    CHECK(cdf::HasDuplicateStableKeys(snapshot.furniture));
    CHECK(cdf::FindCandidates(snapshot, Catalog()).empty());
}

void InGamePromptPaginationAndInput() {
    using cdf::PromptAction;
    cdf::PromptModel prompt{cdf::PromptLines("确认合并？\nNo changes until confirmation."), 0, true, false};
    CHECK(prompt.Enter() == PromptAction::Cancel);
    prompt.confirm_selected = true;
    CHECK(prompt.Enter() == PromptAction::Confirm);
    prompt.lines.assign(33, "家具 Furniture");
    CHECK(prompt.PageCount() == 3);
    prompt.TurnPage(-1);
    CHECK(prompt.page == 0);
    prompt.TurnPage(1);
    prompt.TurnPage(1);
    prompt.TurnPage(1);
    CHECK(prompt.page == 2);
    CHECK(cdf::PromptHit(950, 625, 1280, 720, true) == PromptAction::Confirm);
    CHECK(cdf::PromptHit(1425, 937.5, 1920, 1080, true) == PromptAction::Confirm);
    CHECK(cdf::PromptHit(990, 685, 1360, 840, true) == PromptAction::Confirm);
    CHECK(cdf::PromptHit(760, 625, 1280, 720, true) == PromptAction::Cancel);
    CHECK(cdf::PromptHit(10, 10, 1280, 720, true) == PromptAction::None);
    CHECK(cdf::PromptHit(900, 430, 1280, 720, false) == PromptAction::Cancel);
    CHECK(cdf::PromptHit(950, 625, 1280, 720, false) == PromptAction::None);
    CHECK(cdf::PromptHit(950, 625, 0, 0, true) == PromptAction::None);
    std::string chinese;
    for (int i = 0; i < 80; ++i) chinese += "家";
    const auto wrapped = cdf::PromptLines(chinese);
    CHECK(wrapped.size() == 3);
    CHECK(wrapped[0] + wrapped[1] + wrapped[2] == chinese);
    CHECK(wrapped[0].size() == 36 * 3);
}

}  // namespace

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests{
        {"same ordinary", SameOrdinaryCreatesCandidate},
        {"all ordinary pairs", MultipleOrdinaryCreatesAllPairs},
        {"stored material preferred", StoredMaterialIsPreferred},
        {"placed material confirmation", TwoPlacedRequireStoreConfirmation},
        {"unavailable placed kept", UnavailablePlacedIsNeverConsumed},
        {"two unavailable placed skipped", TwoUnavailablePlacedAreSkipped},
        {"stored consume priority", StoredConsumeStillHasPriority},
        {"different item", DifferentItemsDoNotCombine},
        {"ordinary rare", OrdinaryAndRareDoNotCombine},
        {"two rare", TwoRareCreateConsolidationCandidate},
        {"can_be_rare false", CanBeRareFalseDoesNotCombine},
        {"enhanced not recombined", EnhancedFurnitureDoesNotCombineAgain},
        {"unknown flags", UnknownPlacementFlagDoesNotCombine},
        {"deterministic order", CandidateOrderIsDeterministic},
        {"fridge preview", FridgePreviewAttributesDouble},
        {"negative attributes", NegativeAttributesDouble},
        {"one group", OneExecutionConsumesOnlyOneGroup},
        {"rare group", RareExecutionConsumesDuplicateWithoutConversion},
        {"stable key change", StableKeyChangeCancels},
        {"consume failure", ConsumeFailureIsNotSuccess},
        {"rare consume failure", RareConsumeFailureRollsBackEnhancement},
        {"enhanced failure", EnhancedConversionFailureLeavesBothRare},
        {"rare failure", RareConversionFailureLeavesBothOrdinary},
        {"delete pending", DeletePendingIsNotCandidate},
        {"duplicate stable key", DuplicateStableKeysAreRejected},
        {"in-game prompt", InGamePromptPaginationAndInput}};
    for (const auto& [name, test] : tests) {
        test();
        if (g_failures == 0) {
            std::cout << "PASS " << name << '\n';
        }
    }
    if (g_failures != 0) {
        std::cerr << g_failures << " focused checks failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All " << tests.size() << " focused checks passed\n";
    return EXIT_SUCCESS;
}
