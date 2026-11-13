// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string_view>
#include <vector>
#include "core/memory/dmnt_cheat_types.h"

namespace Core::Memory {

// GUI metadata has no opcodes and is kept separate from the executable entries.
struct CheatList {
    std::vector<CheatEntry> cheats;
    std::vector<RuntimeCheatInfo> menu;

    void Append(CheatList other);
};

class CheatParser {
public:
    virtual ~CheatParser();

    [[nodiscard]] virtual CheatList Parse(std::string_view data) const = 0;
};

class TextCheatParser final : public CheatParser {
public:
    explicit TextCheatParser(bool enable_by_default);
    ~TextCheatParser() override;

    [[nodiscard]] CheatList Parse(std::string_view data) const override;

private:
    bool enable_by_default;
};

} // namespace Core::Memory
