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
void EmitIR<IR::Opcode::VectorGetElement8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorGetElement16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorGetElement32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorGetElement64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSetElement8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSetElement16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSetElement32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSetElement64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorAbs8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorAbs16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorAbs32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorAbs64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorAdd8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorAdd16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorAdd32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorAdd64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorAnd>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorAndNot>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorArithmeticShiftRight8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorArithmeticShiftRight16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorArithmeticShiftRight32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorArithmeticShiftRight64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorArithmeticVShift8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorArithmeticVShift16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorArithmeticVShift32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorArithmeticVShift64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorBroadcastLower8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorBroadcastLower16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorBroadcastLower32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorBroadcast8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorBroadcast16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorBroadcast32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorBroadcast64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorBroadcastElementLower8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorBroadcastElementLower16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorBroadcastElementLower32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorBroadcastElement8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorBroadcastElement16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorBroadcastElement32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorBroadcastElement64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorCountLeadingZeros8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorCountLeadingZeros16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorCountLeadingZeros32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorDeinterleaveEven8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorDeinterleaveEven16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorDeinterleaveEven32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorDeinterleaveEven64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorDeinterleaveEvenLower8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorDeinterleaveEvenLower16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorDeinterleaveEvenLower32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorDeinterleaveOdd8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorDeinterleaveOdd16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorDeinterleaveOdd32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorDeinterleaveOdd64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorDeinterleaveOddLower8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorDeinterleaveOddLower16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorDeinterleaveOddLower32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorEor>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorEqual8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorEqual16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorEqual32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorEqual64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorEqual128>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorExtract>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorExtractLower>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorGreaterS8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorGreaterS16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorGreaterS32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorGreaterS64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorHalvingAddS8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorHalvingAddS16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorHalvingAddS32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorHalvingAddU8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorHalvingAddU16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorHalvingAddU32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorHalvingSubS8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorHalvingSubS16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorHalvingSubS32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorHalvingSubU8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorHalvingSubU16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorHalvingSubU32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorInterleaveLower8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorInterleaveLower16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorInterleaveLower32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorInterleaveLower64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorInterleaveUpper8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorInterleaveUpper16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorInterleaveUpper32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorInterleaveUpper64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorLogicalShiftLeft8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorLogicalShiftLeft16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorLogicalShiftLeft32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorLogicalShiftLeft64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorLogicalShiftRight8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorLogicalShiftRight16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorLogicalShiftRight32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorLogicalShiftRight64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorLogicalVShift8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorLogicalVShift16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorLogicalVShift32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorLogicalVShift64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorMaxS8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorMaxS16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorMaxS32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorMaxS64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorMaxU8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorMaxU16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorMaxU32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorMaxU64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorMinS8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorMinS16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorMinS32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorMinS64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorMinU8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorMinU16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorMinU32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorMinU64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorMultiply8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorMultiply16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorMultiply32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorMultiply64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorMultiplySignedWiden8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorMultiplySignedWiden16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorMultiplySignedWiden32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorMultiplyUnsignedWiden8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorMultiplyUnsignedWiden16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorMultiplyUnsignedWiden32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorNarrow16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorNarrow32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorNarrow64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorNot>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorOr>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorPairedAddLower8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorPairedAddLower16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorPairedAddLower32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorPairedAddSignedWiden8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorPairedAddSignedWiden16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorPairedAddSignedWiden32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorPairedAddUnsignedWiden8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorPairedAddUnsignedWiden16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorPairedAddUnsignedWiden32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorPairedAdd8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorPairedAdd16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorPairedAdd32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorPairedAdd64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorPairedMaxS8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorPairedMaxS16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorPairedMaxS32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorPairedMaxU8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorPairedMaxU16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorPairedMaxU32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorPairedMinS8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorPairedMinS16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorPairedMinS32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorPairedMinU8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorPairedMinU16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorPairedMinU32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorPairedMaxLowerS8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorPairedMaxLowerS16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorPairedMaxLowerS32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorPairedMaxLowerU8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorPairedMaxLowerU16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorPairedMaxLowerU32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorPairedMinLowerS8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorPairedMinLowerS16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorPairedMinLowerS32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorPairedMinLowerU8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorPairedMinLowerU16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorPairedMinLowerU32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorPolynomialMultiply8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorPolynomialMultiplyLong8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorPolynomialMultiplyLong64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorPopulationCount>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorReverseBits>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorReverseElementsInHalfGroups8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorReverseElementsInWordGroups8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorReverseElementsInWordGroups16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorReverseElementsInLongGroups8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorReverseElementsInLongGroups16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorReverseElementsInLongGroups32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorReduceAdd8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorReduceAdd16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorReduceAdd32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorReduceAdd64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorRotateWholeVectorRight>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorRoundingHalvingAddS8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorRoundingHalvingAddS16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorRoundingHalvingAddS32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorRoundingHalvingAddU8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorRoundingHalvingAddU16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorRoundingHalvingAddU32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorRoundingShiftLeftS8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorRoundingShiftLeftS16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorRoundingShiftLeftS32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorRoundingShiftLeftS64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorRoundingShiftLeftU8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorRoundingShiftLeftU16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorRoundingShiftLeftU32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorRoundingShiftLeftU64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSignExtend8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSignExtend16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSignExtend32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSignExtend64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSignedAbsoluteDifference8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSignedAbsoluteDifference16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSignedAbsoluteDifference32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSignedMultiply16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSignedMultiply32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedAbs8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedAbs16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedAbs32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedAbs64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedAccumulateUnsigned8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedAccumulateUnsigned16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedAccumulateUnsigned32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedAccumulateUnsigned64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedDoublingMultiplyHigh16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedDoublingMultiplyHigh32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedDoublingMultiplyHighRounding16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedDoublingMultiplyHighRounding32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedDoublingMultiplyLong16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedDoublingMultiplyLong32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedNarrowToSigned16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedNarrowToSigned32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedNarrowToSigned64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedNarrowToUnsigned16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedNarrowToUnsigned32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedNarrowToUnsigned64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedNeg8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedNeg16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedNeg32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedNeg64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedShiftLeft8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedShiftLeft16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedShiftLeft32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedShiftLeft64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedShiftLeftUnsigned8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedShiftLeftUnsigned16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedShiftLeftUnsigned32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedShiftLeftUnsigned64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSub8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSub16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSub32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSub64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorTable>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorTableLookup64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorTableLookup128>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorTranspose8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorTranspose16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorTranspose32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorTranspose64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedAbsoluteDifference8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedAbsoluteDifference16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedAbsoluteDifference32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedMultiply16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedMultiply32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedRecipEstimate>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedRecipSqrtEstimate>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedSaturatedAccumulateSigned8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedSaturatedAccumulateSigned16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedSaturatedAccumulateSigned32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedSaturatedAccumulateSigned64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedSaturatedNarrow16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedSaturatedNarrow32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedSaturatedNarrow64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedSaturatedShiftLeft8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedSaturatedShiftLeft16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedSaturatedShiftLeft32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedSaturatedShiftLeft64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorZeroExtend8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorZeroExtend16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorZeroExtend32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorZeroExtend64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorZeroUpper>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::ZeroVector>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

