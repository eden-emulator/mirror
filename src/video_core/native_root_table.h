// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

namespace Tegra {

class NativeRootTables {
public:
    static constexpr std::uint32_t FirstBank = 24;
    static constexpr std::uint32_t Count = 8;
    static constexpr std::uint32_t Size = 256;
    static constexpr std::uint32_t SelectorMethod = 0x0504 / 4;
    static constexpr std::uint32_t LoadMethod = 0x0508 / 4;
    using Table = std::array<std::uint32_t, Size / 4>;

    static constexpr bool IsBank(std::uint32_t bank) {
        return bank >= FirstBank && bank - FirstBank < Count;
    }

    void Select(std::uint32_t value) {
        selected = value & 7;
        offset = (value >> 8) & 255;
    }

    bool Load(std::uint32_t value) {
        if (offset % 4 || offset >= Size) return false;
        tables[selected][offset / 4] = value;
        offset += 4;
        return true;
    }

    const Table& Data(std::uint32_t bank) const {
        return tables.at(bank - FirstBank);
    }

    std::uint32_t Read(std::uint32_t bank, std::uint32_t byte_offset) const {
        if (!IsBank(bank) || byte_offset % 4 || byte_offset >= Size) return 0;
        return Data(bank)[byte_offset / 4];
    }

private:
    std::array<Table, Count> tables{};
    std::uint32_t selected{}, offset{};
};

} // namespace Tegra
