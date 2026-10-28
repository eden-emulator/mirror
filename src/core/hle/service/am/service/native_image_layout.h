// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <span>
#include "common/common_types.h"

namespace Service::AM {

struct NativeImageRequirements {
    u64 size;
    u64 alignment;
};

// Experimental inter-module block image layout, restricted to the observed
// four-byte color format. Row pitch is in 64-byte GOBs; block height is in pixels.
inline std::optional<NativeImageRequirements> CalculateNativeImageRequirements(
    std::span<const u32> preference, u32 count) {
    if (preference.size() != 16 || count != 1 || preference[0] != 0 ||
        (preference[1] != 1 && preference[1] != 2) || preference[3] != 0 || preference[6] != 1 ||
        preference[7] != 1 || preference[4] < 3 || preference[4] > 8) {
        return std::nullopt;
    }
    for (size_t i = 8; i < preference.size(); ++i) {
        if (preference[i] != 0) { return std::nullopt; }
    }
    const u64 width = (preference[2] & 0xffffU) + 1;
    const u64 height = (preference[2] >> 16) + 1;
    const u64 pitch_gobs = preference[5];
    if (pitch_gobs < (width * 4 + 63) / 64 || pitch_gobs > 4096) {
        return std::nullopt;
    }
    const u64 block_height = 1ULL << preference[4];
    const u64 aligned_height = (height + block_height - 1) & ~(block_height - 1);
    return NativeImageRequirements{pitch_gobs * 64 * aligned_height, 64 * block_height};
}

} // namespace Service::AM
