// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <powah_emit.hpp>
#include <fmt/ostream.h>

#include "dynarmic/frontend/A64/a64_types.h"
#include "dynarmic/backend/ppc64/a64_core.h"
#include "dynarmic/backend/ppc64/abi.h"
#include "dynarmic/backend/ppc64/emit_context.h"
#include "dynarmic/backend/ppc64/emit_ppc64.h"
#include "dynarmic/backend/ppc64/reg_alloc.h"
#include "dynarmic/ir/basic_block.h"
#include "dynarmic/ir/microinstruction.h"
#include "dynarmic/ir/opcodes.h"

namespace Dynarmic::Backend::PPC64 {

template<>
void EmitIR<IR::Opcode::A64SetCheckBit>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const value = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    code.STD(value, powah::R1, offsetof(StackLayout, check_bit));
}

template<>
void EmitIR<IR::Opcode::A64GetCFlag>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64GetNZCVRaw>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64SetNZCVRaw>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64SetNZCV>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const value = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    code.STW(value, PPC64::RJIT, offsetof(A64JitState, pstate));
}

template<>
void EmitIR<IR::Opcode::A64GetW>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    if (inst->GetArg(0).GetType() == IR::Type::A64Reg) {
        auto const result = ctx.reg_alloc.ScratchGpr();
        // Need to account for endianess here...
#ifdef __ORDER_BIG_ENDIAN__
        constexpr u32 pe_offset64 = 4;
#else
        constexpr u32 pe_offset64 = 0;
#endif
        auto const offs = offsetof(A64JitState, regs)
            + A64::RegNumber(inst->GetArg(0).GetA64RegRef()) * sizeof(u64)
            + pe_offset64;
        code.LWZ(result, PPC64::RJIT, offs);
        ctx.reg_alloc.DefineValue(inst, result);
    } else {
        ASSERT(false && "unimp");
    }
}

template<>
void EmitIR<IR::Opcode::A64GetX>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    if (inst->GetArg(0).GetType() == IR::Type::A64Reg) {
        auto const result = ctx.reg_alloc.ScratchGpr();
        auto const offs = offsetof(A64JitState, regs)
            + A64::RegNumber(inst->GetArg(0).GetA64RegRef()) * sizeof(u64);
        code.LD(result, PPC64::RJIT, offs);
        ctx.reg_alloc.DefineValue(inst, result);
    } else {
        ASSERT(false && "unimp");
    }
}

template<>
void EmitIR<IR::Opcode::A64GetS>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64GetD>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64GetQ>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64GetSP>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64GetFPCR>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64GetFPSR>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64SetW>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const value = ctx.reg_alloc.UseGpr(inst->GetArg(1));
    if (inst->GetArg(0).GetType() == IR::Type::A64Reg) {
        auto const tmp = ctx.reg_alloc.ScratchGpr();
        auto const offs = offsetof(A64JitState, regs)
            + A64::RegNumber(inst->GetArg(0).GetA64RegRef()) * sizeof(u64);
        code.RLDICL(tmp, value, 0, 32);
        code.STD(tmp, PPC64::RJIT, offs);
    } else {
        ASSERT(false && "unimp");
    }
}

template<>
void EmitIR<IR::Opcode::A64SetX>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const value = ctx.reg_alloc.UseGpr(inst->GetArg(1));
    if (inst->GetArg(0).GetType() == IR::Type::A64Reg) {
        auto const offs = offsetof(A64JitState, regs)
            + A64::RegNumber(inst->GetArg(0).GetA64RegRef()) * sizeof(u64);
        code.STD(value, PPC64::RJIT, offs);
    } else {
        ASSERT(false && "unimp");
    }
}

template<>
void EmitIR<IR::Opcode::A64SetS>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64SetD>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64SetQ>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64SetSP>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64SetFPCR>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64SetFPSR>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64SetPC>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64CallSupervisor>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64ExceptionRaised>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64DataCacheOperationRaised>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64InstructionCacheOperationRaised>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64DataSynchronizationBarrier>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64DataMemoryBarrier>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64InstructionSynchronizationBarrier>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64GetCNTFRQ>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64GetCNTPCT>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64GetCTR>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64GetDCZID>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64GetTPIDR>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64GetTPIDRRO>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64SetTPIDR>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

// Memory
template<>
void EmitIR<IR::Opcode::A64ClearExclusive>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64ReadMemory8>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64ReadMemory16>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64ReadMemory32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64ReadMemory64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64ReadMemory128>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64ExclusiveReadMemory8>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64ExclusiveReadMemory16>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64ExclusiveReadMemory32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64ExclusiveReadMemory64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64ExclusiveReadMemory128>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64WriteMemory8>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64WriteMemory16>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64WriteMemory32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64WriteMemory64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64WriteMemory128>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64ExclusiveWriteMemory8>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64ExclusiveWriteMemory16>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64ExclusiveWriteMemory32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64ExclusiveWriteMemory64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64ExclusiveWriteMemory128>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}


}  // namespace Dynarmic::Backend::RV64
