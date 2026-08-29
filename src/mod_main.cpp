#include "combine_duplicate_furniture/catalog.hpp"
#include "combine_duplicate_furniture/domain.hpp"
#include "combine_duplicate_furniture/native_runtime.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <new>
#include <regex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr auto kOwner = "CombineDuplicateFurniture";
constexpr UINT_PTR kSceneReadyUpdateRva = 0x962820;
constexpr int kSceneReadyStolenBytes = 15;

using InstallHookFn = int (__cdecl*)(
    UINT_PTR, int, void*, void**, int, const char*);
using MewLogFn = void (__cdecl*)(const char*, const char*, ...);
using GetVersionFn = int (__cdecl*)();
using SceneReadyUpdateFn = void (__fastcall*)(void*);

HMODULE g_module{};
SceneReadyUpdateFn g_next_scene_ready{};
MewLogFn g_mew_log{};
std::atomic<bool> g_enabled{false};
std::atomic<bool> g_busy{false};
std::mutex g_log_mutex;
cdf::FurnitureCatalog g_catalog;
bool g_catalog_attempted{};
std::string g_catalog_error;
bool g_config_loaded{};
int g_hotkey{VK_F8};
bool g_show_dialogs{};
bool g_hotkey_down{};
bool g_combine_requested{};
std::chrono::steady_clock::time_point g_combine_request_deadline{};

enum class UiLanguage {
    Chinese,
    English,
};

UiLanguage g_language{UiLanguage::Chinese};

bool EnglishUi() {
    return g_language == UiLanguage::English;
}

const wchar_t* DialogTitle() {
    return EnglishUi()
        ? L"Combine Duplicate Furniture"
        : L"批量合并重复家具";
}

std::string Localized(const char* chinese, const char* english) {
    return EnglishUi() ? english : chinese;
}

struct BatchState {
    enum class Stage {
        AwaitingFurnitureExit,
        ConfirmingFurnitureExit,
        Executing,
    };

    std::vector<cdf::CombineCandidate> candidates;
    std::size_t next_index{};
    std::vector<void*> pending_components;
    Stage stage{Stage::AwaitingFurnitureExit};
};

BatchState g_batch;

struct TransientMessage {
    HWND owner{};
    std::wstring text;
    UINT icon{};
    DWORD timeout_ms{};
};

std::filesystem::path ModulePath() {
    std::wstring buffer(32768, L'\0');
    const auto length = GetModuleFileNameW(
        g_module, buffer.data(), static_cast<DWORD>(buffer.size()));
    buffer.resize(length);
    return buffer;
}

std::filesystem::path GameRoot() {
    std::wstring buffer(32768, L'\0');
    const auto length = GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
}

std::filesystem::path DataRoot() {
    const auto module = ModulePath();
    return module.parent_path() / module.stem();
}

std::wstring Wide(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const auto count = MultiByteToWideChar(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
        nullptr, 0);
    if (count <= 0) {
        return std::wstring(text.begin(), text.end());
    }
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
        result.data(), count);
    return result;
}

BOOL CALLBACK FindGameWindowCallback(HWND window, LPARAM parameter) {
    DWORD process_id{};
    GetWindowThreadProcessId(window, &process_id);
    if (process_id != GetCurrentProcessId() || !IsWindowVisible(window) ||
        GetWindow(window, GW_OWNER) != nullptr) {
        return TRUE;
    }
    *reinterpret_cast<HWND*>(parameter) = window;
    return FALSE;
}

HWND GameWindow() {
    const auto foreground = GetForegroundWindow();
    DWORD process_id{};
    if (foreground) {
        GetWindowThreadProcessId(foreground, &process_id);
        if (process_id == GetCurrentProcessId()) {
            return foreground;
        }
    }
    HWND window{};
    EnumWindows(FindGameWindowCallback, reinterpret_cast<LPARAM>(&window));
    return window;
}

void Log(std::string_view message) {
    if (g_mew_log) {
        g_mew_log(kOwner, "%s", std::string(message).c_str());
    }
    std::scoped_lock lock(g_log_mutex);
    std::error_code error;
    const auto root = DataRoot();
    std::filesystem::create_directories(root / L"logs", error);
    std::ofstream stream(
        root / L"logs" / L"CombineDuplicateFurniture.log",
        std::ios::app);
    if (!stream) {
        return;
    }
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_s(&local, &time);
    stream << std::put_time(&local, "%Y-%m-%d %H:%M:%S")
           << " " << message << '\n';
}

