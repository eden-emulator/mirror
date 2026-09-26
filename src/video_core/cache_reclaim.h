// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>

#include "common/common_types.h"

namespace VideoCommon {

struct ReclaimThresholds {
    u64 minimum{};
    u64 expected{};
    u64 critical{};
    u64 headroom{};
};

[[nodiscard]] constexpr ReclaimThresholds MakeReclaimThresholds(u64 device_local_memory,
                                                                u64 target_threshold,
                                                                u64 default_expected,
                                                                u64 default_critical,
                                                                u64 default_headroom) {
    u64 critical = default_critical;
    if (device_local_memory != 0) {
        const u64 budget = (std::min)(device_local_memory, target_threshold);
        critical = (std::min)(critical, budget / 2);
    }
    const u64 expected = (std::min)(default_expected, (critical * 3) / 4);
    return ReclaimThresholds{
        .minimum = (expected * 3) / 4,
        .expected = expected,
        .critical = critical,
        .headroom = (std::clamp)(device_local_memory / 4, default_headroom, default_headroom * 2),
    };
}

} // namespace VideoCommon
