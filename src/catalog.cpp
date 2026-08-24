#include "combine_duplicate_furniture/catalog.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cdf {
namespace {

template<class T>
bool Read(std::ifstream& stream, T& value) {
    return static_cast<bool>(stream.read(
        reinterpret_cast<char*>(&value), sizeof(value)));
}

struct GpakEntry {
    std::string name;
    std::uint32_t size{};
    std::uint64_t offset{};
};

bool ReadGpakTargets(
    const std::filesystem::path& path,
    const std::unordered_set<std::string>& targets,
    std::unordered_map<std::string, std::vector<std::byte>>& output,
    std::string& error) {
    std::ifstream stream(path, std::ios::binary);
    std::uint32_t count{};
    if (!stream || !Read(stream, count) || count > 1'000'000U) {
        error = "resources.gpak directory is unavailable";
        return false;
    }

    std::vector<GpakEntry> entries;
    entries.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        std::uint16_t length{};
        GpakEntry entry;
        if (!Read(stream, length) || length == 0 || length > 4096) {
            error = "resources.gpak directory name is invalid";
            return false;
        }
        entry.name.resize(length);
        if (!stream.read(entry.name.data(), length) ||
            !Read(stream, entry.size)) {
            error = "resources.gpak directory is truncated";
            return false;
        }
        entries.push_back(std::move(entry));
    }

    std::uint64_t offset = static_cast<std::uint64_t>(stream.tellg());
    for (auto& entry : entries) {
        entry.offset = offset;
        offset += entry.size;
    }

    for (const auto& entry : entries) {
        if (!targets.contains(entry.name)) {
            continue;
        }
        if (entry.size > 64U * 1024U * 1024U) {
            error = entry.name + " is unexpectedly large";
            return false;
        }
        std::vector<std::byte> bytes(entry.size);
        stream.clear();
        stream.seekg(static_cast<std::streamoff>(entry.offset));
        if (!stream || (entry.size != 0 && !stream.read(
                reinterpret_cast<char*>(bytes.data()), entry.size))) {
            error = entry.name + " is truncated";
            return false;
        }
        output.emplace(entry.name, std::move(bytes));
    }

    for (const auto& target : targets) {
        if (!output.contains(target)) {
            error = target + " is missing";
            return false;
        }
    }
    return true;
}

