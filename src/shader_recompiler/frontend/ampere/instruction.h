// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include "common/common_types.h"

namespace Shader::Ampere {
struct Instruction {
    std::array<u32, 4> words;

    u32 Bits(u32 offset, u32 count) const {
        u64 bits = words[offset / 32];
        if (offset / 32 + 1 < words.size()) {
            bits |= u64{words[offset / 32 + 1]} << 32;
        }
        return static_cast<u32>((bits >> (offset % 32)) & ((u64{1} << count) - 1));
    }
};
static_assert(sizeof(Instruction) == 16);

// SM70+ relative control-flow immediates start at bit 34 and are expressed in
// 32-bit words. Expanding them into byte displacements leaves bits 0-1 clear.
inline s64 RelativeOffset48(const Instruction& instruction) {
    const u64 raw = u64{instruction.words[1] & ~3U} |
                    (u64{instruction.words[2] & 0x3ffffU} << 32);
    return static_cast<s64>(raw << 14) >> 14;
}

inline s64 RelativeOffset30(const Instruction& instruction) {
    return static_cast<s32>(instruction.words[1] & ~3U);
}

} // namespace Shader::Ampere
