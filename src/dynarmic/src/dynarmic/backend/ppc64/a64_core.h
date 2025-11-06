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
    u32 upper_location_descriptor;
    u32 exclusive_state = 0;
    u32 pstate = 0;
    u32 fpcr = 0;
    u32 fpsr = 0;
    volatile u32 halt_reason = 0;
    u8 check_bit = 0;
    IR::LocationDescriptor GetLocationDescriptor() const {
        return IR::LocationDescriptor{regs[15] | (u64(upper_location_descriptor) << 32)};
    }
};

class A64AddressSpace final {
public:
    explicit A64AddressSpace(const A64::UserConfig& conf);
    CodePtr GetOrEmit(IR::LocationDescriptor descriptor);
    void ClearCache();
private:
    friend class A64Core;
    void EmitPrelude();
    EmittedBlockInfo Emit(IR::Block ir_block);
    void Link(EmittedBlockInfo& block);

    const A64::UserConfig conf;
    CodeBlock cb;
    powah::Context as;
    ankerl::unordered_dense::map<u64, CodePtr> block_entries;
    ankerl::unordered_dense::map<u64, EmittedBlockInfo> block_infos;
};

class A64Core final {
public:
    explicit A64Core(const A64::UserConfig&) {}
    HaltReason Run(A64AddressSpace& process, A64JitState& thread_ctx, volatile u32* halt_reason) {
        const auto loc = thread_ctx.GetLocationDescriptor();
        const auto entry = process.GetOrEmit(loc);
        using CodeFn = HaltReason (*)(A64JitState*, volatile u32*);
        return (CodeFn(entry))(&thread_ctx, halt_reason);
    }
};

}  // namespace Dynarmic::Backend::RV64
