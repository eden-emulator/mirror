// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <optional>
#include <span>
#include <utility>
#include "core/memory/cheat_parser.h"

namespace Core::Memory {
namespace {

struct TextEntry {
    std::string name;
    std::vector<u32> opcodes;
};

std::string_view Trim(std::string_view text) {
    const auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) {
        return {};
    }
    return text.substr(begin, text.find_last_not_of(" \t\r\n") - begin + 1);
}

std::optional<std::string_view> SectionName(std::string_view name, std::string_view prefix) {
    if (!name.starts_with(prefix) || !name.ends_with("--")) {
        return std::nullopt;
    }
    return Trim(name.substr(prefix.size(), name.size() - prefix.size() - 2));
}

}

void CheatList::Append(CheatList other) {
    const auto offset = static_cast<u32>(menu.size());
    for (auto& cheat : other.cheats) {
        cheat.cheat_id += offset;
    }
    for (auto& item : other.menu) {
        item.id += offset;
        if (item.parent_id != CHEAT_MENU_ROOT) {
            item.parent_id += offset;
        }
    }
    cheats.insert(cheats.end(), std::make_move_iterator(other.cheats.begin()), std::make_move_iterator(other.cheats.end()));
    menu.insert(menu.end(), std::make_move_iterator(other.menu.begin()), std::make_move_iterator(other.menu.end()));
}

CheatParser::~CheatParser() = default;

TextCheatParser::TextCheatParser(bool enable_by_default_) : enable_by_default{enable_by_default_} {}

TextCheatParser::~TextCheatParser() = default;

CheatList TextCheatParser::Parse(std::string_view data) const {
    // The master block retains its position ahead of regular entries, as in Atmosphere.
    std::vector<TextEntry> entries(1);
    std::optional<std::size_t> current_entry;
    for (std::size_t i = 0; i < data.size(); ++i) {
        if (std::isspace(static_cast<unsigned char>(data[i]))) {
            continue;
        }
        if (data[i] == '{' || data[i] == '[') {
            const bool is_master = data[i] == '{';
            const auto end = data.find(is_master ? '}' : ']', i + 1);
            if (end == std::string_view::npos || end == i + 1) {
                return {};
            }
            if (is_master) {
                if (!entries[0].opcodes.empty()) {
                    return {};
                }
                current_entry = 0;
            } else {
                current_entry = entries.size();
                entries.emplace_back();
            }
            entries[*current_entry].name = data.substr(i + 1, end - i - 1);
            i = end;
        } else if (std::isxdigit(static_cast<unsigned char>(data[i]))) {
            if (!current_entry || data.size() - i < 8) {
                return {};
            }
            const auto hex = data.substr(i, 8);
            if (!std::all_of(hex.begin(), hex.end(), [](unsigned char value) { return std::isxdigit(value); })) {
                return {};
            }
            entries[*current_entry].opcodes.push_back(static_cast<u32>(std::strtoul(std::string{hex}.c_str(), nullptr, 16)));
            i += 7;
        } else {
            return {};
        }
    }

    CheatList result;
    const auto add_cheat = [&](TextEntry& entry, bool is_master, u32 parent_id) {
        if (entry.opcodes.empty()) {
            return;
        }
        const auto id = static_cast<u32>(result.menu.size());
        result.menu.push_back({
            .id = id,
            .name = entry.name,
            .is_master = is_master,
            .parent_id = parent_id,
        });
        CheatEntry cheat{
            .enabled = is_master || enable_by_default,
            .is_master = is_master,
            .cheat_id = id,
        };
        std::memcpy(cheat.definition.readable_name.data(), entry.name.data(),
                    std::min(entry.name.size(), cheat.definition.readable_name.size() - 1));
        cheat.definition.opcodes = std::move(entry.opcodes);
        result.cheats.push_back(std::move(cheat));
    };
    // The master code always runs, so it is never read as a GUI marker.
    add_cheat(entries[0], true, CHEAT_MENU_ROOT);

    const auto regular = std::span{entries}.subspan(1);
    const bool disable_submenus =
        std::any_of(regular.begin(), regular.end(),
                    [](const auto& entry) { return Trim(entry.name) == "--DisableSubmenus--"; });
    std::vector<u32> sections;
    std::optional<u32> outline;
    for (auto& entry : regular) {
        const auto name = Trim(entry.name);
        if (name == "--DisableSubmenus--") {
            continue;
        }
        // EdiZon section names take precedence over their placeholder opcodes.
        if (const auto section = SectionName(name, "--SectionEnd:")) {
            const auto match = std::find_if(sections.rbegin(), sections.rend(), [&](u32 group) {
                return result.menu[group].name == *section;
            });
            if (match != sections.rend()) {
                outline.reset();
                sections.erase(std::prev(match.base()), sections.end());
            }
            continue;
        }
        // Like Zing, only the first opcode marks an outline boundary.
        const auto opcode = entry.opcodes.empty() ? 0 : entry.opcodes[0];
        if (const auto section = SectionName(name, "--SectionStart:"); section || opcode == 0x20000000) {
            // EdiZon sections nest; Zing has one active outline, which ends at
            // the next start marker or when entering another EdiZon section.
            outline.reset();
            if (!disable_submenus) {
                const auto id = static_cast<u32>(result.menu.size());
                result.menu.push_back({
                    .id = id,
                    .name = std::string {section.value_or(name)},
                    .type = CheatMenuEntryType::Submenu,
                    .parent_id = sections.empty() ? CHEAT_MENU_ROOT : sections.back(),
                });
                if (section) {
                    sections.push_back(id);
                } else {
                    outline = id;
                }
            }
            continue;
        }
        if (opcode == 0x20000001) {
            outline.reset();
            continue;
        }
        add_cheat(entry, false, outline.value_or(sections.empty() ? CHEAT_MENU_ROOT : sections.back()));
    }
    return result;
}

} // namespace Core::Memory
