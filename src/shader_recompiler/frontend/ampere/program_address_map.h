// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <optional>
#include <span>
#include <vector>
#include "common/common_types.h"

namespace Shader::Ampere {
constexpr u64 NativeAddressMapMagic = 0x50414d4e4544454eULL;
inline bool HasNativeAddressMap(std::span<const u64> code) {
    return code.size() >= 2 && code.back() == NativeAddressMapMagic;
}
inline void AppendNativeAddressMap(std::vector<u64>& code, std::span<const u32> offsets) {
    if (offsets.empty()) return;
    for (u32 index : offsets) code.push_back(0x50U + index * 8 + ((index + 1) / 3) * 8);
    code.push_back(offsets.size());
    code.push_back(NativeAddressMapMagic);
}
inline std::optional<u32> NativeProgramAddress(std::span<const u64> code, u32 address) {
    if (!HasNativeAddressMap(code) || address % 16) return {};
    const u64 count = code[code.size() - 2];
    if (!count || count > 65536 || count + 2 > code.size() || address / 16 >= count) return {};
    const size_t start = code.size() - static_cast<size_t>(count) - 2;
    const u64 target = code[start + address / 16];
    if (target < 0x50 || target >= start * 8 || target % 8 || target % 32 == 0) return {};
    return static_cast<u32>(target);
}
} // namespace Shader::Ampere
