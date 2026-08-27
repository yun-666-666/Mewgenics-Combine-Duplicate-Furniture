#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace cdf {

enum class PromptAction { None, Previous, Next, Cancel, Confirm };

// Virtual coordinates match the SWF's 1280x720 canvas (including letterboxing).
inline PromptAction PromptHit(double x, double y, double width, double height,
                              bool confirmation) {
    const double scale = std::min(width / 1280.0, height / 720.0);
    if (scale <= 0) return PromptAction::None;
    x = (x - (width - 1280 * scale) / 2) / scale;
    y = (y - (height - 720 * scale) / 2) / scale;
    if (!confirmation) {
        return x >= 830 && x <= 1010 && y >= 410 && y <= 454
            ? PromptAction::Cancel : PromptAction::None;
    }
    if (y < 606 || y > 650) return PromptAction::None;
    if (x >= 218 && x <= 398) return PromptAction::Previous;
    if (x >= 418 && x <= 598) return PromptAction::Next;
    if (x >= 670 && x <= 850) return PromptAction::Cancel;
    if (x >= 870 && x <= 1050) return PromptAction::Confirm;
    return PromptAction::None;
}

inline std::vector<std::string> PromptLines(std::string_view text) {
    std::vector<std::string> lines;
    std::string line;
    std::size_t columns = 0;
    for (std::size_t i = 0; i < text.size();) {
        const auto byte = static_cast<unsigned char>(text[i]);
        if (byte == '\n') {
            lines.push_back(std::move(line));
            line.clear();
            columns = 0;
            ++i;
            continue;
        }
        if (byte == '\r') { ++i; continue; }
        const std::size_t count = byte < 0x80 ? 1 : byte < 0xE0 ? 2 : byte < 0xF0 ? 3 : 4;
        const std::size_t width = byte < 0x80 ? 1 : 2;
        if (columns + width > 72) {
            // Preserve whole English words where possible, without splitting UTF-8.
            const auto space = line.rfind(' ');
            if (byte < 0x80 && byte != ' ' && space != std::string::npos &&
                line.size() - space < 20) {
                const auto tail = line.substr(space + 1);
                line.resize(space);
                lines.push_back(std::move(line));
                line = tail;
                columns = tail.size();
            } else {
                lines.push_back(std::move(line));
                line.clear();
                columns = 0;
            }
        }
        if (byte != ' ' || !line.empty()) {
            line.append(text.substr(i, std::min(count, text.size() - i)));
            columns += width;
        }
        i += count;
    }
    if (!line.empty() || lines.empty()) lines.push_back(std::move(line));
    return lines;
}

struct PromptModel {
    std::vector<std::string> lines;
    std::size_t page{};
    bool confirmation{};
    bool confirm_selected{};

    [[nodiscard]] std::size_t PageSize() const { return confirmation ? 16 : 4; }
    [[nodiscard]] std::size_t PageCount() const {
        return std::max(std::size_t{1}, (lines.size() + PageSize() - 1) / PageSize());
    }
    void TurnPage(int direction) {
        if (direction < 0 && page > 0) --page;
        if (direction > 0 && page + 1 < PageCount()) ++page;
    }
    [[nodiscard]] PromptAction Enter() const {
        return confirmation && confirm_selected ? PromptAction::Confirm : PromptAction::Cancel;
    }
};

} // namespace cdf
