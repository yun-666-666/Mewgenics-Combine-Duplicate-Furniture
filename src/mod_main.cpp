#include "combine_duplicate_furniture/catalog.hpp"
#include "combine_duplicate_furniture/domain.hpp"
#include "combine_duplicate_furniture/native_runtime.hpp"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <regex>
#include <sstream>
#include <string>

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

std::string Number(double value) {
    std::ostringstream output;
    if (value >= 0.0) {
        output << '+';
    }
    output << std::fixed << std::setprecision(2) << value;
    auto text = output.str();
    while (text.size() > 1 && text.back() == '0') {
        text.pop_back();
    }
    if (!text.empty() && text.back() == '.') {
        text.pop_back();
    }
    return text;
}

void AppendAttribute(
    std::ostringstream& output,
    std::string_view name,
    double normal,
    double rare) {
    if (normal == 0.0 && rare == 0.0) {
        return;
    }
    output << name << ": " << Number(normal)
           << " x2 = " << Number(rare) << '\n';
}

std::string Preview(
    const cdf::CombineCandidate& candidate,
    const cdf::FurnitureDefinition& definition) {
    const auto rare = definition.attributes.Rare();
    std::ostringstream output;
    output << definition.display_name << " x2\n"
           << "-> Rare " << definition.display_name << " x1\n\n"
           << "item_id: " << candidate.item_id << '\n';
    AppendAttribute(output, "Comfort", definition.attributes.comfort, rare.comfort);
    AppendAttribute(output, "Stimulation", definition.attributes.stimulation, rare.stimulation);
    AppendAttribute(output, "Health", definition.attributes.health, rare.health);
    AppendAttribute(output, "Evolution", definition.attributes.evolution, rare.evolution);
    AppendAttribute(output, "Appeal", definition.attributes.appeal, rare.appeal);
    output << "\nRare also doubles negative attributes.\n"
           << "Only one duplicate group will be combined.\n\n"
           << "Confirm?";
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
    std::smatch match;
    if (!std::regex_search(
            text, match,
            std::regex(R"cdf("hotkey"\s*:\s*"F([1-9]|1[0-2])")cdf",
                       std::regex::icase))) {
        Log("config hotkey invalid; using F8");
        return;
    }
    const int function_key = std::stoi(match[1].str());
    g_hotkey = VK_F1 + function_key - 1;
    Log("config hotkey=F" + std::to_string(function_key));
}

std::string StatusName(cdf::ExecuteStatus status) {
    switch (status) {
    case cdf::ExecuteStatus::Success:
        return "success";
    case cdf::ExecuteStatus::PreconditionFailed:
        return "precondition_failed";
    case cdf::ExecuteStatus::RareConversionFailed:
        return "rare_conversion_failed";
    case cdf::ExecuteStatus::ConsumeFailed:
        return "consume_failed";
    case cdf::ExecuteStatus::VerificationFailed:
        return "verification_failed";
    }
    return "unknown";
}

void HandleCombine(void* scene_manager) {
    if (g_busy.exchange(true)) {
        return;
    }
    struct BusyReset {
        ~BusyReset() { g_busy = false; }
    } reset;

    if (!EnsureCatalog()) {
        MessageBoxW(
            nullptr,
            Wide("Furniture data could not be loaded:\n" + g_catalog_error).c_str(),
            L"Combine Duplicate Furniture",
            MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
        return;
    }

    cdf::NativeTransactionPort port(scene_manager);
    const auto snapshot = port.Scan();
    std::size_t rare_count{};
    for (const auto& item : snapshot.furniture) {
        rare_count += item.IsRare() ? 1U : 0U;
    }
    const auto candidates = cdf::FindCandidates(snapshot, g_catalog);
    {
        std::ostringstream message;
        message << "scan total=" << snapshot.furniture.size()
                << " normal=" << snapshot.furniture.size() - rare_count
                << " rare=" << rare_count
                << " candidates=" << candidates.size()
                << " complete=" << snapshot.complete;
        Log(message.str());
    }
    if (!snapshot.complete || !port.SignaturesValid()) {
        MessageBoxW(
            nullptr,
            L"The current game build or furniture state did not pass the native checks. No furniture was changed.",
            L"Combine Duplicate Furniture",
            MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
        return;
    }
    if (candidates.empty()) {
        MessageBoxW(
            nullptr,
            L"No pair of identical ordinary furniture can be combined.",
            L"Combine Duplicate Furniture",
            MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
        return;
    }

    const auto& candidate = candidates.front();
    const auto definition = g_catalog.find(candidate.item_id);
    if (definition == g_catalog.end()) {
        return;
    }
    {
        std::ostringstream message;
        message << "preview item=" << candidate.item_id
                << " keep_key=" << candidate.keep_key
                << " consume_key=" << candidate.consume_key
                << " normal_attributes="
                << Number(definition->second.attributes.comfort) << ','
                << Number(definition->second.attributes.stimulation) << ','
                << Number(definition->second.attributes.health) << ','
                << Number(definition->second.attributes.evolution) << ','
                << Number(definition->second.attributes.appeal);
        Log(message.str());
    }
    const auto choice = MessageBoxW(
        nullptr,
        Wide(Preview(candidate, definition->second)).c_str(),
        L"Combine Duplicate Furniture",
        MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2 | MB_SETFOREGROUND);
    if (choice != IDYES) {
        Log("confirmation cancelled item=" + candidate.item_id);
        return;
    }

    const auto result = cdf::ExecuteCandidate(candidate, port);
    const auto failure = port.LastFailure();
    {
        std::ostringstream message;
        const auto rare = definition->second.attributes.Rare();
        message << "execute status=" << StatusName(result.status)
                << " stage=" << result.stage
                << " item=" << candidate.item_id
                << " keep_key=" << candidate.keep_key
                << " consume_key=" << candidate.consume_key
                << " before=" << result.before_count
                << " after=" << result.after_count
                << " rare_target="
                << Number(rare.comfort) << ','
                << Number(rare.stimulation) << ','
                << Number(rare.health) << ','
                << Number(rare.evolution) << ','
                << Number(rare.appeal)
                << " rollback_attempted=" << result.rare_rollback_attempted
                << " rollback_succeeded=" << result.rare_rollback_succeeded
                << " seh=0x" << std::hex << failure.seh_code
                << " exception_rva=0x" << failure.exception_rva;
        Log(message.str());
    }

    if (result.status == cdf::ExecuteStatus::Success) {
        MessageBoxW(
            nullptr,
            Wide("Combined one duplicate pair successfully:\n" +
                 definition->second.display_name +
                 "\n\nThe kept instance now has the native Rare flag.").c_str(),
            L"Combine Duplicate Furniture",
            MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
    } else {
        MessageBoxW(
            nullptr,
            Wide("Combine failed at stage: " + result.stage +
                 "\nNo further group was processed. Check the independent mod log.").c_str(),
            L"Combine Duplicate Furniture",
            MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    }
}

void __fastcall SceneReadyHook(void* scene_manager) {
    if (g_next_scene_ready) {
        g_next_scene_ready(scene_manager);
    }
    if (!g_enabled || !scene_manager ||
        !cdf::FurnitureModeActive(scene_manager)) {
        return;
    }
    EnsureConfig();
    if ((GetAsyncKeyState(g_hotkey) & 1) != 0) {
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
