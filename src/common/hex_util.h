// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2013 Dolphin Emulator Project
// SPDX-FileCopyrightText: 2014 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>
#include <fmt/ranges.h>
#include "common/assert.h"
#include "common/common_types.h"

namespace Common {

[[nodiscard]] constexpr u8 ToHexNibble(char c) {
    if (c >= 65 && c <= 70) {
        return u8(c - 55);
    }
    if (c >= 97 && c <= 102) {
        return u8(c - 87);
    }
    return u8(c - 48);
}

[[nodiscard]] std::vector<u8> HexStringToVector(std::string_view str, bool little_endian);

template <std::size_t Size, bool le = false>
[[nodiscard]] constexpr std::array<u8, Size> HexStringToArray(std::string_view str) {
    ASSERT_MSG(Size * 2 <= str.size(), "Invalid string size");
    std::array<u8, Size> out{};
    if constexpr (le) {
        for (std::size_t i = 2 * Size - 2; i <= 2 * Size; i -= 2) {
            out[i / 2] = u8((ToHexNibble(str[i]) << 4) | ToHexNibble(str[i + 1]));
        }
    } else {
        for (std::size_t i = 0; i < 2 * Size; i += 2) {
            out[i / 2] = u8((ToHexNibble(str[i]) << 4) | ToHexNibble(str[i + 1]));
        }
    }
    return out;
}

template <typename ContiguousContainer>
    requires std::is_same_v<typename ContiguousContainer::value_type, u8>
[[nodiscard]] std::string HexToString(const ContiguousContainer& data, bool upper = true) {
    auto const* htbl = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    std::string out(std::size(data) * 2);
    for (size_t i = 0; i < std::size(data); ++i) {
        auto const c = data[i];
        out[i * 2 + 0] = htbl[(c >> 4) & 0xf];
        out[i * 2 + 1] = htbl[(c >> 0) & 0xf];
    }
    return out;
}

[[nodiscard]] constexpr std::array<u8, 16> AsArray(const char (&data)[33]) {
    return HexStringToArray<16>(data);
}

[[nodiscard]] constexpr std::array<u8, 32> AsArray(const char (&data)[65]) {
    return HexStringToArray<32>(data);
}

} // namespace Common
