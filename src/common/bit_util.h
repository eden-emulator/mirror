// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <bit>
#include <climits>
#include <cstddef>
#include <type_traits>

#include "common/common_types.h"

namespace Common {

/// Gets the size of a specified type T in bits.
template <typename T>
    requires std::is_integral_v<T>
[[nodiscard]] constexpr std::size_t BitSize() {
    return std::size_t(sizeof(T) * CHAR_BIT);
}

template<typename T>
    requires std::is_integral_v<T>
[[nodiscard]] constexpr T Log2Floor(const T value) {
    return std::bit_width(value) - 1;
}

template<typename T>
    requires std::is_integral_v<T>
[[nodiscard]] constexpr T Log2Ceil(const T value) {
    return std::bit_width(value - 1);
}

template <size_t bit_index, typename T>
    requires (std::is_integral_v<T> && bit_index < BitSize<T>())
[[nodiscard]] constexpr bool Bit(const T value) {
    return (T(value >> bit_index) & T(1)) == T(1);
}

} // namespace Common