DWORD WINAPI TransientMessageThread(void* parameter) {
    auto* message = static_cast<TransientMessage*>(parameter);
    using MessageBoxTimeoutFn = int (WINAPI*)(
        HWND, LPCWSTR, LPCWSTR, UINT, WORD, DWORD);
    const auto user32 = GetModuleHandleW(L"user32.dll");
    const auto show = user32
        ? reinterpret_cast<MessageBoxTimeoutFn>(
              GetProcAddress(user32, "MessageBoxTimeoutW"))
        : nullptr;
    if (show) {
        show(
            message->owner,
            message->text.c_str(),
            DialogTitle(),
            MB_OK | message->icon | MB_TOPMOST | MB_SETFOREGROUND,
            0,
            message->timeout_ms);
    } else {
        MessageBeep(message->icon);
    }
    delete message;
    return 0;
}

void ShowTransient(
    std::string text,
    UINT icon = MB_ICONINFORMATION,
    DWORD timeout_ms = 2500U) {
    auto* message = new (std::nothrow) TransientMessage{
        GameWindow(), Wide(text), icon, timeout_ms};
    if (!message) {
        return;
    }
    const auto thread = CreateThread(
        nullptr, 0, TransientMessageThread, message, 0, nullptr);
    if (!thread) {
        delete message;
        return;
    }
    CloseHandle(thread);
}

void Notify(
    std::string text,
    UINT icon = MB_ICONINFORMATION,
    DWORD timeout_ms = 2500U) {
    if (g_show_dialogs) {
        ShowTransient(std::move(text), icon, timeout_ms);
    }
}

std::string BatchPreview(
    const std::vector<cdf::CombineCandidate>& candidates) {
    std::map<std::string, std::size_t> pair_counts;
    std::size_t placed_pairs{};
    std::size_t ordinary_pairs{};
    std::size_t rare_pairs{};
    for (const auto& candidate : candidates) {
        ++pair_counts[candidate.item_id];
        placed_pairs += candidate.consume_placed ? 1U : 0U;
        candidate.promote_to_rare ? ++ordinary_pairs : ++rare_pairs;
    }

    std::ostringstream output;
    if (EnglishUi()) {
        output << "Found " << candidates.size()
               << " duplicate furniture pairs that can be combined.\n\n";
        if (ordinary_pairs != 0U) {
            output << "- " << ordinary_pairs
                   << " ordinary pairs: keep one native Rare item per pair.\n";
        }
        if (rare_pairs != 0U) {
            output << "- " << rare_pairs
                   << " Rare pairs: keep one enhanced Rare item with 4x base values per pair.\n";
        }
        output << "The batch will consume " << candidates.size()
               << " duplicate items.\n";
        if (placed_pairs != 0U) {
            output << placed_pairs
                   << " pairs require recalling the material item from a room first.\n";
        }
    } else {
        output << "发现 " << candidates.size() << " 组可合并的重复家具。\n\n";
        if (ordinary_pairs != 0U) {
            output << "- " << ordinary_pairs
                   << " 组普通同款：每组升级并保留 1 件原生 Rare。\n";
        }
        if (rare_pairs != 0U) {
            output << "- " << rare_pairs
                   << " 组 Rare 同款：每组保留 1 件强化 Rare（基础数值 4 倍），清掉 1 件重复件。\n";
        }
        output << "合计将消耗 " << candidates.size() << " 件重复家具。\n";
        if (placed_pairs != 0U) {
            output << "其中 " << placed_pairs
                   << " 组需要先把材料家具从房间收回家具栏。\n";
        }
    }
    output << '\n';

    std::size_t shown{};
    constexpr std::size_t kMaxShownTypes = 12;
    for (const auto& [item_id, count] : pair_counts) {
        if (shown == kMaxShownTypes) {
            if (EnglishUi()) {
                output << "...and " << pair_counts.size() - shown
                       << " more furniture types\n";
            } else {
                output << "……另有 " << pair_counts.size() - shown
                       << " 种家具\n";
            }
            break;
        }
        const auto definition = g_catalog.find(item_id);
        output << "- "
               << (definition != g_catalog.end()
                       ? definition->second.display_name
                       : item_id)
               << " × " << count
               << (EnglishUi() ? " pairs\n" : " 组\n");
        ++shown;
    }

    if (EnglishUi()) {
        output << "\nThe enhanced Rare marker is stored on the kept item and remains 4x after saving and reloading.\n"
               << "After confirming, leave furniture mode. The batch starts only after the furniture screen is fully closed.\n"
               << "Rare multipliers also increase negative attributes.\n\n"
               << "Special furniture that the game marks as unable to become Rare, such as the Food Box, is skipped.\n\n"
               << "Start batch combine?";
    } else {
        output << "\n强化 Rare 会在保留实例上写入持久标记，保存并重开后仍为基础数值 4 倍。\n"
               << "确认后请退出家具界面；只有家具界面完全关闭后才会开始合并。\n"
               << "Rare 也会使负面属性翻倍。\n\n"
               << "游戏标记为不可 Rare 的特殊家具（例如食物箱）不会参与。\n\n"
               << "开始批量合并？";
    }
    return output.str();
}

