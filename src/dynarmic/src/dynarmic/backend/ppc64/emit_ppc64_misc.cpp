// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
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
void EmitIR<IR::Opcode::SignedSaturatedAddWithFlag32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::SignedSaturatedSubWithFlag32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::SignedSaturation>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::UnsignedSaturation>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::SignedSaturatedAdd8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::SignedSaturatedAdd16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::SignedSaturatedAdd32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::SignedSaturatedAdd64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::SignedSaturatedDoublingMultiplyReturnHigh16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::SignedSaturatedDoublingMultiplyReturnHigh32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::SignedSaturatedSub8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::SignedSaturatedSub16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::SignedSaturatedSub32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::SignedSaturatedSub64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::UnsignedSaturatedAdd8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::UnsignedSaturatedAdd16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::UnsignedSaturatedAdd32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::UnsignedSaturatedAdd64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::UnsignedSaturatedSub8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::UnsignedSaturatedSub16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::UnsignedSaturatedSub32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::UnsignedSaturatedSub64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

// Packed
template<>
void EmitIR<IR::Opcode::PackedAddU8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::PackedAddS8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::PackedSubU8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::PackedSubS8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::PackedAddU16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::PackedAddS16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::PackedSubU16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::PackedSubS16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::PackedAddSubU16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::PackedAddSubS16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::PackedSubAddU16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::PackedSubAddS16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::PackedHalvingAddU8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::PackedHalvingAddS8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::PackedHalvingSubU8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::PackedHalvingSubS8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::PackedHalvingAddU16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::PackedHalvingAddS16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::PackedHalvingSubU16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::PackedHalvingSubS16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::PackedHalvingAddSubU16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::PackedHalvingAddSubS16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::PackedHalvingSubAddU16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::PackedHalvingSubAddS16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::PackedSaturatedAddU8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::PackedSaturatedAddS8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::PackedSaturatedSubU8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::PackedSaturatedSubS8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::PackedSaturatedAddU16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::PackedSaturatedAddS16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::PackedSaturatedSubU16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::PackedSaturatedSubS16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::PackedAbsDiffSumU8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::PackedSelect>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

// Crypto
template<>
void EmitIR<IR::Opcode::CRC32Castagnoli8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::CRC32Castagnoli16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::CRC32Castagnoli32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::CRC32Castagnoli64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::CRC32ISO8>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::CRC32ISO16>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::CRC32ISO32>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::CRC32ISO64>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::AESDecryptSingleRound>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::AESEncryptSingleRound>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::AESInverseMixColumns>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::AESMixColumns>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::SM4AccessSubstitutionBox>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::SHA256Hash>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::SHA256MessageSchedule0>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::SHA256MessageSchedule1>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

}  // namespace Dynarmic::Backend::RV64
