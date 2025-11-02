// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <powah_emit.hpp>
#include <fmt/ostream.h>

#include "dynarmic/backend/ppc64/a32_core.h"
#include "dynarmic/backend/ppc64/abi.h"
#include "dynarmic/backend/ppc64/emit_context.h"
#include "dynarmic/backend/ppc64/emit_ppc64.h"
#include "dynarmic/backend/ppc64/reg_alloc.h"
#include "dynarmic/ir/basic_block.h"
#include "dynarmic/ir/microinstruction.h"
#include "dynarmic/ir/opcodes.h"

namespace Dynarmic::Backend::PPC64 {

void EmitA32Cond(powah::Context& as, EmitContext&, IR::Cond cond, powah::Label* label) {
    UNREACHABLE();
}

void EmitA32Terminal(powah::Context& as, EmitContext& ctx, IR::Term::Terminal terminal, IR::LocationDescriptor initial_location, bool is_single_step);

void EmitA32Terminal(powah::Context&, EmitContext&, IR::Term::Interpret, IR::LocationDescriptor, bool) {
    UNREACHABLE();
}

void EmitA32Terminal(powah::Context& as, EmitContext& ctx, IR::Term::ReturnToDispatch, IR::LocationDescriptor, bool) {
    EmitRelocation(as, ctx, LinkTarget::ReturnFromRunCode);
}

void EmitSetUpperLocationDescriptor(powah::Context& as, EmitContext& ctx, IR::LocationDescriptor new_location, IR::LocationDescriptor old_location) {
    UNREACHABLE();
}

void EmitA32Terminal(powah::Context& as, EmitContext& ctx, IR::Term::LinkBlock terminal, IR::LocationDescriptor initial_location, bool) {
    UNREACHABLE();
}

void EmitA32Terminal(powah::Context& as, EmitContext& ctx, IR::Term::LinkBlockFast terminal, IR::LocationDescriptor initial_location, bool) {
    UNREACHABLE();
}

void EmitA32Terminal(powah::Context& as, EmitContext& ctx, IR::Term::PopRSBHint, IR::LocationDescriptor, bool) {
    UNREACHABLE();
}

void EmitA32Terminal(powah::Context& as, EmitContext& ctx, IR::Term::FastDispatchHint, IR::LocationDescriptor, bool) {
    UNREACHABLE();
}

void EmitA32Terminal(powah::Context& as, EmitContext& ctx, IR::Term::If terminal, IR::LocationDescriptor initial_location, bool is_single_step) {
    UNREACHABLE();
}

void EmitA32Terminal(powah::Context& as, EmitContext& ctx, IR::Term::CheckBit terminal, IR::LocationDescriptor initial_location, bool is_single_step) {
    UNREACHABLE();
}

void EmitA32Terminal(powah::Context& as, EmitContext& ctx, IR::Term::CheckHalt terminal, IR::LocationDescriptor initial_location, bool is_single_step) {
    UNREACHABLE();
}

void EmitA32Terminal(powah::Context& as, EmitContext& ctx, IR::Term::Terminal terminal, IR::LocationDescriptor initial_location, bool is_single_step) {
    UNREACHABLE();
}

void EmitA32Terminal(powah::Context& as, EmitContext& ctx) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32SetCheckBit>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32GetRegister>(powah::Context& as, EmitContext& ctx, IR::Inst* inst) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32GetExtendedRegister32>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32GetExtendedRegister64>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32GetVector>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32SetRegister>(powah::Context& as, EmitContext& ctx, IR::Inst* inst) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32SetExtendedRegister32>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32SetExtendedRegister64>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32SetVector>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32GetCpsr>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32SetCpsr>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32SetCpsrNZCV>(powah::Context& as, EmitContext& ctx, IR::Inst* inst) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32SetCpsrNZCVRaw>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32SetCpsrNZCVQ>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32SetCpsrNZ>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32SetCpsrNZC>(powah::Context& as, EmitContext& ctx, IR::Inst* inst) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32GetCFlag>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32OrQFlag>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32GetGEFlags>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32SetGEFlags>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32SetGEFlagsCompressed>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32BXWritePC>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32UpdateUpperLocationDescriptor>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32CallSupervisor>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32ExceptionRaised>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32DataSynchronizationBarrier>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32DataMemoryBarrier>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32InstructionSynchronizationBarrier>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32GetFpscr>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32SetFpscr>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32GetFpscrNZCV>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32SetFpscrNZCV>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

// Memory
template<>
void EmitIR<IR::Opcode::A32ClearExclusive>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32ReadMemory8>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32ReadMemory16>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32ReadMemory32>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32ReadMemory64>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32ExclusiveReadMemory8>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32ExclusiveReadMemory16>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32ExclusiveReadMemory32>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32ExclusiveReadMemory64>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32WriteMemory8>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32WriteMemory16>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32WriteMemory32>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32WriteMemory64>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32ExclusiveWriteMemory8>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32ExclusiveWriteMemory16>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32ExclusiveWriteMemory32>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32ExclusiveWriteMemory64>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

// Coprocesor
template<>
void EmitIR<IR::Opcode::A32CoprocInternalOperation>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32CoprocSendOneWord>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32CoprocSendTwoWords>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32CoprocGetOneWord>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32CoprocGetTwoWords>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32CoprocLoadWords>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::A32CoprocStoreWords>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

}  // namespace Dynarmic::Backend::RV64