bool EnsureCatalog() {
    if (g_catalog_attempted) {
        return !g_catalog.empty();
    }
    g_catalog_attempted = true;
    if (!cdf::LoadFurnitureCatalog(
            GameRoot() / L"resources.gpak", g_catalog, g_catalog_error)) {
        Log("catalog load failed: " + g_catalog_error);
        return false;
    }
    Log("catalog loaded definitions=" + std::to_string(g_catalog.size()));
    return true;
}

void EnsureConfig() {
    if (g_config_loaded) {
        return;
    }
    g_config_loaded = true;
    std::ifstream stream(DataRoot() / L"config.json");
    if (!stream) {
        Log("config unavailable; using F8");
        return;
    }
    const std::string text{
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>()};
    std::smatch hotkey_match;
    if (std::regex_search(
            text, hotkey_match,
            std::regex(R"cdf("hotkey"\s*:\s*"F([1-9]|1[0-2])")cdf",
                       std::regex::icase))) {
        const int function_key = std::stoi(hotkey_match[1].str());
        g_hotkey = VK_F1 + function_key - 1;
        Log("config hotkey=F" + std::to_string(function_key));
    } else {
        Log("config hotkey invalid; using F8");
    }

    std::smatch language_match;
    if (std::regex_search(
            text, language_match,
            std::regex(
                R"cdf("language"\s*:\s*"(zh-CN|en-US)")cdf",
                std::regex::icase))) {
        const auto language = language_match[1].str();
        g_language = !language.empty() &&
                (language.front() == 'e' || language.front() == 'E')
            ? UiLanguage::English
            : UiLanguage::Chinese;
    }
    Log(std::string("config language=") +
        (EnglishUi() ? "en-US" : "zh-CN"));

    std::smatch dialogs_match;
    if (std::regex_search(
            text, dialogs_match,
            std::regex(R"cdf("show_dialogs"\s*:\s*(true|false))cdf",
                       std::regex::icase))) {
        const auto value = dialogs_match[1].str();
        g_show_dialogs = !value.empty() &&
            (value.front() == 't' || value.front() == 'T');
    }
    Log(std::string("config dialogs=") +
        (g_show_dialogs ? "external" : "disabled"));
}

void ClearBatch() {
    g_batch = {};
    g_busy = false;
}

void StopBatch(
    std::string_view stage,
    const cdf::CombineCandidate& candidate,
    const cdf::NativeFailure& failure,
    bool rollback_attempted = false,
    bool rollback_succeeded = false) {
    std::ostringstream message;
    message << "batch stopped stage=" << stage
            << " completed=" << g_batch.next_index
            << " planned=" << g_batch.candidates.size()
            << " item=" << candidate.item_id
            << " keep_key=" << candidate.keep_key
            << " consume_key=" << candidate.consume_key
            << " rollback_attempted=" << rollback_attempted
            << " rollback_succeeded=" << rollback_succeeded
            << " seh=0x" << std::hex << failure.seh_code
            << " exception_rva=0x" << failure.exception_rva;
    Log(message.str());
    if (EnglishUi()) {
        Notify(
            "Batch combine stopped. Completed " +
                std::to_string(g_batch.next_index) + " / " +
                std::to_string(g_batch.candidates.size()) +
                " pairs. See the mod log for details.",
            MB_ICONERROR,
            4000U);
    } else {
        Notify(
            "批量合并已停止。已完成 " +
                std::to_string(g_batch.next_index) + " / " +
                std::to_string(g_batch.candidates.size()) +
                " 组。详情请查看 MOD 日志。",
            MB_ICONERROR,
            4000U);
    }
    ClearBatch();
}