// Vector saturation
template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedAdd8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedAdd16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedAdd32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedAdd64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedSub8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedSub16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedSub32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorSignedSaturatedSub64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedSaturatedAdd8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedSaturatedAdd16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedSaturatedAdd32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedSaturatedAdd64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedSaturatedSub8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedSaturatedSub16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedSaturatedSub32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::VectorUnsignedSaturatedSub64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

// Vector VFP
template<>
void EmitIR<IR::Opcode::FPVectorAbs16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorAbs32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorAbs64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorAdd32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorAdd64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorDiv32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorDiv64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorEqual16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorEqual32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorEqual64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorFromHalf32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorFromSignedFixed32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorFromSignedFixed64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorFromUnsignedFixed32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorFromUnsignedFixed64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorGreater32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorGreater64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorGreaterEqual32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorGreaterEqual64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorMax32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorMax64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorMaxNumeric32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorMaxNumeric64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorMin32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorMin64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorMinNumeric32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorMinNumeric64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorMul32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorMul64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorMulAdd16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorMulAdd32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorMulAdd64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorMulX32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorMulX64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorNeg16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorNeg32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorNeg64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorPairedAdd32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorPairedAdd64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorPairedAddLower32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorPairedAddLower64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorRecipEstimate16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorRecipEstimate32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorRecipEstimate64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorRecipStepFused16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorRecipStepFused32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorRecipStepFused64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorRoundInt16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorRoundInt32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorRoundInt64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorRSqrtEstimate16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorRSqrtEstimate32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorRSqrtEstimate64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorRSqrtStepFused16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorRSqrtStepFused32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorRSqrtStepFused64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorSqrt32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorSqrt64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorSub32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorSub64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorToHalf32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorToSignedFixed16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorToSignedFixed32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorToSignedFixed64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorToUnsignedFixed16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorToUnsignedFixed32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::FPVectorToUnsignedFixed64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

}  // namespace Dynarmic::Backend::RV64
