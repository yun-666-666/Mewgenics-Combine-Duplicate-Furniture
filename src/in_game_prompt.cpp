#include "in_game_prompt.hpp"
#include "prompt_native.h"
#include "combine_duplicate_furniture/native_runtime.hpp"

#include <windows.h>
#include <windowsx.h>

#include <chrono>
#include <utility>

namespace cdf {
namespace {
void* g_scene{};
void* g_owner{};
void* g_root{};
HHOOK g_message_hook{};
PromptModel g_model;
PromptAction g_pending{}, g_pressed{};
std::chrono::steady_clock::time_point g_deadline{};
bool g_visible{}, g_english{}, g_dirty{};

bool SetText(const char* name, const std::string& text) {
    return cdf_prompt_text(cdf_prompt_child(g_root, name), text.c_str()) != 0;
}

bool SetFrame(const char* name, int frame) {
    return cdf_prompt_frame(cdf_prompt_child(g_root, name), frame) != 0;
}

bool ShowVisuals(bool confirmation) {
    bool success = SetFrame("edge", 1);
    if (confirmation) {
        success &= SetFrame("prev", 1) && SetFrame("next", 1);
        success &= SetFrame("cancel", 1) && SetFrame("yes", 1);
    } else {
        success &= SetFrame("cancel", 1);
    }
    return success;
}

bool Render() {
    bool success = true;
    for (std::size_t row = 0; row < g_model.PageSize(); ++row) {
        const auto index = g_model.page * g_model.PageSize() + row;
        success &= SetText(("line" + std::to_string(row)).c_str(),
                           index < g_model.lines.size() ? g_model.lines[index] : "");
    }
    if (!g_model.confirmation) {
        return SetText("cancel_txt", g_english ? "Close [Esc]" : "关闭 [Esc]") &&
            success;
    }
    success &= SetText("page", (g_english ? "Page " : "第 ") +
        std::to_string(g_model.page + 1) + " / " + std::to_string(g_model.PageCount()) +
        (g_english ? "    PgUp/PgDn: pages   Tab: select   Enter: activate   Esc: cancel"
                   : " 页    PgUp/PgDn 翻页 · Tab 切换 · Enter 选择 · Esc 取消"));
    success &= SetText("prev_txt", g_model.page > 0 ? (g_english ? "< Previous" : "< 上一页") : "");
    success &= SetText("next_txt", g_model.page + 1 < g_model.PageCount() ? (g_english ? "Next >" : "下一页 >") : "");
    success &= SetText("cancel_txt", g_model.confirm_selected
        ? (g_english ? "Cancel" : "取消") : (g_english ? "[ Cancel ]" : "[ 取消 ]"));
    success &= SetText("yes_txt", g_model.confirm_selected
        ? (g_english ? "[ Combine ]" : "[ 开始合并 ]") : (g_english ? "Combine" : "开始合并"));
    success &= SetFrame("cancel", g_model.confirm_selected ? 1 : 2);
    success &= SetFrame("yes", g_model.confirm_selected ? 2 : 1);
    g_dirty = false;
    return success;
}

PromptAction Hit(const MSG& message) {
    RECT client{};
    if (!GetClientRect(message.hwnd, &client)) return PromptAction::None;
    return PromptHit(GET_X_LPARAM(message.lParam), GET_Y_LPARAM(message.lParam),
                     client.right, client.bottom, g_model.confirmation);
}

LRESULT CALLBACK MessageHook(int code, WPARAM remove, LPARAM pointer) {
    if (code >= 0 && remove == PM_REMOVE && g_visible) {
        auto* message = reinterpret_cast<MSG*>(pointer);
        if (message && message->hwnd) {
            if (message->message == WM_KEYDOWN && !(message->lParam & (1LL << 30))) {
                switch (message->wParam) {
                case VK_ESCAPE: g_pending = PromptAction::Cancel; break;
                case VK_RETURN: g_pending = g_model.Enter(); break;
                case VK_TAB:
                    g_model.confirm_selected = !g_model.confirm_selected;
                    g_dirty = true;
                    break;
                case VK_PRIOR: g_pending = PromptAction::Previous; break;
                case VK_NEXT: g_pending = PromptAction::Next; break;
                }
            } else if (message->message == WM_LBUTTONDOWN) {
                g_pressed = Hit(*message);
            } else if (message->message == WM_LBUTTONUP) {
                const auto action = Hit(*message);
                if (action == g_pressed) g_pending = action;
                g_pressed = PromptAction::None;
            } else if (message->message == WM_MOUSEWHEEL && g_model.confirmation) {
                g_pending = GET_WHEEL_DELTA_WPARAM(message->wParam) > 0
                    ? PromptAction::Previous : PromptAction::Next;
            }
            // Consume input only while our in-game modal is visible. No new HWND is created.
            if ((message->message >= WM_MOUSEFIRST && message->message <= WM_MOUSELAST) ||
                message->message == WM_KEYDOWN || message->message == WM_KEYUP ||
                message->message == WM_CHAR) message->message = WM_NULL;
        }
    }
    return CallNextHookEx(nullptr, code, remove, pointer);
}
} // namespace

bool ShowGamePrompt(void* scene, std::string title, std::string text,
                    bool english, bool confirmation, unsigned timeout_ms) {
    CloseGamePrompt();
    g_scene = scene;
    g_root = cdf_prompt_find(scene, &g_owner);
    if (!g_root || !cdf_prompt_frame(g_root, confirmation ? 1 : 2)) return false;
    if (!ShowVisuals(confirmation)) {
        cdf_prompt_frame(g_root, 0);
        return false;
    }
    g_model = {PromptLines(text), 0, confirmation, false};
    g_english = english;
    g_pending = g_pressed = PromptAction::None;
    g_deadline = timeout_ms ? std::chrono::steady_clock::now() +
        std::chrono::milliseconds(timeout_ms) : std::chrono::steady_clock::time_point{};
    if (!SetText("title", title) || !Render()) {
        cdf_prompt_frame(g_root, 0);
        return false;
    }
    g_message_hook = SetWindowsHookExW(WH_GETMESSAGE, MessageHook, nullptr, GetCurrentThreadId());
    if (!g_message_hook) {
        cdf_prompt_frame(g_root, 0);
        return false;
    }
    g_visible = true;
    cdf_prompt_block_input(1);
    return true;
}

PromptAction TickGamePrompt(void* scene) {
    if (!g_visible) return PromptAction::None;
    if (scene != g_scene || !SceneContainsComponent(scene, g_owner)) {
        g_root = nullptr;  // The old scene owns and destroys its nodes.
        CloseGamePrompt();
        return PromptAction::Cancel;
    }
    const auto action = std::exchange(g_pending, PromptAction::None);
    if (action == PromptAction::Cancel || action == PromptAction::Confirm) {
        CloseGamePrompt();
        return action;
    }
    if (action == PromptAction::Previous || action == PromptAction::Next) {
        g_model.TurnPage(action == PromptAction::Previous ? -1 : 1);
        g_dirty = true;
    }
    if (g_dirty && !Render()) {
        CloseGamePrompt();
        return PromptAction::Cancel;
    }
    if (g_deadline != std::chrono::steady_clock::time_point{} &&
        std::chrono::steady_clock::now() >= g_deadline) CloseGamePrompt();
    return PromptAction::None;
}

bool GamePromptVisible() { return g_visible; }

void CloseGamePrompt() {
    g_visible = false;
    cdf_prompt_block_input(0);
    if (g_message_hook) UnhookWindowsHookEx(std::exchange(g_message_hook, nullptr));
    if (g_root && cdf_prompt_house() == g_scene && SceneContainsComponent(g_scene, g_owner))
        cdf_prompt_frame(g_root, 0);
    g_scene = g_owner = g_root = nullptr;
    g_pending = g_pressed = PromptAction::None;
}

void ShutdownGamePrompt() {
    // DLL detach must not dereference objects during engine teardown.
    g_root = nullptr;
    CloseGamePrompt();
}
} // namespace cdf