void FinishBatch() {
    const auto completed = g_batch.next_index;
    std::size_t ordinary_pairs{};
    std::size_t rare_pairs{};
    for (const auto& candidate : g_batch.candidates) {
        candidate.promote_to_rare ? ++ordinary_pairs : ++rare_pairs;
    }
    Log("batch complete pairs=" + std::to_string(completed));
    if (EnglishUi()) {
        Notify(
            "Batch combine complete: " + std::to_string(completed) +
                " duplicate pairs combined (ordinary " +
                std::to_string(ordinary_pairs) + ", Rare " +
                std::to_string(rare_pairs) +
                "). Re-enter furniture mode to view the updated items.",
            MB_ICONINFORMATION,
            2500U);
    } else {
        Notify(
            "批量合并完成：已合并 " + std::to_string(completed) +
                " 组重复家具（普通 " + std::to_string(ordinary_pairs) +
                "，Rare " + std::to_string(rare_pairs) +
                "）。重新进入家具界面即可查看更新后的家具。",
            MB_ICONINFORMATION,
            2500U);
    }
    ClearBatch();
}

void ProcessBatch(void* scene_manager) {
    if (!g_busy) {
        return;
    }

    if (!g_batch.pending_components.empty()) {
        std::erase_if(
            g_batch.pending_components,
            [scene_manager](const void* component) {
                return !cdf::SceneContainsComponent(
                    scene_manager, component);
            });
        if (!g_batch.pending_components.empty()) {
            return;
        }
        Log("room furniture pre-store complete");
        return;
    }

    const bool furniture_mode_active =
        cdf::FurnitureModeActive(scene_manager);
    if (g_batch.stage == BatchState::Stage::AwaitingFurnitureExit) {
        if (furniture_mode_active) {
            return;
        }
        g_batch.stage = BatchState::Stage::ConfirmingFurnitureExit;
        Log("furniture mode exit observed; validating on next frame");
        return;
    }
    if (g_batch.stage == BatchState::Stage::ConfirmingFurnitureExit) {
        if (furniture_mode_active) {
            g_batch.stage = BatchState::Stage::AwaitingFurnitureExit;
            return;
        }

        cdf::NativeTransactionPort port(scene_manager);
        const auto snapshot = port.Scan();
        const auto refreshed = cdf::FindCandidates(snapshot, g_catalog);
        const bool matches = snapshot.complete && port.SignaturesValid() &&
            cdf::RemainingCandidatesMatch(
                g_batch.candidates, g_batch.next_index, refreshed);
        Log("batch exit validation completed=" +
            std::to_string(g_batch.next_index) + " planned=" +
            std::to_string(g_batch.candidates.size()) + " refreshed_pairs=" +
            std::to_string(refreshed.size()) + " total=" +
            std::to_string(snapshot.furniture.size()) + " complete=" +
            std::to_string(snapshot.complete) + " matches=" +
            std::to_string(matches));
        if (!matches) {
            Notify(
                Localized(
                    "退出家具界面后家具状态发生变化，批量合并已停止；尚未执行的家具未被修改。",
                    "The furniture state changed after leaving furniture mode. The batch stopped without modifying the remaining pairs."),
                MB_ICONERROR,
                4000U);
            ClearBatch();
            return;
        }
        g_batch.stage = BatchState::Stage::Executing;
        Log("batch execution armed outside furniture mode");
        return;
    }
    if (furniture_mode_active) {
        g_batch.stage = BatchState::Stage::AwaitingFurnitureExit;
        Log("batch paused because furniture mode reopened");
        return;
    }

    if (g_batch.next_index >= g_batch.candidates.size()) {
        FinishBatch();
        return;
    }

    const auto candidate = g_batch.candidates[g_batch.next_index];
    cdf::NativeTransactionPort port(scene_manager);
    if (candidate.promote_to_rare && !port.SetRare(
            candidate.keep_key,
            candidate.item_id,
            candidate.keep_flags)) {
        StopBatch("set_rare", candidate, port.LastFailure());
        return;
    }
    if (candidate.promote_to_enhanced && !port.SetEnhanced(
            candidate.keep_key,
            candidate.item_id,
            candidate.keep_flags)) {
        StopBatch("set_enhanced", candidate, port.LastFailure());
        return;
    }

    if (!port.Consume(
            candidate.consume_key,
            candidate.item_id,
            candidate.consume_flags)) {
        const auto failure = port.LastFailure();
        bool rollback{};
        if (candidate.promote_to_rare) {
            rollback = port.ClearRare(
                candidate.keep_key, candidate.item_id);
        } else if (candidate.promote_to_enhanced) {
            rollback = port.ClearEnhanced(
                candidate.keep_key, candidate.item_id);
        }
        StopBatch(
            "consume",
            candidate,
            failure,
            candidate.promote_to_rare || candidate.promote_to_enhanced,
            rollback);
        return;
    }

    ++g_batch.next_index;
    {
        std::ostringstream message;
        message << "batch pair complete index=" << g_batch.next_index
                << '/' << g_batch.candidates.size()
                << " item=" << candidate.item_id
                << " keep_key=" << candidate.keep_key
                << " consume_key=" << candidate.consume_key
                << " kind="
                << (candidate.promote_to_rare ? "ordinary_to_rare" :
                                                "rare_to_enhanced")
                << " stored_from_room=" << candidate.consume_placed;
        Log(message.str());
    }

    if (g_batch.next_index == g_batch.candidates.size()) {
        FinishBatch();
    }
}

