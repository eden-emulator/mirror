// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>

#include "dynarmic/common/common_types.h"

namespace Dynarmic::Backend::PPC64 {

constexpr size_t SpillCount = 16;

struct alignas(16) StackLayout {
    u64 resv0; //0
    u64 resv1; //8
    u64 lr; //16
    u64 sp; //24
    std::array<u64, SpillCount> spill;
    u64 check_bit;
};

static_assert(sizeof(StackLayout) % 16 == 0);

}  // namespace Dynarmic::Backend::RV64
