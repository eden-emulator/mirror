// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include "common/common_types.h"

namespace VideoCommon {

inline u32 StorageBufferSize(u64 mapped_size, u64 following_word, bool native_pointer) {
    // Native global addresses are bare pointers. Adjacent constants may be
    // texture handles or dimensions, rather than an NVN descriptor length.
    if (!native_pointer && following_word != 0 && following_word <= mapped_size &&
        (following_word >> 32) == 0) {
        return static_cast<u32>(following_word);
    }
    return static_cast<u32>((std::min)(mapped_size, u64{8 * 1024 * 1024}));
}

} // namespace VideoCommon
