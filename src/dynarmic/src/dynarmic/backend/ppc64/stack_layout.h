// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>

#include "dynarmic/common/common_types.h"

namespace Dynarmic::Backend::PPC64 {

constexpr size_t SpillCount = 16;

struct alignas(16) StackLayout {
    std::array<u64, 18> regs;
    std::array<u64, SpillCount> spill;
    u64 check_bit;
    void* lr;
    std::array<u64, 29> extra_regs;
};

static_assert(sizeof(StackLayout) % 16 == 0);

}  // namespace Dynarmic::Backend::RV64
