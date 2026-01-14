// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <powah_emit.hpp>
#include <ankerl/unordered_dense.h>
#include "dynarmic/backend/ppc64/code_block.h"
#include "dynarmic/backend/ppc64/emit_ppc64.h"
#include "dynarmic/frontend/A32/a32_location_descriptor.h"
#include "dynarmic/interface/A32/a32.h"
#include "dynarmic/interface/A32/config.h"
#include "dynarmic/ir/terminal.h"
#include "dynarmic/ir/basic_block.h"

namespace Dynarmic::Backend::PPC64 {

struct A32JitState {
    std::array<u32, 16> regs{};
    alignas(16) std::array<u32, 64> ext_regs{};
    u32 upper_location_descriptor;
    u32 exclusive_state = 0;
    u32 cpsr_nzcv = 0;
    u32 fpscr = 0;
    u8 check_bit = 0;

    IR::LocationDescriptor GetLocationDescriptor() const {
        return IR::LocationDescriptor{regs[15] | (u64(upper_location_descriptor) << 32)};
    }
};

}  // namespace Dynarmic::Backend::RV64
