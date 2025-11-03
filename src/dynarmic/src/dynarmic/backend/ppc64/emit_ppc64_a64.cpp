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

template<>
void EmitIR<IR::Opcode::A64SetCheckBit>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64GetCFlag>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64GetNZCVRaw>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64SetNZCVRaw>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64SetNZCV>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64GetW>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64GetX>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64GetS>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64GetD>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64GetQ>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64GetSP>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64GetFPCR>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64GetFPSR>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64SetW>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64SetX>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64SetS>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64SetD>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64SetQ>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64SetSP>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64SetFPCR>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64SetFPSR>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64SetPC>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64CallSupervisor>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64ExceptionRaised>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64DataCacheOperationRaised>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64InstructionCacheOperationRaised>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64DataSynchronizationBarrier>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64DataMemoryBarrier>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64InstructionSynchronizationBarrier>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64GetCNTFRQ>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64GetCNTPCT>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64GetCTR>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64GetDCZID>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64GetTPIDR>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64GetTPIDRRO>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64SetTPIDR>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

// Memory
template<>
void EmitIR<IR::Opcode::A64ClearExclusive>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64ReadMemory8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64ReadMemory16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64ReadMemory32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64ReadMemory64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64ReadMemory128>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64ExclusiveReadMemory8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64ExclusiveReadMemory16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64ExclusiveReadMemory32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64ExclusiveReadMemory64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64ExclusiveReadMemory128>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64WriteMemory8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64WriteMemory16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64WriteMemory32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64WriteMemory64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64WriteMemory128>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64ExclusiveWriteMemory8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64ExclusiveWriteMemory16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64ExclusiveWriteMemory32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64ExclusiveWriteMemory64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::A64ExclusiveWriteMemory128>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}


}  // namespace Dynarmic::Backend::RV64