std::string_view AsText(const std::vector<std::byte>& bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

bool ParseNumber(std::string_view text, double& value) {
    std::string owned(text);
    char* end{};
    value = std::strtod(owned.c_str(), &end);
    return end != owned.c_str() && *end == '\0';
}

void SetAttribute(
    RoomAttributes& attributes,
    std::string_view key,
    double value) {
    if (key == "Comfort") {
        attributes.comfort = value;
    } else if (key == "Stimulation") {
        attributes.stimulation = value;
    } else if (key == "Health") {
        attributes.health = value;
    } else if (key == "Evolution") {
        attributes.evolution = value;
    } else if (key == "Appeal") {
        attributes.appeal = value;
    }
}

std::string Trim(std::string_view text) {
    while (!text.empty() &&
           std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() &&
           std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    return std::string(text);
}

void ParseFurnitureBlock(
    std::string_view item_id,
    std::string_view block,
    FurnitureCatalog& catalog) {
    FurnitureDefinition definition;
    definition.item_id = std::string(item_id);
    definition.display_name = definition.item_id;
    std::istringstream lines{std::string(block)};
    for (std::string line; std::getline(lines, line);) {
        if (const auto comment = line.find("//");
            comment != std::string::npos) {
            line.erase(comment);
        }
        std::istringstream fields(line);
        std::string key;
        std::string raw;
        if (!(fields >> key >> raw)) {
            continue;
        }
        if (key == "name") {
            definition.display_name = raw;
        } else if (key == "can_be_rare") {
            definition.can_be_rare = raw != "false";
        } else {
            double value{};
            if (ParseNumber(raw, value)) {
                SetAttribute(definition.attributes, key, value);
            }
        }
    }
    catalog.insert_or_assign(definition.item_id, std::move(definition));
}

bool ParseFurnitureEffects(
    std::string_view text,
    FurnitureCatalog& catalog) {
    std::size_t cursor{};
    while (cursor < text.size()) {
        const auto brace = text.find('{', cursor);
        if (brace == std::string_view::npos) {
            break;
        }
        auto id_end = brace;
        while (id_end > 0 &&
               std::isspace(static_cast<unsigned char>(text[id_end - 1]))) {
            --id_end;
        }
        auto id_start = id_end;
        while (id_start > 0) {
            const auto ch = static_cast<unsigned char>(text[id_start - 1]);
            if (!std::isalnum(ch) && ch != '_') {
                break;
            }
            --id_start;
        }
        if (id_start == id_end) {
            cursor = brace + 1;
            continue;
        }
        std::size_t end = brace + 1;
        int depth = 1;
        while (end < text.size() && depth > 0) {
            depth += text[end] == '{' ? 1 : 0;
            depth -= text[end] == '}' ? 1 : 0;
            ++end;
        }
        if (depth != 0) {
            return false;
        }
        ParseFurnitureBlock(
            text.substr(id_start, id_end - id_start),
            text.substr(brace + 1, end - brace - 2),
            catalog);
        cursor = end;
    }
    return !catalog.empty();
}

template<class T>
bool ReadBytes(
    const std::vector<std::byte>& bytes,
    std::size_t& offset,
    T& value) {
    if (offset > bytes.size() || bytes.size() - offset < sizeof(value)) {
        return false;
    }
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    offset += sizeof(value);
    return true;
}

bool ParseFurnitureInfo(
    const std::vector<std::byte>& bytes,
    std::unordered_set<std::string>& item_ids) {
    constexpr std::size_t kPayloadSize = 580;
    std::size_t offset{};
    std::uint32_t version{};
    std::uint32_t count{};
    if (!ReadBytes(bytes, offset, version) || version != 1 ||
        !ReadBytes(bytes, offset, count) || count > 100'000U) {
        return false;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        std::uint32_t length{};
        std::uint32_t unknown{};
        if (!ReadBytes(bytes, offset, length) ||
            !ReadBytes(bytes, offset, unknown) || length == 0 ||
            offset > bytes.size() || bytes.size() - offset < length ||
            bytes.size() - offset - length < kPayloadSize) {
            return false;
        }
        std::string item_id(
            reinterpret_cast<const char*>(bytes.data() + offset), length);
        offset += length + kPayloadSize;
        if (!item_ids.insert(std::move(item_id)).second) {
            return false;
        }
    }
    return offset == bytes.size();
}

std::vector<std::string> ParseCsvFields(std::string_view line, int limit) {
    std::vector<std::string> fields;
    std::string current;
    bool quoted{};
    for (std::size_t index = 0; index < line.size(); ++index) {
        const char ch = line[index];
        if (ch == '"') {
            if (quoted && index + 1 < line.size() && line[index + 1] == '"') {
                current.push_back('"');
                ++index;
            } else {
                quoted = !quoted;
            }
        } else if (ch == ',' && !quoted) {
            fields.push_back(std::move(current));
            current.clear();
            if (static_cast<int>(fields.size()) >= limit) {
                return fields;
            }
        } else {
            current.push_back(ch);
        }
    }
    fields.push_back(std::move(current));
    return fields;
}

std::unordered_map<std::string, std::string> ParseEnglishNames(
    std::string_view csv) {
    std::unordered_map<std::string, std::string> result;
    std::size_t cursor{};
    while (cursor < csv.size()) {
        const auto end = csv.find_first_of("\r\n", cursor);
        const auto line = csv.substr(
            cursor, end == std::string_view::npos ? csv.size() - cursor
                                                  : end - cursor);
        const auto fields = ParseCsvFields(line, 2);
        if (fields.size() >= 2 && !fields[0].empty() && !fields[1].empty()) {
            result.try_emplace(fields[0], fields[1]);
        }
        if (end == std::string_view::npos) {
            break;
        }
        cursor = end + 1;
        if (cursor < csv.size() && csv[end] == '\r' && csv[cursor] == '\n') {
            ++cursor;
        }
    }
    return result;
}

}  // namespace

bool LoadFurnitureCatalog(
    const std::filesystem::path& resources_gpak,
    FurnitureCatalog& catalog,
    std::string& error) {
    static const std::unordered_set<std::string> targets{
        "data/furniture_effects.gon",
        "data/furniture_effects_guide.gon",
        "data/furniture_info.data",
        "data/text/combined.csv"};
    std::unordered_map<std::string, std::vector<std::byte>> files;
    if (!ReadGpakTargets(resources_gpak, targets, files, error)) {
        return false;
    }
    if (files.at("data/furniture_effects_guide.gon").empty()) {
        error = "furniture_effects_guide.gon is empty";
        return false;
    }

    FurnitureCatalog parsed;
    if (!ParseFurnitureEffects(
            AsText(files.at("data/furniture_effects.gon")), parsed)) {
        error = "furniture_effects.gon could not be parsed";
        return false;
    }
    std::unordered_set<std::string> info_ids;
    if (!ParseFurnitureInfo(
            files.at("data/furniture_info.data"), info_ids)) {
        error = "furniture_info.data could not be parsed";
        return false;
    }
    const auto names = ParseEnglishNames(
        AsText(files.at("data/text/combined.csv")));
    for (auto iterator = parsed.begin(); iterator != parsed.end();) {
        if (!info_ids.contains(iterator->first)) {
            iterator = parsed.erase(iterator);
            continue;
        }
        if (const auto name = names.find(iterator->second.display_name);
            name != names.end()) {
            iterator->second.display_name = name->second;
        } else {
            iterator->second.display_name = iterator->second.item_id;
        }
        ++iterator;
    }
    if (parsed.empty()) {
        error = "no furniture definitions matched furniture_info.data";
        return false;
    }
    catalog = std::move(parsed);
    return true;
}

}  // namespace cdf
