// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <type_traits>

namespace Common {

/// Ceiled integer division.
template <typename N, typename D>
    requires std::is_integral_v<N> && std::is_unsigned_v<D>
[[nodiscard]] constexpr N DivCeil(N number, D divisor) {
    return N((D(number) + divisor - 1) / divisor);
}

/// Ceiled integer division with logarithmic divisor in base 2
template <typename N, typename D>
    requires std::is_integral_v<N> && std::is_unsigned_v<D>
[[nodiscard]] constexpr N DivCeilLog2(N value, D alignment_log2) {
    return N((D(value) + (D(1) << alignment_log2) - 1) >> alignment_log2);
}

} // namespace Common
