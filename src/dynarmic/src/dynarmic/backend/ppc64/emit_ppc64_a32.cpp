// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <powah_emit.hpp>
#include <fmt/ostream.h>

#include "dynarmic/frontend/A32/a32_types.h"
#include "dynarmic/backend/ppc64/a32_core.h"
#include "dynarmic/backend/ppc64/abi.h"
#include "dynarmic/backend/ppc64/emit_context.h"
#include "dynarmic/backend/ppc64/emit_ppc64.h"
#include "dynarmic/backend/ppc64/reg_alloc.h"
#include "dynarmic/ir/basic_block.h"
#include "dynarmic/ir/microinstruction.h"
#include "dynarmic/ir/opcodes.h"

namespace Dynarmic::Backend::PPC64 {

template<>
void EmitIR<IR::Opcode::A32SetCheckBit>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32GetRegister>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    if (inst->GetArg(0).GetType() == IR::Type::A32Reg) {
        auto const result = ctx.reg_alloc.ScratchGpr();
        code.ADDI(result, PPC64::RJIT, A32::RegNumber(inst->GetArg(0).GetA32RegRef()) * sizeof(u32));
        code.LWZ(result, result, offsetof(A32JitState, regs));
        ctx.reg_alloc.DefineValue(inst, result);
    } else {
        ASSERT(false && "unimp");
    }
}

template<>
void EmitIR<IR::Opcode::A32GetExtendedRegister32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32GetExtendedRegister64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32GetVector>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32SetRegister>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const value = ctx.reg_alloc.UseGpr(inst->GetArg(1));
    if (inst->GetArg(0).GetType() == IR::Type::A32Reg) {
        auto const addr = ctx.reg_alloc.ScratchGpr();
        code.ADDI(addr, PPC64::RJIT, A32::RegNumber(inst->GetArg(0).GetA32RegRef()) * sizeof(u32));
        code.STW(value, addr, offsetof(A32JitState, regs));
    } else {
        ASSERT(false && "unimp");
    }
}

template<>
void EmitIR<IR::Opcode::A32SetExtendedRegister32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32SetExtendedRegister64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32SetVector>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32GetCpsr>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32SetCpsr>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32SetCpsrNZCV>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32SetCpsrNZCVRaw>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32SetCpsrNZCVQ>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32SetCpsrNZ>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32SetCpsrNZC>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32GetCFlag>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    code.LD(result, PPC64::RJIT, offsetof(A32JitState, cpsr_nzcv));
    code.RLWINM(result, result, 31, 31, 31);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::A32OrQFlag>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32GetGEFlags>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32SetGEFlags>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32SetGEFlagsCompressed>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32BXWritePC>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32UpdateUpperLocationDescriptor>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32CallSupervisor>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32ExceptionRaised>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32DataSynchronizationBarrier>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32DataMemoryBarrier>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32InstructionSynchronizationBarrier>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32GetFpscr>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32SetFpscr>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32GetFpscrNZCV>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32SetFpscrNZCV>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

// Memory
template<>
void EmitIR<IR::Opcode::A32ClearExclusive>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32ReadMemory8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32ReadMemory16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32ReadMemory32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32ReadMemory64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32ExclusiveReadMemory8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32ExclusiveReadMemory16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32ExclusiveReadMemory32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32ExclusiveReadMemory64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32WriteMemory8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32WriteMemory16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32WriteMemory32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32WriteMemory64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32ExclusiveWriteMemory8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32ExclusiveWriteMemory16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32ExclusiveWriteMemory32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32ExclusiveWriteMemory64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

// Coprocesor
template<>
void EmitIR<IR::Opcode::A32CoprocInternalOperation>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32CoprocSendOneWord>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32CoprocSendTwoWords>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32CoprocGetOneWord>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32CoprocGetTwoWords>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32CoprocLoadWords>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A32CoprocStoreWords>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

}  // namespace Dynarmic::Backend::RV64
