// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// Encoding reference: Mesa NAK sm70_encode.rs (MIT), Copyright 2022 Collabora, Ltd.

#include "shader_recompiler/frontend/ampere/visitor.h"
#include "shader_recompiler/frontend/maxwell/translate/impl/common_funcs.h"

namespace Shader::Ampere {

void Visitor::LOP3() {
    const u32 dest = Dst();
    const Operand a = Source(0, false);
    const Operand b = Source(1, false);
    const Operand c = Source(2, false);
    const u32 lut = Take(72, 8);
    // Bit 80 selects a predicate result of the logic operation; 81..83 is its destination and
    // 87..90 the (always false) predicate input. None of them is used by plain bitwise logic.
    Require(Take(80, 1) == 0, "native LOP3 predicate result");
    const u32 pred_dest = Take(81, 3);
    Require(Take(87, 4) == 0xf, "native LOP3 predicate input");
    const IR::U32 op_a{Value(a)};
    const IR::U32 op_b{Value(b)};
    const IR::U32 op_c{Value(c)};
    const IR::U32 result{Maxwell::ApplyLUT(ir, op_a, op_b, op_c, lut)};
    WriteDst(dest, result);
    if (pred_dest != 7) {
        SetPred(pred_dest, ir.INotEqual(result, ir.Imm32(0U)));
    }
}

void Visitor::SHF() {
    const u32 dest = Dst();
    const Operand low = Source(0, false);
    const Operand shift = Source(1, false);
    const Operand high = Source(2, false);
    Require(Take(72, 1) == 0, "native funnel shift negate");
    const u32 type = Take(73, 2);
    const bool wrap = Take(75, 1) != 0;
    const bool right = Take(76, 1) != 0;
    const bool dst_high = Take(80, 1) != 0;
    // Types: 1 = U32 (shift clamped at 32, as in the Maxwell encoding), 2 = S64, 3 = U64.
    Require(type >= 1, "native 64-bit funnel shift type");
    const bool is_signed = type == 2;

    const bool high_zero = high.kind == Operand::Kind::Reg && high.reg == 255;
    const bool low_zero = low.kind == Operand::Kind::Reg && low.reg == 255;
    if (shift.kind == Operand::Kind::Imm && !wrap && high_zero && !right && !dst_high) {
        // Plain left shift of the low word.
        WriteDst(dest, shift.imm >= 32 ? ir.Imm32(0U)
                                : IR::U32{ir.ShiftLeftLogical(Value(low), ir.Imm32(shift.imm))});
        return;
    }
    if (shift.kind == Operand::Kind::Imm && !wrap && low_zero && right && dst_high) {
        // Right shift of the high word.
        const IR::U32 value{Value(high)};
        if (is_signed) {
            WriteDst(dest, IR::U32{ir.ShiftRightArithmetic(value, ir.Imm32((std::min)(shift.imm, 31U)))});
        } else {
            WriteDst(dest, shift.imm >= 32 ? ir.Imm32(0U)
                                    : IR::U32{ir.ShiftRightLogical(value, ir.Imm32(shift.imm))});
        }
        return;
    }
    const IR::U32 low_bits{Value(low)};
    const IR::U32 high_bits{Value(high)};
    const IR::U64 packed{ir.PackUint2x32(ir.CompositeConstruct(low_bits, high_bits))};
    const IR::U32 amount{Value(shift)};
    const IR::U32 safe_shift{wrap ? ir.BitwiseAnd(amount, ir.Imm32(31U)) : ir.UMin(amount, ir.Imm32(32U))};
    IR::U64 shifted;
    if (!right) {
        shifted = IR::U64{ir.ShiftLeftLogical(packed, safe_shift)};
    } else if (is_signed) {
        shifted = IR::U64{ir.ShiftRightArithmetic(packed, safe_shift)};
    } else {
        shifted = IR::U64{ir.ShiftRightLogical(packed, safe_shift)};
    }
    const IR::Value unpacked{ir.UnpackUint2x32(shifted)};
    WriteDst(dest, IR::U32{ir.CompositeExtract(unpacked, dst_high ? 1 : 0)});
}

void Visitor::LEA() {
    const u32 dest = Dst();
    const Operand a = Source(0, false);
    const Operand b = Source(1, true);
    const u32 high_reg = Take(64, 8);
    Require(Take(72, 1) == 0, "native LEA intermediate modifier");
    const bool sign_extend = Take(73, 1) != 0;
    const bool carry_in = Take(74, 1) != 0;
    const u32 shift = Take(75, 5);
    const bool dst_high = Take(80, 1) != 0;
    const u32 carry_out = Take(81, 3);
    const u32 carry_index = Take(87, 3);
    const bool carry_negated = Take(90, 1) != 0;
    Require(!b.abs, "native LEA absolute addend");
    Require(carry_in || (carry_index == 7 && carry_negated), "native LEA carry predicate");
    Require(carry_out == 7 || !dst_high, "native LEA high word carry output");
    if (!dst_high && (carry_in || carry_out != 7)) {
        // (a << shift) + b (+ carry) as a wide sum: dst is the low word, the carry output is sum >= 2^32.
        // With .X the negate flag of the addend is a bitwise inversion.
        Require(!sign_extend, "native LEA sign extension without high word");
        const auto widen = [&](const IR::U32& x) {
            return IR::U64{ir.PackUint2x32(ir.CompositeConstruct(x, ir.Imm32(0U)))};
        };
        IR::U32 addend{Value(b)};
        if (b.negate) {
            addend = carry_in ? IR::U32{ir.BitwiseNot(addend)} : IR::U32{ir.INeg(addend)};
        }
        // The bits shifted out of the low word belong to the LEA.HI half, so the carry only sees the
        // truncated 32 bit shift.
        const IR::U32 shifted{shift == 0 ? Value(a) : IR::U32{ir.ShiftLeftLogical(Value(a), ir.Imm32(shift))}};
        IR::U64 sum{ir.IAdd(widen(shifted), widen(addend))};
        if (carry_in) {
            sum = IR::U64{ir.IAdd(sum, widen(IR::U32{ir.Select(PredIn(carry_index, carry_negated),
                                                              ir.Imm32(1U), ir.Imm32(0U))}))};
        }
        const IR::Value words{ir.UnpackUint2x32(sum)};
        WriteDst(dest, IR::U32{ir.CompositeExtract(words, 0)});
        if (carry_out != 7) {
            PredOut(carry_out, ir.INotEqual(IR::U32{ir.CompositeExtract(words, 1)}, ir.Imm32(0U)));
        }
        return;
    }

    const IR::U32 low{Value(a)};
    IR::U32 addend{Value(b)};
    if (b.negate) {
        addend = ir.INeg(addend);
    }
    if (!dst_high) {
        Require(!sign_extend, "native LEA sign extension without high word");
        WriteDst(dest, ir.IAdd(shift == 0 ? low : IR::U32{ir.ShiftLeftLogical(low, ir.Imm32(shift))}, addend));
        return;
    }
    // LEA.HI: addend + high32(((high : low) << shift)), optionally sign-extending `low` into high.
    IR::U32 high;
    if (sign_extend) {
        Require(high_reg == 255, "native LEA sign extension with explicit high word");
        high = IR::U32{ir.ShiftRightArithmetic(low, ir.Imm32(31U))};
    } else {
        high = X(high_reg);
    }
    IR::U32 result;
    if (shift == 0) {
        result = high;
    } else {
        result = IR::U32{ir.BitwiseOr(ir.ShiftLeftLogical(high, ir.Imm32(shift)),
                                      ir.ShiftRightLogical(low, ir.Imm32(32 - shift)))};
    }
    result = ir.IAdd(result, addend);
    if (carry_in) {
        const IR::U1 carry{Pred(carry_index, carry_negated)};
        result = ir.IAdd(result, IR::U32{ir.Select(carry, ir.Imm32(1U), ir.Imm32(0U))});
    }
    WriteDst(dest, result);
}

void Visitor::IMNMX() {
    const u32 dest = Take(16, 8);
    const Operand a = Source(0, false);
    const Operand b = Source(1, false);
    Require(Take(72, 1) == 0, "native integer min/max negate");
    const bool is_signed = Take(73, 1) != 0;
    const u32 pred_index = Take(87, 3);
    const bool pred_negated = Take(90, 1) != 0;
    const IR::U32 op_a{Value(a)};
    const IR::U32 op_b{Value(b)};
    const IR::U32 min{is_signed ? ir.SMin(op_a, op_b) : ir.UMin(op_a, op_b)};
    const IR::U32 max{is_signed ? ir.SMax(op_a, op_b) : ir.UMax(op_a, op_b)};
    X(dest, IR::U32{ir.Select(Pred(pred_index, pred_negated), min, max)});
}

void Visitor::PRMT() {
    const u32 dest = Take(16, 8);
    const Operand a = Source(0, false);
    const Operand selector = Source(1, false);
    const u32 b_reg = Take(64, 8);
    Require(Take(72, 3) == 0, "native permute mode");
    Require(Take(75, 1) == 0, "native permute modifier");
    Require(selector.kind == Operand::Kind::Imm, "native register permute selector");
    const IR::U32 low{Value(a)};
    const IR::U32 high{X(b_reg)};
    IR::U32 result{ir.Imm32(0U)};
    for (u32 byte = 0; byte < 4; ++byte) {
        const u32 nibble = (selector.imm >> (4 * byte)) & 0xf;
        const IR::U32& source = (nibble & 4) ? high : low;
        const u32 offset = (nibble & 3) * 8;
        IR::U32 value;
        if (nibble & 8) {
            // Replicate the sign bit of the selected byte.
            const IR::U32 sign{ir.BitFieldExtract(source, ir.Imm32(offset + 7), ir.Imm32(1U))};
            value = ir.IMul(sign, ir.Imm32(0xffU));
        } else {
            value = ir.BitFieldExtract(source, ir.Imm32(offset), ir.Imm32(8U));
        }
        result = ir.BitFieldInsert(result, value, ir.Imm32(byte * 8), ir.Imm32(8U));
    }
    X(dest, result);
}

void Visitor::IMAD() {
    const u32 dest = Dst();
    const Operand a = Source(0, false);
    const Operand b = Source(1, true);
    const Operand c = Source(2, true);
    const bool neg_a = Take(72, 1) != 0;
    Ignore(73, 1); // signedness only matters for the high / wide forms
    // The absolute-value positions of the second and third source hold the carry-in selector.
    // The absolute-value position of the third source marks the carry-in (.X) form: the carry
    // predicate is at 87..90 and the negate flag of the addend is a bitwise inversion.
    // The marker is bit 74 whichever operand occupies that slot; no other absolute flag may be set.
    const bool extended = Take(74, 1) != 0;
    Require(static_cast<u32>(b.abs) + static_cast<u32>(c.abs) == (extended ? 1U : 0U), "native IMAD carry-in");
    Require(Take(81, 3) == 7, "native IMAD carry output");
    const u32 carry_index = Take(87, 3);
    const bool carry_negated = Take(90, 1) != 0;
    Require(extended || (carry_index == 7 && carry_negated), "native IMAD carry input");
    Require(!extended || (!neg_a && !b.negate), "native IMAD carry-in with negated factor");
    IR::U32 op_a{Value(a)};
    IR::U32 op_b{Value(b)};
    IR::U32 op_c{Value(c)};
    if (neg_a) op_a = ir.INeg(op_a);
    if (b.negate) op_b = ir.INeg(op_b);
    if (c.negate) op_c = extended ? IR::U32{ir.BitwiseNot(op_c)} : IR::U32{ir.INeg(op_c)};
    IR::U32 sum{ir.IAdd(ir.IMul(op_a, op_b), op_c)};
    if (extended) {
        sum = ir.IAdd(sum, IR::U32{ir.Select(PredIn(carry_index, carry_negated), ir.Imm32(1U), ir.Imm32(0U))});
    }
    WriteDst(dest, sum);
}

// High 32 bits of the 64-bit product, built from 16-bit limbs.
IR::U32 Visitor::MultiplyHigh(const IR::U32& op_a, const IR::U32& op_b, bool is_signed) {
    const IR::U32 mask{ir.Imm32(0xffffU)};
    const IR::U32 sixteen{ir.Imm32(16U)};
    const IR::U32 al{ir.BitwiseAnd(op_a, mask)};
    const IR::U32 ah{ir.ShiftRightLogical(op_a, sixteen)};
    const IR::U32 bl{ir.BitwiseAnd(op_b, mask)};
    const IR::U32 bh{ir.ShiftRightLogical(op_b, sixteen)};
    const IR::U32 t{ir.IMul(al, bl)};
    const IR::U32 u{ir.IAdd(ir.IMul(ah, bl), ir.ShiftRightLogical(t, sixteen))};
    const IR::U32 v{ir.IAdd(ir.IMul(al, bh), ir.BitwiseAnd(u, mask))};
    IR::U32 hi{ir.IAdd(ir.IAdd(ir.IMul(ah, bh), ir.ShiftRightLogical(u, sixteen)),
                       ir.ShiftRightLogical(v, sixteen))};
    if (is_signed) {
        const IR::U32 zero{ir.Imm32(0U)};
        const IR::U32 fix_a{IR::U32{ir.Select(ir.ILessThan(op_a, zero, true), op_b, zero)}};
        const IR::U32 fix_b{IR::U32{ir.Select(ir.ILessThan(op_b, zero, true), op_a, zero)}};
        hi = ir.ISub(ir.ISub(hi, fix_a), fix_b);
    }
    return hi;
}

// IMAD.HI: high 32 bits of the 64-bit product plus the third source.
void Visitor::IMAD_HI() {
    const u32 dest = Take(16, 8);
    const Operand a = Source(0, false);
    const Operand b = Source(1, false);
    const Operand c = Source(2, false);
    const bool is_signed = Take(73, 1) != 0;
    Require(Take(81, 3) == 7, "native IMAD carry output");
    Require(Take(87, 4) == 0xf, "native IMAD carry input");
    const IR::U32 op_a{Value(a)};
    const IR::U32 op_b{Value(b)};
    const IR::U32 op_c{Value(c)};
    const IR::U32 hi{MultiplyHigh(op_a, op_b, is_signed)};
    X(dest, ir.IAdd(hi, op_c));
}

// IMAD.WIDE: 64-bit product of two 32-bit sources plus a 64-bit addend, written to a register pair.
void Visitor::IMAD_WIDE() {
    const u32 dest = Dst();
    const Operand a = Source(0, false);
    const Operand b = Source(1, true);
    const Operand c = Source(2, true);
    Require(Take(72, 1) == 0, "native wide multiply negated source");
    const bool is_signed = Take(73, 1) != 0;
    Require(!b.abs && !c.abs && !b.negate && !c.negate, "native wide multiply source modifiers");
    Require(Take(81, 3) == 7, "native wide multiply carry output");
    Require(Take(87, 4) == 0xf, "native wide multiply carry input");
    const u32 limit = uniform_op ? 62 : 254;
    Require(dest % 2 == 0 && dest < limit, "native wide multiply destination");
    // The addend is a register pair or a 64-bit constant-buffer value.
    IR::U32 c_low{Value(c)};
    IR::U32 c_high;
    switch (c.kind) {
    case Operand::Kind::Reg:
    case Operand::Kind::Ureg:
        if (c.reg == (uniform_op ? 63U : 255U)) {
            c_high = ir.Imm32(0U); // RZ / URZ: a zero addend
            break;
        }
        Require(c.reg % 2 == 0 && c.reg < limit, "native wide multiply addend");
        c_high = c.kind == Operand::Kind::Reg ? X(c.reg + 1) : UR(c.reg + 1);
        break;
    case Operand::Kind::Cbuf:
        c_high = Cbuf(c.cbuf_index, c.cbuf_offset + 4);
        break;
    default:
        Unsupported("native wide multiply addend");
    }
    const IR::U32 op_a{Value(a)};
    const IR::U32 op_b{Value(b)};
    const IR::U32 low{ir.IMul(op_a, op_b)};
    const IR::U32 high{MultiplyHigh(op_a, op_b, is_signed)};
    const IR::U64 product{ir.PackUint2x32(ir.CompositeConstruct(low, high))};
    const IR::U64 addend{ir.PackUint2x32(ir.CompositeConstruct(c_low, c_high))};
    const IR::Value sum{ir.UnpackUint2x32(ir.IAdd(product, addend))};
    WriteDst(dest, IR::U32{ir.CompositeExtract(sum, 0)});
    WriteDst(dest + 1, IR::U32{ir.CompositeExtract(sum, 1)});
}

// IABS: absolute value of a signed integer.
void Visitor::IABS() {
    const u32 dest = Dst();
    const Operand source = Source(1, false);
    WriteDst(dest, IR::U32{ir.IAbs(Value(source))});
}

// SGXT: keep the low `bits` bits of a, sign extended or zero extended (NAK's reference semantics:
// bits of 32 or more leave the value alone and zero bits give zero).
void Visitor::SGXT() {
    const u32 dest = Dst();
    const Operand a = Source(0, false);
    const Operand bits = Source(1, false);
    const bool is_signed = Take(73, 1) != 0;
    const IR::U32 value{Value(a)};
    const IR::U32 count{Value(bits)};
    const IR::U32 shift{ir.BitwiseAnd(ir.ISub(ir.Imm32(32U), count), ir.Imm32(31U))};
    const IR::U32 shifted{ir.ShiftLeftLogical(value, shift)};
    const IR::U32 extended{is_signed ? IR::U32{ir.ShiftRightArithmetic(shifted, shift)}
                                     : IR::U32{ir.ShiftRightLogical(shifted, shift)}};
    const IR::U32 narrow{ir.Select(ir.IEqual(count, ir.Imm32(0U)), ir.Imm32(0U), extended)};
    WriteDst(dest, IR::U32{ir.Select(ir.IGreaterThanEqual(count, ir.Imm32(32U), false), value, narrow)});
}

// FLO: index of the highest set bit (of the leading non-sign bit for signed negative inputs), or the
// number of leading bits with return_shift_amount; -1 / 32 for an input with none. Bit 63 inverts the
// source first. Semantics follow NAK's reference fold.
void Visitor::FLO() {
    const u32 dest = Dst();
    const Operand source = Source(1, false);
    const bool not_source = Take(63, 1) != 0;
    const bool is_signed = Take(73, 1) != 0;
    const bool shift_amount = Take(74, 1) != 0;
    Require(Take(81, 3) == 7, "native find leading one predicate output");
    IR::U32 value{Value(source)};
    if (not_source) {
        value = ir.BitwiseNot(value);
    }
    const IR::U32 msb{is_signed ? ir.FindSMsb(value) : ir.FindUMsb(value)};
    WriteDst(dest, shift_amount ? IR::U32{ir.ISub(ir.Imm32(31U), msb)} : msb);
}

// POPC: number of set bits; bit 63 inverts the source first.
void Visitor::POPC() {
    const u32 dest = Dst();
    const Operand source = Source(1, false);
    const bool not_source = Take(63, 1) != 0;
    IR::U32 value{Value(source)};
    if (not_source) {
        value = ir.BitwiseNot(value);
    }
    WriteDst(dest, ir.BitCount(value));
}

// IDP.2A.LO.U16.U8: accumulator plus the two 16-bit halves of A times the two low bytes of B, as the
// reference lowering decodes it (the only encoding seen: bit 76 set, nothing else in the upper word).
void Visitor::IDP() {
    const u32 dest = Dst();
    const u32 a = Take(24, 8);
    const u32 b = Take(32, 8);
    const u32 c = Take(64, 8);
    Require(Take(76, 1) == 1 && Take(72, 4) == 0, "native packed dot-product mode");
    const IR::U32 word_a{X(a)};
    const IR::U32 word_b{X(b)};
    const IR::U32 low_a{ir.BitwiseAnd(word_a, ir.Imm32(0xffffU))};
    const IR::U32 high_a{ir.ShiftRightLogical(word_a, ir.Imm32(16U))};
    const IR::U32 byte_0{ir.BitwiseAnd(word_b, ir.Imm32(0xffU))};
    const IR::U32 byte_1{ir.BitwiseAnd(ir.ShiftRightLogical(word_b, ir.Imm32(8U)), ir.Imm32(0xffU))};
    const IR::U32 sum{ir.IAdd(ir.IMul(low_a, byte_0), ir.IMul(high_a, byte_1))};
    WriteDst(dest, ir.IAdd(sum, X(c)));
}

} // namespace Shader::Ampere
