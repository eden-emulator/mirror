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
    IR::LocationDescriptor GetLocationDescriptor() const {
        return IR::LocationDescriptor{regs[15] | (u64(upper_location_descriptor) << 32)};
    }
};

class A32AddressSpace final {
public:
    explicit A32AddressSpace(const A32::UserConfig& conf);
    CodePtr GetOrEmit(IR::LocationDescriptor descriptor);
    void ClearCache();
private:
    friend class A32Core;
    EmittedBlockInfo Emit(IR::Block ir_block);
    void Link(EmittedBlockInfo& block);
    const A32::UserConfig conf;
    CodeBlock cb;
    powah::Context as;
    ankerl::unordered_dense::map<u64, CodePtr> block_entries;
    ankerl::unordered_dense::map<u64, EmittedBlockInfo> block_infos;
};

class A32Core final {
public:
    explicit A32Core(const A32::UserConfig&) {}
    HaltReason Run(A32AddressSpace& process, A32JitState& thread_ctx, volatile u32* halt_reason) {
        auto const loc = thread_ctx.GetLocationDescriptor();
        auto const entry = process.GetOrEmit(loc);
        using CodeFn = HaltReason (*)(A32JitState*, volatile u32*);
        return (CodeFn(entry))(&thread_ctx, halt_reason);
    }
};

}  // namespace Dynarmic::Backend::RV64
