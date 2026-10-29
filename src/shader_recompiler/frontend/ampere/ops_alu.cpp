// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// Encoding reference: Mesa NAK sm70_encode.rs (MIT), Copyright 2022 Collabora, Ltd.

#include "shader_recompiler/frontend/ampere/visitor.h"
#include "shader_recompiler/frontend/ir/modifiers.h"
#include "shader_recompiler/frontend/maxwell/translate/impl/common_funcs.h"

namespace Shader::Ampere {
namespace {
IR::FpRounding RoundingOf(u32 mode) {
    switch (mode) {
    case 0:
        return IR::FpRounding::RN;
    case 1:
        return IR::FpRounding::RM;
    case 2:
        return IR::FpRounding::RP;
    default:
        return IR::FpRounding::RZ;
    }
}
} // Anonymous namespace

void Visitor::FADD() {
    const u32 dest = Take(16, 8);
    const Operand a = Source(0, true);
    // FADD keeps its second operand in the wide slot (src1 for forms 1/4/5, src2 for 2/3).
    const u32 form = Form();
    const Operand b = (form == 2 || form == 3 || form == 7) ? Source(2, true) : Source(1, true);
    Ignore(64, 8);
    const bool saturate = Take(77, 1) != 0;
    const u32 rounding = Take(78, 2);
    const bool ftz = Take(80, 1) != 0;
    const IR::FpControl control{
        .no_contraction = true,
        .rounding = RoundingOf(rounding),
        .fmz_mode = ftz ? IR::FmzMode::FTZ : IR::FmzMode::None,
    };
    const IR::F32 op_a{FloatValue(a)};
    const IR::F32 op_b{FloatValue(b)};
    IR::F32 value{ir.FPAdd(op_a, op_b, control)};
    if (saturate) {
        value = IR::F32{ir.FPSaturate(value)};
    }
    F(dest, value);
}

void Visitor::FFMA() {
    const u32 dest = Take(16, 8);
    const Operand a = Source(0, true);
    const Operand b = Source(1, true);
    const Operand c = Source(2, true);
    Require(Take(76, 1) == 0, "native FFMA multiply-zero mode");
    const bool saturate = Take(77, 1) != 0;
    const u32 rounding = Take(78, 2);
    const bool ftz = Take(80, 1) != 0;
    const IR::FpControl control{
        .no_contraction = true,
        .rounding = RoundingOf(rounding),
        .fmz_mode = ftz ? IR::FmzMode::FTZ : IR::FmzMode::None,
    };
    const IR::F32 op_a{FloatValue(a)};
    const IR::F32 op_b{FloatValue(b)};
    const IR::F32 op_c{FloatValue(c)};
    IR::F32 value{ir.FPFma(op_a, op_b, op_c, control)};
    if (saturate) {
        value = IR::F32{ir.FPSaturate(value)};
    }
    F(dest, value);
}

void Visitor::FMNMX() {
    const u32 dest = Take(16, 8);
    const Operand a = Source(0, true);
    const Operand b = Source(1, true);
    Ignore(64, 8);
    const bool ftz = Take(80, 1) != 0;
    const bool propagate_nan = Take(81, 1) != 0; // a NaN operand makes the result NaN
    const u32 pred_index = Take(87, 3);
    const bool pred_negated = Take(90, 1) != 0;
    const IR::FpControl control{
        .no_contraction = false,
        .rounding = IR::FpRounding::DontCare,
        .fmz_mode = ftz ? IR::FmzMode::FTZ : IR::FmzMode::None,
    };
    const IR::F32 op_a{FloatValue(a)};
    const IR::F32 op_b{FloatValue(b)};
    const IR::U1 pred{Pred(pred_index, false)};
    IR::F32 max{ir.FPMax(op_a, op_b, control)};
    IR::F32 min{ir.FPMin(op_a, op_b, control)};
    if (pred_negated) {
        std::swap(min, max);
    }
    const IR::F32 result{ir.Select(pred, min, max)};
    if (propagate_nan) {
        const IR::U1 unordered{ir.LogicalOr(ir.FPIsNan(op_a), ir.FPIsNan(op_b))};
        X(dest, IR::U32{ir.Select(unordered, ir.Imm32(0x7fffffffU), ir.BitCast<IR::U32>(result))});
        return;
    }
    F(dest, result);
}

// SEL (integer) and FSEL (floating, with operand modifiers): dest = pred ? a : b
void Visitor::SEL(bool floating) {
    const u32 dest = Dst();
    const Operand a = Source(0, floating);
    const Operand b = Source(1, floating);
    Ignore(64, 8);
    const u32 pred_index = Take(87, 3);
    const bool pred_negated = Take(90, 1) != 0;
    IR::U32 op_a, op_b;
    if (floating) {
        op_a = ir.BitCast<IR::U32>(FloatValue(a));
        op_b = ir.BitCast<IR::U32>(FloatValue(b));
    } else {
        op_a = Value(a);
        op_b = Value(b);
    }
    const IR::U1 pred{PredIn(pred_index, pred_negated)};
    WriteDst(dest, IR::U32{ir.Select(pred, op_a, op_b)});
}

void Visitor::FSETP() {
    const Operand a = Source(0, true);
    const Operand b = Source(1, true);
    Ignore(64, 8);
    const u32 set_op = Take(74, 2);
    const u32 compare = Take(76, 4);
    const bool ftz = Take(80, 1) != 0;
    const u32 dest_a = Take(81, 3);
    const u32 dest_b = Take(84, 3);
    const u32 accum_index = Take(87, 3);
    const bool accum_negated = Take(90, 1) != 0;
    Require(set_op <= 2, "native predicate combine");
    const IR::FpControl control{
        .no_contraction = false,
        .rounding = IR::FpRounding::DontCare,
        .fmz_mode = ftz ? IR::FmzMode::FTZ : IR::FmzMode::None,
    };
    const IR::F32 op_a{FloatValue(a)};
    const IR::F32 op_b{FloatValue(b)};
    const IR::U1 comparison{Maxwell::FloatingPointCompare(
        ir, op_a, op_b, static_cast<Maxwell::FPCompareOp>(compare), control)};
    const IR::U1 accum{Pred(accum_index, accum_negated)};
    const auto bop = static_cast<Maxwell::BooleanOp>(set_op);
    SetPred(dest_a, Maxwell::PredicateCombine(ir, comparison, accum, bop));
    SetPred(dest_b, Maxwell::PredicateCombine(ir, ir.LogicalNot(comparison), accum, bop));
}

// FSET: float comparison written as a boolean float (1.0 or 0.0), combined with a true accumulator.
void Visitor::FSET() {
    const u32 dest = Dst();
    const Operand a = Source(0, true);
    const Operand b = Source(1, true);
    Ignore(64, 8);
    // The combine operation (bits 74..75, as for FSETP) joins the comparison with the accumulator
    // predicate at 87..90; PT with AND is the plain comparison.
    const u32 set_op = Take(74, 2);
    const u32 compare = Take(76, 4);
    const bool ftz = Take(80, 1) != 0;
    const u32 accum_index = Take(87, 3);
    const bool accum_negated = Take(90, 1) != 0;
    Require(set_op <= 2, "native floating comparison output mode");
    const IR::FpControl control{
        .no_contraction = false,
        .rounding = IR::FpRounding::DontCare,
        .fmz_mode = ftz ? IR::FmzMode::FTZ : IR::FmzMode::None,
    };
    const IR::U1 comparison{Maxwell::FloatingPointCompare(
        ir, FloatValue(a), FloatValue(b), static_cast<Maxwell::FPCompareOp>(compare), control)};
    const IR::U1 combined{Maxwell::PredicateCombine(ir, comparison, Pred(accum_index, accum_negated),
                                                    static_cast<Maxwell::BooleanOp>(set_op))};
    WriteDst(dest, IR::U32{ir.Select(combined, ir.Imm32(0x3f800000U), ir.Imm32(0U))});
}

void Visitor::ISETP() {
    const Operand a = Source(0, false);
    const Operand b = Source(1, false);
    Ignore(64, 4);
    Require(Take(68, 4) == 7, "native extended integer comparison predicate");
    Require(Take(72, 1) == 0, "native extended integer comparison");
    const bool is_signed = Take(73, 1) != 0;
    const u32 set_op = Take(74, 2);
    const u32 compare = Take(76, 3);
    const u32 dest_a = Take(81, 3);
    const u32 dest_b = Take(84, 3);
    const u32 accum_index = Take(87, 3);
    const bool accum_negated = Take(90, 1) != 0;
    Require(set_op <= 2, "native predicate combine");
    const IR::U1 comparison{Maxwell::IntegerCompare(
        ir, Value(a), Value(b), static_cast<Maxwell::CompareOp>(compare), is_signed)};
    const IR::U1 accum{PredIn(accum_index, accum_negated)};
    const auto bop = static_cast<Maxwell::BooleanOp>(set_op);
    PredOut(dest_a, Maxwell::PredicateCombine(ir, comparison, accum, bop));
    PredOut(dest_b, Maxwell::PredicateCombine(ir, ir.LogicalNot(comparison), accum, bop));
}

void Visitor::IADD3() {
    const u32 dest = Dst();
    const Operand a = Source(0, true);
    const Operand b = Source(1, true);
    const Operand c = Source(2, true);
    // Bit 74 (the absolute-value position of the third source) selects the carry-in (.X) variant, in
    // which the negate flags are bitwise inversions instead of integer negation.
    Require(!a.abs && !b.abs, "native IADD3 absolute value");
    const bool extended = c.abs;
    const u32 carry1_index = Take(77, 3);
    const bool carry1_negated = Take(80, 1) != 0;
    const u32 overflow0 = Take(81, 3);
    const u32 overflow1 = Take(84, 3);
    const u32 carry0_index = Take(87, 3);
    const bool carry0_negated = Take(90, 1) != 0;
    const bool no_carry_in = carry0_index == 7 && carry0_negated && carry1_index == 7 && carry1_negated;
    IR::U32 op_a{Value(a)};
    IR::U32 op_b{Value(b)};
    IR::U32 op_c{Value(c)};
    if (!extended && overflow0 == 7 && overflow1 == 7) {
        Require(no_carry_in, "native IADD3 carry predicate");
        if (a.negate) op_a = ir.INeg(op_a);
        if (b.negate) op_b = ir.INeg(op_b);
        if (c.negate) op_c = ir.INeg(op_c);
        const IR::U32 lhs{ir.IAdd(op_a, op_b)};
        WriteDst(dest, ir.IAdd(lhs, op_c));
        return;
    }
    Require(extended || no_carry_in, "native IADD3 carry predicate");
    // Wide sum of three terms plus the carries: dst is the low word, overflow[0] is sum >= 2^32 and
    // overflow[1] is sum >= 2 * 2^32.
    const auto widen = [&](const IR::U32& x) {
        return IR::U64{ir.PackUint2x32(ir.CompositeConstruct(x, ir.Imm32(0U)))};
    };
    const auto term = [&](const Operand& source, const IR::U32& x) -> IR::U64 {
        if (!source.negate) return widen(x);
        // Integer negation is the literal two's complement ~x + 1 (not -x as a 64 bit value).
        return extended ? widen(ir.BitwiseNot(x)) : IR::U64{ir.IAdd(widen(ir.BitwiseNot(x)), widen(ir.Imm32(1U)))};
    };
    IR::U64 sum{ir.IAdd(ir.IAdd(term(a, op_a), term(b, op_b)), term(c, op_c))};
    if (extended) {
        const auto carry_term = [&](const IR::U1& carry) {
            return widen(IR::U32{ir.Select(carry, ir.Imm32(1U), ir.Imm32(0U))});
        };
        sum = IR::U64{ir.IAdd(sum, carry_term(PredIn(carry0_index, carry0_negated)))};
        sum = IR::U64{ir.IAdd(sum, carry_term(PredIn(carry1_index, carry1_negated)))};
    }
    const IR::Value words{ir.UnpackUint2x32(sum)};
    const IR::U32 low{ir.CompositeExtract(words, 0)};
    const IR::U32 high{ir.CompositeExtract(words, 1)};
    WriteDst(dest, low);
    if (overflow0 != 7) {
        PredOut(overflow0, ir.INotEqual(high, ir.Imm32(0U)));
    }
    if (overflow1 != 7) {
        PredOut(overflow1, ir.IGreaterThanEqual(high, ir.Imm32(2U), false));
    }
}

// PLOP3: two three-input logic operations over predicates, each given as an 8 bit truth table.
void Visitor::PLOP3() {
    const u32 lut1 = Take(16, 8);
    const u32 lut0 = Take(64, 3) | (Take(72, 5) << 3);
    // Bit 67 makes the third input a uniform predicate (PLOP3 only; UPLOP3 reads uniform predicates
    // throughout).
    const bool uniform_c = Take(67, 1) != 0;
    Require(!uniform_c || !uniform_op, "native uniform predicate operand");
    const u32 c_index = Take(68, 3);
    const bool c_negated = Take(71, 1) != 0;
    const u32 b_index = Take(77, 3);
    const bool b_negated = Take(80, 1) != 0;
    const u32 dest0 = Take(81, 3);
    const u32 dest1 = Take(84, 3);
    const u32 a_index = Take(87, 3);
    const bool a_negated = Take(90, 1) != 0;
    const std::array<IR::U1, 3> inputs{PredIn(a_index, a_negated), PredIn(b_index, b_negated),
                                       uniform_c ? UPred(c_index, c_negated) : PredIn(c_index, c_negated)};
    const auto evaluate = [&](u32 lut) {
        IR::U1 result{ir.Imm1(false)};
        for (u32 row = 0; row < 8; ++row) {
            if (((lut >> row) & 1) == 0) {
                continue;
            }
            IR::U1 term{ir.Imm1(true)};
            for (u32 k = 0; k < 3; ++k) {
                const bool wanted = ((row >> (2 - k)) & 1) != 0;
                term = ir.LogicalAnd(term, wanted ? inputs[k] : IR::U1{ir.LogicalNot(inputs[k])});
            }
            result = ir.LogicalOr(result, term);
        }
        return result;
    };
    if (dest0 != 7) {
        PredOut(dest0, evaluate(lut0));
    }
    if (dest1 != 7) {
        PredOut(dest1, evaluate(lut1));
    }
}

// P2R: pack predicates P0..P6 into the bits of Rd selected by the immediate mask, keeping the other
// bits of Ra.
void Visitor::P2R() {
    const u32 dest = Take(16, 8);
    const u32 source = Take(24, 8);
    const u32 mask = Take(32, 7);
    Require(Take(39, 1) == 0, "native predicate-to-register mask");
    IR::U32 insert{ir.Imm32(0U)};
    for (u32 bit_index = 0; bit_index < 7; ++bit_index) {
        if (((mask >> bit_index) & 1) == 0) {
            continue;
        }
        const IR::U32 bit{ir.Select(Pred(bit_index, false), ir.Imm32(1U << bit_index), ir.Imm32(0U))};
        insert = ir.BitwiseOr(insert, bit);
    }
    const IR::U32 kept{ir.BitwiseAnd(X(source), ir.Imm32(~mask))};
    X(dest, ir.BitwiseOr(kept, insert));
}

// R2P: unpack bits of a register into predicates P0..P6 selected by the immediate mask.
void Visitor::R2P() {
    Require(Take(16, 8) == 0, "native register-to-predicate destination");
    const u32 source = Take(24, 8);
    const u32 mask = Take(32, 7);
    Require(Take(39, 1) == 0, "native register-to-predicate mask");
    const u32 byte = Take(76, 2);
    const IR::U32 value{X(source)};
    for (u32 bit_index = 0; bit_index < 7; ++bit_index) {
        if (((mask >> bit_index) & 1) == 0) {
            continue;
        }
        const IR::U32 bit{ir.BitFieldExtract(value, ir.Imm32(byte * 8 + bit_index), ir.Imm32(1U))};
        SetPred(bit_index, ir.INotEqual(bit, ir.Imm32(0U)));
    }
}

} // namespace Shader::Ampere
