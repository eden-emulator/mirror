// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once


#include <powah_emit.hpp>
#include <ankerl/unordered_dense.h>
#include "dynarmic/backend/ppc64/code_block.h"
#include "dynarmic/backend/ppc64/emit_ppc64.h"
#include "dynarmic/frontend/A64/a64_location_descriptor.h"
#include "dynarmic/interface/A64/a64.h"
#include "dynarmic/interface/A64/config.h"
#include "dynarmic/ir/terminal.h"
#include "dynarmic/ir/basic_block.h"

namespace Dynarmic::Backend::PPC64 {

struct A64JitState {
    using ProgramCounterType = u32;
    std::array<u64, 31> regs{};
    u64 pc = 0;
    alignas(16) std::array<u64, 64> vec{};
    u64 sp = 0;
    u32 upper_location_descriptor = 0;
    u32 exclusive_state = 0;
    u32 pstate = 0;
    u32 fpcr = 0;
    u32 fpsr = 0;
    volatile u32 halt_reason = 0;
    u8 check_bit = 0;
    void* run_fn = nullptr;

    IR::LocationDescriptor GetLocationDescriptor() const {
        const u64 fpcr_u64 = u64(fpcr & A64::LocationDescriptor::fpcr_mask) << A64::LocationDescriptor::fpcr_shift;
        const u64 pc_u64 = pc & A64::LocationDescriptor::pc_mask;
        return IR::LocationDescriptor{pc_u64 | fpcr_u64};
    }
};

}  // namespace Dynarmic::Backend::RV64