void HandleCombine(void* scene_manager) {
    if (g_busy.exchange(true)) {
        return;
    }

    if (!EnsureCatalog()) {
        Notify(
            Localized(
                "家具数据读取失败：\n",
                "Failed to read furniture data:\n") +
                g_catalog_error,
            MB_ICONERROR,
            4000U);
        ClearBatch();
        return;
    }

    cdf::NativeTransactionPort port(scene_manager);
    const auto snapshot = port.Scan();
    std::size_t rare_count{};
    for (const auto& item : snapshot.furniture) {
        rare_count += item.IsRare() ? 1U : 0U;
    }
    auto candidates = cdf::FindCandidates(snapshot, g_catalog);
    std::size_t ordinary_candidates{};
    std::size_t rare_candidates{};
    for (const auto& candidate : candidates) {
        candidate.promote_to_rare ? ++ordinary_candidates : ++rare_candidates;
    }
    {
        std::ostringstream message;
        message << "scan total=" << snapshot.furniture.size()
                << " normal=" << snapshot.furniture.size() - rare_count
                << " rare=" << rare_count
                << " candidates=" << candidates.size()
                << " ordinary_candidates=" << ordinary_candidates
                << " rare_candidates=" << rare_candidates
                << " complete=" << snapshot.complete;
        Log(message.str());
    }
    if (!snapshot.complete || !port.SignaturesValid()) {
        Notify(
            Localized(
                "当前家具状态无法读取，未修改任何家具。",
                "The current furniture state could not be read. No furniture was changed."),
            MB_ICONERROR,
            3500U);
        ClearBatch();
        return;
    }
    if (candidates.empty()) {
        Notify(
            Localized(
                "没有可合并的相同普通或 Rare 家具。",
                "No matching ordinary or Rare furniture can be combined."),
            MB_ICONINFORMATION);
        ClearBatch();
        return;
    }

    Log("batch preview pairs=" + std::to_string(candidates.size()));
    if (g_show_dialogs) {
        const auto choice = MessageBoxW(
            GameWindow(),
            Wide(BatchPreview(candidates)).c_str(),
            DialogTitle(),
            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2 |
                MB_SETFOREGROUND);
        if (choice != IDYES) {
            Log("batch confirmation cancelled pairs=" +
                std::to_string(candidates.size()));
            ClearBatch();
            return;
        }
    } else {
        Log("batch confirmation skipped dialogs=disabled pairs=" +
            std::to_string(candidates.size()));
    }

    const auto preview_pairs = candidates.size();
    const auto refreshed_snapshot = port.Scan();
    candidates = cdf::FindCandidates(refreshed_snapshot, g_catalog);
    Log("batch refreshed preview_pairs=" +
        std::to_string(preview_pairs) + " refreshed_pairs=" +
        std::to_string(candidates.size()) + " total=" +
        std::to_string(refreshed_snapshot.furniture.size()) +
        " complete=" + std::to_string(refreshed_snapshot.complete));
    if (!refreshed_snapshot.complete || !port.SignaturesValid()) {
        Notify(
            Localized(
                "确认后家具状态无法重新读取，未修改任何家具。",
                "The furniture state could not be read again after confirmation. No furniture was changed."),
            MB_ICONERROR,
            3500U);
        ClearBatch();
        return;
    }
    if (candidates.empty()) {
        Notify(
            Localized(
                "确认后家具状态已变化，目前没有可合并的家具。",
                "The furniture state changed after confirmation, and there are no longer any pairs to combine."),
            MB_ICONINFORMATION,
            3000U);
        ClearBatch();
        return;
    }

    g_batch.candidates = std::move(candidates);
    g_batch.next_index = 0;
    g_batch.stage = BatchState::Stage::AwaitingFurnitureExit;
    Log("batch sealed pairs=" +
        std::to_string(g_batch.candidates.size()) +
        "; waiting for furniture mode exit");

    std::size_t room_materials{};
    for (const auto& candidate : g_batch.candidates) {
        if (!candidate.consume_placed) {
            continue;
        }
        const bool stored = port.Store(
            candidate.consume_key, candidate.item_id);
        Log("room furniture pre-store probe item=" + candidate.item_id +
            " consume_key=" + std::to_string(candidate.consume_key) +
            " success=" + std::to_string(stored) + " " +
            port.LastStoreProbeSummary());
        if (!stored) {
            StopBatch("pre_store", candidate, port.LastFailure());
            return;
        }
        g_batch.pending_components.push_back(
            port.LastStoredComponent());
        ++room_materials;
    }
    if (room_materials != 0U) {
        Log("room furniture pre-store queued count=" +
            std::to_string(room_materials));
    }
}

