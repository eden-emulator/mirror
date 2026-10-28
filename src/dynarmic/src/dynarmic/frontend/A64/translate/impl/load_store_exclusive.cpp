/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include <optional>

#include "dynarmic/frontend/A64/translate/impl/impl.h"
#include "dynarmic/ir/terminal.h"

namespace Dynarmic::A64 {

bool TranslatorVisitor::CAS(Imm<2> sz, bool L, Reg Rs, bool o0, Reg Rn, Reg Rt) {
    const size_t bits = 8U << sz.ZeroExtend<size_t>();
    const size_t reg_bits = bits == 64 ? 64 : 32;
    const IR::U64 address = Rn == Reg::SP ? IR::U64{SP(64)} : IR::U64{X(64, Rn)};
    const IR::U64 original_expected = X(64, Rs);
    const IR::U32U64 expected = ZeroExtend(X(bits, Rs), reg_bits);
    const IR::U32U64 desired = ZeroExtend(X(bits, Rt), reg_bits);
    const auto flags = ir.GetNZCVRaw();
    const auto access = L || o0 ? IR::AccType::ORDERED : IR::AccType::ATOMIC;
    const auto observed = ZeroExtend(ExclusiveMem(address, bits / 8, access), reg_bits);

    ir.SetNZCV(ir.NZCVFrom(ir.SubWithCarry(observed, expected, ir.Imm1(1))));
    const auto replacement = ir.ConditionalSelect(IR::Cond::EQ, desired, observed);
    ir.SetNZCVRaw(flags);
    // A failed comparison may atomically write back the observed value.
    IR::UAny store_value = replacement;
    if (bits == 8) {
        store_value = ir.LeastSignificantByte(replacement);
    } else if (bits == 16) {
        store_value = ir.LeastSignificantHalf(replacement);
    }
    const auto status = ExclusiveMem(address, bits / 8, access, store_value);
    ir.SetNZCV(ir.NZCVFrom(ir.SubWithCarry(status, ir.Imm32(0), ir.Imm1(1))));
    X(64, Rs, ir.ConditionalSelect(IR::Cond::EQ, ir.ZeroExtendToLong(observed), original_expected));
    ir.SetNZCVRaw(flags);
    ir.ClearExclusive();

    // A store-exclusive failure must retry without changing the expected value.
    ir.SetCheckBit(ir.IsZero(status));
    ir.SetTerm(IR::Term::CheckBit{
        IR::Term::LinkBlock{ir.current_location->AdvancePC(4)},
        IR::Term::LinkBlock{*ir.current_location}});
    return false;
}

bool TranslatorVisitor::LDAPR(Imm<2> size, Reg Rn, Reg Rt) {
    // LDAR provides ordering at least as strong as an RCpc acquire load.
    return LDAR(size, Rn, Rt);
}

enum class AtomicRegisterOperation { Add, Set, Clear, Swap };

static bool AtomicRegisterOperationImpl(TranslatorVisitor& v, size_t bits, bool A, bool R,
                                        Reg Rs, Reg Rn, Reg Rt, AtomicRegisterOperation operation) {
    const size_t reg_bits = bits == 64 ? 64 : 32;
    const IR::U64 address = Rn == Reg::SP ? IR::U64{v.SP(64)} : IR::U64{v.X(64, Rn)};
    const IR::U64 original_result = v.X(64, Rt);
    const IR::U32U64 operand = v.ZeroExtend(v.X(bits, Rs), reg_bits);
    const auto flags = v.ir.GetNZCVRaw();
    const auto access = A || R ? IR::AccType::ORDERED : IR::AccType::ATOMIC;
    const auto observed = v.ZeroExtend(v.ExclusiveMem(address, bits / 8, access), reg_bits);
    auto replacement = operand;
    switch (operation) {
    case AtomicRegisterOperation::Add:
        replacement = v.ir.Add(observed, operand);
        break;
    case AtomicRegisterOperation::Set:
        replacement = v.ir.Or(observed, operand);
        break;
    case AtomicRegisterOperation::Clear:
        replacement = v.ir.And(observed, v.ir.Not(operand));
        break;
    case AtomicRegisterOperation::Swap:
        break;
    }
    IR::UAny store_value = replacement;
    if (bits == 8) {
        store_value = v.ir.LeastSignificantByte(replacement);
    } else if (bits == 16) {
        store_value = v.ir.LeastSignificantHalf(replacement);
    }
    const auto status = v.ExclusiveMem(address, bits / 8, access, store_value);
    v.ir.SetNZCV(v.ir.NZCVFrom(v.ir.SubWithCarry(status, v.ir.Imm32(0), v.ir.Imm1(1))));
    v.X(64, Rt, v.ir.ConditionalSelect(IR::Cond::EQ, v.ir.ZeroExtendToLong(observed), original_result));
    v.ir.SetNZCVRaw(flags);
    v.ir.ClearExclusive();
    v.ir.SetCheckBit(v.ir.IsZero(status));
    v.ir.SetTerm(IR::Term::CheckBit{
        IR::Term::LinkBlock{v.ir.current_location->AdvancePC(4)},
        IR::Term::LinkBlock{*v.ir.current_location}});
    return false;
}

bool TranslatorVisitor::LDADD(bool sz, bool A, bool R, Reg Rs, Reg Rn, Reg Rt) {
    return AtomicRegisterOperationImpl(*this, sz ? 64 : 32, A, R, Rs, Rn, Rt, AtomicRegisterOperation::Add);
}

bool TranslatorVisitor::LDCLR(bool sz, bool A, bool R, Reg Rs, Reg Rn, Reg Rt) {
    return AtomicRegisterOperationImpl(*this, sz ? 64 : 32, A, R, Rs, Rn, Rt, AtomicRegisterOperation::Clear);
}

bool TranslatorVisitor::LDSET(bool sz, bool A, bool R, Reg Rs, Reg Rn, Reg Rt) {
    return AtomicRegisterOperationImpl(*this, sz ? 64 : 32, A, R, Rs, Rn, Rt, AtomicRegisterOperation::Set);
}

bool TranslatorVisitor::SWPB(bool A, bool R, Reg Rs, Reg Rn, Reg Rt) {
    return AtomicRegisterOperationImpl(*this, 8, A, R, Rs, Rn, Rt, AtomicRegisterOperation::Swap);
}

bool TranslatorVisitor::SWPH(bool A, bool R, Reg Rs, Reg Rn, Reg Rt) {
    return AtomicRegisterOperationImpl(*this, 16, A, R, Rs, Rn, Rt, AtomicRegisterOperation::Swap);
}

bool TranslatorVisitor::SWP(bool sz, bool A, bool R, Reg Rs, Reg Rn, Reg Rt) {
    return AtomicRegisterOperationImpl(*this, sz ? 64 : 32, A, R, Rs, Rn, Rt, AtomicRegisterOperation::Swap);
}

static bool ExclusiveSharedDecodeAndOperation(TranslatorVisitor& v, bool pair, size_t size, bool L, bool o0, std::optional<Reg> Rs, std::optional<Reg> Rt2, Reg Rn, Reg Rt) {
    // Shared Decode

    const auto acctype = o0 ? IR::AccType::ORDERED : IR::AccType::ATOMIC;
    const auto memop = L ? IR::MemOp::LOAD : IR::MemOp::STORE;
    const size_t elsize = 8 << size;
    const size_t regsize = elsize == 64 ? 64 : 32;
    const size_t datasize = pair ? elsize * 2 : elsize;

    // Operation

    const size_t dbytes = datasize / 8;

    if (memop == IR::MemOp::LOAD && pair && Rt == *Rt2) {
        return v.UnpredictableInstruction();
    } else if (memop == IR::MemOp::STORE && (*Rs == Rt || (pair && *Rs == *Rt2))) {
        if (!v.options.define_unpredictable_behaviour) {
            return v.UnpredictableInstruction();
        }
        // UNPREDICTABLE: The Constraint_NONE case is executed.
    } else if (memop == IR::MemOp::STORE && *Rs == Rn && Rn != Reg::R31) {
        return v.UnpredictableInstruction();
    }

    IR::U64 address;
    if (Rn == Reg::SP) {
        // TODO: Check SP Alignment
        address = v.SP(64);
    } else {
        address = v.X(64, Rn);
    }

    switch (memop) {
    case IR::MemOp::STORE: {
        IR::UAnyU128 data;
        if (pair && elsize == 64) {
            data = v.ir.Pack2x64To1x128(v.X(64, Rt), v.X(64, *Rt2));
        } else if (pair && elsize == 32) {
            data = v.ir.Pack2x32To1x64(v.X(32, Rt), v.X(32, *Rt2));
        } else {
            data = v.X(elsize, Rt);
        }
        const IR::U32 status = v.ExclusiveMem(address, dbytes, acctype, data);
        v.X(32, *Rs, status);
        break;
    }
    case IR::MemOp::LOAD: {
        const IR::UAnyU128 data = v.ExclusiveMem(address, dbytes, acctype);
        if (pair && elsize == 64) {
            v.X(64, Rt, v.ir.VectorGetElement(64, data, 0));
            v.X(64, *Rt2, v.ir.VectorGetElement(64, data, 1));
        } else if (pair && elsize == 32) {
            v.X(32, Rt, v.ir.LeastSignificantWord(data));
            v.X(32, *Rt2, v.ir.MostSignificantWord(data).result);
        } else {
            v.X(regsize, Rt, v.ZeroExtend(data, regsize));
        }
        break;
    }
    default:
        UNREACHABLE();
    }

    return true;
}

bool TranslatorVisitor::STXR(Imm<2> sz, Reg Rs, Reg Rn, Reg Rt) {
    const bool pair = false;
    const size_t size = sz.ZeroExtend<size_t>();
    const bool L = 0;
    const bool o0 = 0;
    return ExclusiveSharedDecodeAndOperation(*this, pair, size, L, o0, Rs, {}, Rn, Rt);
}

bool TranslatorVisitor::STLXR(Imm<2> sz, Reg Rs, Reg Rn, Reg Rt) {
    const bool pair = false;
    const size_t size = sz.ZeroExtend<size_t>();
    const bool L = 0;
    const bool o0 = 1;
    return ExclusiveSharedDecodeAndOperation(*this, pair, size, L, o0, Rs, {}, Rn, Rt);
}

bool TranslatorVisitor::STXP(Imm<1> sz, Reg Rs, Reg Rt2, Reg Rn, Reg Rt) {
    const bool pair = true;
    const size_t size = concatenate(Imm<1>{1}, sz).ZeroExtend<size_t>();
    const bool L = 0;
    const bool o0 = 0;
    return ExclusiveSharedDecodeAndOperation(*this, pair, size, L, o0, Rs, Rt2, Rn, Rt);
}

bool TranslatorVisitor::STLXP(Imm<1> sz, Reg Rs, Reg Rt2, Reg Rn, Reg Rt) {
    const bool pair = true;
    const size_t size = concatenate(Imm<1>{1}, sz).ZeroExtend<size_t>();
    const bool L = 0;
    const bool o0 = 1;
    return ExclusiveSharedDecodeAndOperation(*this, pair, size, L, o0, Rs, Rt2, Rn, Rt);
}

bool TranslatorVisitor::LDXR(Imm<2> sz, Reg Rn, Reg Rt) {
    const bool pair = false;
    const size_t size = sz.ZeroExtend<size_t>();
    const bool L = 1;
    const bool o0 = 0;
    return ExclusiveSharedDecodeAndOperation(*this, pair, size, L, o0, {}, {}, Rn, Rt);
}

bool TranslatorVisitor::LDAXR(Imm<2> sz, Reg Rn, Reg Rt) {
    const bool pair = false;
    const size_t size = sz.ZeroExtend<size_t>();
    const bool L = 1;
    const bool o0 = 1;
    return ExclusiveSharedDecodeAndOperation(*this, pair, size, L, o0, {}, {}, Rn, Rt);
}

bool TranslatorVisitor::LDXP(Imm<1> sz, Reg Rt2, Reg Rn, Reg Rt) {
    const bool pair = true;
    const size_t size = concatenate(Imm<1>{1}, sz).ZeroExtend<size_t>();
    const bool L = 1;
    const bool o0 = 0;
    return ExclusiveSharedDecodeAndOperation(*this, pair, size, L, o0, {}, Rt2, Rn, Rt);
}

bool TranslatorVisitor::LDAXP(Imm<1> sz, Reg Rt2, Reg Rn, Reg Rt) {
    const bool pair = true;
    const size_t size = concatenate(Imm<1>{1}, sz).ZeroExtend<size_t>();
    const bool L = 1;
    const bool o0 = 1;
    return ExclusiveSharedDecodeAndOperation(*this, pair, size, L, o0, {}, Rt2, Rn, Rt);
}

static bool OrderedSharedDecodeAndOperation(TranslatorVisitor& v, size_t size, bool L, bool o0, Reg Rn, Reg Rt) {
    // Shared Decode

    const auto acctype = !o0 ? IR::AccType::LIMITEDORDERED : IR::AccType::ORDERED;
    const auto memop = L ? IR::MemOp::LOAD : IR::MemOp::STORE;
    const size_t elsize = 8 << size;
    const size_t regsize = elsize == 64 ? 64 : 32;
    const size_t datasize = elsize;

    // Operation

    const size_t dbytes = datasize / 8;

    IR::U64 address;
    if (Rn == Reg::SP) {
        // TODO: Check SP Alignment
        address = v.SP(64);
    } else {
        address = v.X(64, Rn);
    }

    switch (memop) {
    case IR::MemOp::STORE: {
        const IR::UAny data = v.X(datasize, Rt);
        v.Mem(address, dbytes, acctype, data);
        break;
    }
    case IR::MemOp::LOAD: {
        const IR::UAny data = v.Mem(address, dbytes, acctype);
        v.X(regsize, Rt, v.ZeroExtend(data, regsize));
        break;
    }
    default:
        UNREACHABLE();
    }

    return true;
}

bool TranslatorVisitor::STLLR(Imm<2> sz, Reg Rn, Reg Rt) {
    const size_t size = sz.ZeroExtend<size_t>();
    const bool L = 0;
    const bool o0 = 0;
    return OrderedSharedDecodeAndOperation(*this, size, L, o0, Rn, Rt);
}

bool TranslatorVisitor::STLR(Imm<2> sz, Reg Rn, Reg Rt) {
    const size_t size = sz.ZeroExtend<size_t>();
    const bool L = 0;
    const bool o0 = 1;
    return OrderedSharedDecodeAndOperation(*this, size, L, o0, Rn, Rt);
}

bool TranslatorVisitor::LDLAR(Imm<2> sz, Reg Rn, Reg Rt) {
    const size_t size = sz.ZeroExtend<size_t>();
    const bool L = 1;
    const bool o0 = 0;
    return OrderedSharedDecodeAndOperation(*this, size, L, o0, Rn, Rt);
}

bool TranslatorVisitor::LDAR(Imm<2> sz, Reg Rn, Reg Rt) {
    const size_t size = sz.ZeroExtend<size_t>();
    const bool L = 1;
    const bool o0 = 1;
    return OrderedSharedDecodeAndOperation(*this, size, L, o0, Rn, Rt);
}

}  // namespace Dynarmic::A64
