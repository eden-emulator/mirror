// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <span>
#include "common/common_types.h"
namespace Tegra {
// NVC797_SET_VARIABLE_PIXEL_RATE_SHADING_CONTROL(j), ENABLE bit 0.
inline u32 NativeVprsEnabledTables(std::span<const u32, 16> controls) {
    u32 enabled{};
    for (u32 index = 0; index < controls.size(); ++index)
        enabled |= (controls[index] & 1U) << index;
    return enabled;
}

struct NativeVprsFallback {
    u32 enabled{};
    u32 coarse{};
    u32 unsupported{};
    bool AllowsFullRate(bool fragment_writes_memory) const {
        return unsupported == 0 && (!fragment_writes_memory || coarse == 0);
    }
};
// Native rate 5 is one invocation per pixel; 6..11 are coarse rates.
inline NativeVprsFallback ClassifyNativeVprs(std::span<const u32,16> controls,
                                            std::span<const u32,16> rates_a,
                                            std::span<const u32,16> rates_b) {
    NativeVprsFallback result;
    result.enabled = NativeVprsEnabledTables(controls);
    for (u32 table = 0; table < controls.size(); ++table) {
        if (!(result.enabled & (1U << table))) continue;
        for (u32 index = 0; index < 16; ++index) {
            const u32 packed = index < 8 ? rates_a[table] : rates_b[table];
            const u32 rate = (packed >> ((index & 7) * 4)) & 15U;
            if (rate < 5 || rate > 11) result.unsupported |= 1U << table;
            else if (rate > 5) result.coarse |= 1U << table;
        }
    }
    return result;
}
}
