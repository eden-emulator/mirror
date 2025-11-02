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
void EmitIR<IR::Opcode::FPAbs16>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPAbs32>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPAbs64>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPAdd32>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPAdd64>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPCompare32>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPCompare64>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPDiv32>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPDiv64>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPMax32>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPMax64>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPMaxNumeric32>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPMaxNumeric64>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPMin32>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPMin64>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPMinNumeric32>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPMinNumeric64>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPMul32>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPMul64>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPMulAdd16>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPMulAdd32>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPMulAdd64>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPMulSub16>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPMulSub32>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPMulSub64>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPMulX32>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPMulX64>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPNeg16>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPNeg32>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPNeg64>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPRecipEstimate16>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPRecipEstimate32>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPRecipEstimate64>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPRecipExponent16>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPRecipExponent32>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPRecipExponent64>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPRecipStepFused16>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPRecipStepFused32>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPRecipStepFused64>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPRoundInt16>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPRoundInt32>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPRoundInt64>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPRSqrtEstimate16>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPRSqrtEstimate32>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPRSqrtEstimate64>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPRSqrtStepFused16>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPRSqrtStepFused32>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPRSqrtStepFused64>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPSqrt32>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPSqrt64>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPSub32>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPSub64>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPHalfToDouble>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPHalfToSingle>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPSingleToDouble>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPSingleToHalf>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPDoubleToHalf>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPDoubleToSingle>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPDoubleToFixedS16>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPDoubleToFixedS32>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPDoubleToFixedS64>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPDoubleToFixedU16>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPDoubleToFixedU32>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPDoubleToFixedU64>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPHalfToFixedS16>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPHalfToFixedS32>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPHalfToFixedS64>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPHalfToFixedU16>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPHalfToFixedU32>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPHalfToFixedU64>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPSingleToFixedS16>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPSingleToFixedS32>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPSingleToFixedS64>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPSingleToFixedU16>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPSingleToFixedU32>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPSingleToFixedU64>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPFixedU16ToSingle>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPFixedS16ToSingle>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPFixedU16ToDouble>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPFixedS16ToDouble>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPFixedU32ToSingle>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPFixedS32ToSingle>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPFixedU32ToDouble>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPFixedS32ToDouble>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPFixedU64ToDouble>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPFixedU64ToSingle>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPFixedS64ToDouble>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::FPFixedS64ToSingle>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

}  // namespace Dynarmic::Backend::RV64