void __fastcall SceneReadyHook(void* scene_manager) {
    if (g_next_scene_ready) {
        g_next_scene_ready(scene_manager);
    }
    if (!g_enabled || !scene_manager) {
        return;
    }
    EnsureConfig();
    const auto now = std::chrono::steady_clock::now();
    const bool hotkey_down =
        (GetAsyncKeyState(g_hotkey) & 0x8000) != 0;
    if (hotkey_down && !g_hotkey_down && !g_busy) {
        g_combine_requested = true;
        g_combine_request_deadline = now + std::chrono::seconds(1);
        Log("hotkey press captured");
    } else if (!g_busy && g_combine_requested &&
               now >= g_combine_request_deadline) {
        g_combine_requested = false;
        Log("hotkey request expired before furniture mode");
    }
    g_hotkey_down = hotkey_down;
    if (g_busy) {
        ProcessBatch(scene_manager);
        return;
    }
    if (!g_combine_requested) {
        return;
    }
    const bool furniture_mode_active =
        cdf::FurnitureModeActive(scene_manager);
    if (!furniture_mode_active) {
        return;
    }
    if (g_combine_requested) {
        g_combine_requested = false;
        HandleCombine(scene_manager);
    }
}

bool ResolveAndHook() {
    const auto mewjector = GetModuleHandleA("version.dll");
    if (!mewjector) {
        return false;
    }
    const auto get_version = reinterpret_cast<GetVersionFn>(
        GetProcAddress(mewjector, "MJ_GetVersion"));
    const auto install_hook = reinterpret_cast<InstallHookFn>(
        GetProcAddress(mewjector, "MJ_InstallHook"));
    g_mew_log = reinterpret_cast<MewLogFn>(
        GetProcAddress(mewjector, "MJ_Log"));
    if (!get_version || get_version() < 3 || !install_hook) {
        return false;
    }
    if (!cdf::InstallEnhancedFurniturePatches()) {
        if (g_mew_log) {
            g_mew_log(kOwner, "Enhanced furniture patch signatures do not match");
        }
        return false;
    }
    void* trampoline{};
    if (!install_hook(
            kSceneReadyUpdateRva,
            kSceneReadyStolenBytes,
            reinterpret_cast<void*>(&SceneReadyHook),
            &trampoline,
            40,
            kOwner)) {
        return false;
    }
    g_next_scene_ready = reinterpret_cast<SceneReadyUpdateFn>(trampoline);
    g_enabled = true;
    if (g_mew_log) {
        g_mew_log(kOwner, "Loaded v%s; furniture-mode hotkey is F8", CDF_VERSION);
    }
    return true;
}

}  // namespace

extern "C" __declspec(dllexport)
int CombineDuplicateFurniture_Initialize() {
    if (g_enabled) {
        return 1;
    }
    return ResolveAndHook() ? 1 : 0;
}

extern "C" __declspec(dllexport)
void CombineDuplicateFurniture_Shutdown() {
    g_enabled = false;
}

BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);
        CombineDuplicateFurniture_Initialize();
    } else if (reason == DLL_PROCESS_DETACH) {
        CombineDuplicateFurniture_Shutdown();
    }
    return TRUE;
}
