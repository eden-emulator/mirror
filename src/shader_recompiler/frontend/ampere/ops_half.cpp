// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// Encoding reference: Mesa NAK sm70_encode.rs (MIT), Copyright 2022 Collabora, Ltd.

#include <bit>

#include "shader_recompiler/frontend/ampere/visitor.h"
#include "shader_recompiler/frontend/ir/modifiers.h"
#include "shader_recompiler/frontend/maxwell/translate/impl/common_funcs.h"
#include "shader_recompiler/frontend/maxwell/translate/impl/half_floating_point_helper.h"

namespace Shader::Ampere {

// Packed half sources: the register (or immediate / constant) holds two binary16 lanes and each
// source has its own lane swizzle, absolute value and negation. Source 0 is the register at 24;
// sources 1 and 2 use the wide slot, or the register at 32 (source 1, register form) / 64.
HalfOperand Visitor::HalfSource(u32 which) {
    HalfOperand operand;
    const u32 form = Form();
    Require(form >= 1 && form <= 5, "native half source form");
    if (which == 0) {
        operand.value = X(Take(24, 8));
        operand.swizzle = Take(74, 2);
        operand.abs = Take(73, 1) != 0;
        operand.negate = Take(72, 1) != 0;
        return operand;
    }
    const bool wide = which == 1 ? (form == 4 || form == 5) : (form == 2 || form == 3);
    if (wide) {
        if (form == 2 || form == 4) {
            operand.value = ir.Imm32(Take(32, 32));
        } else {
            Require(Take(91, 1) == 0, "native bindless constant buffer");
            const u32 cbuf_index = Take(54, 5);
            const u32 cbuf_offset = Take(38, 16);
            operand.value = Cbuf(cbuf_index, cbuf_offset);
            operand.swizzle = Take(60, 2);
            operand.abs = Take(62, 1) != 0;
            operand.negate = Take(63, 1) != 0;
        }
        return operand;
    }
    const bool upper = which == 2 || form == 2 || form == 3;
    if (!upper) {
        operand.value = X(Take(32, 8));
        operand.swizzle = Take(60, 2);
        operand.abs = Take(62, 1) != 0;
        operand.negate = Take(63, 1) != 0;
    } else {
        operand.value = X(Take(64, 8));
        operand.swizzle = Take(81, 2);
        operand.abs = Take(83, 1) != 0;
        operand.negate = Take(84, 1) != 0;
    }
    return operand;
}

std::pair<IR::F16F32F64, IR::F16F32F64> Visitor::HalfLanes(const HalfOperand& operand) {
    Require(operand.swizzle != 1, "native half source read as float");
    auto [lhs, rhs] = Maxwell::Extract(ir, operand.value, static_cast<Maxwell::Swizzle>(operand.swizzle));
    lhs = ir.FPAbsNeg(lhs, operand.abs, operand.negate);
    rhs = ir.FPAbsNeg(rhs, operand.abs, operand.negate);
    return {lhs, rhs};
}

void Visitor::HADD2() {
    Require(Form() <= 3, "native half add source form");
    const u32 dest = Take(16, 8);
    const HalfOperand a = HalfSource(0);
    const HalfOperand b = HalfSource(Form() == 1 ? 1 : 2);
    Require(Take(76, 1) == 0 && Take(79, 1) == 0, "native half add mode");
    const bool saturate = Take(77, 1) != 0;
    const bool to_float = Take(78, 1) != 0;
    const bool ftz = Take(80, 1) != 0;
    if (to_float) {
        // -RZ + b.Hn widened to F32: a plain half to float conversion.
        Require(!ftz && !saturate, "native float-output half add flags");
        Require(b.swizzle >= 2, "native float-output half add source");
        auto [lhs, rhs] = Maxwell::Extract(ir, b.value, static_cast<Maxwell::Swizzle>(b.swizzle));
        IR::F16F32F64 selected{b.swizzle == 3 ? rhs : lhs};
        selected = ir.FPAbsNeg(selected, b.abs, b.negate);
        const IR::FpControl widen{
            .no_contraction = false,
            .rounding = IR::FpRounding::RN,
            .fmz_mode = IR::FmzMode::None,
        };
        X(dest, ir.BitCast<IR::U32>(IR::F32{ir.FPConvert(32, selected, widen)}));
        return;
    }
    const auto [lhs_a, rhs_a] = HalfLanes(a);
    const auto [lhs_b, rhs_b] = HalfLanes(b);
    const IR::FpControl control{
        .no_contraction = true,
        .rounding = IR::FpRounding::DontCare,
        .fmz_mode = ftz ? IR::FmzMode::FTZ : IR::FmzMode::None,
    };
    IR::F16F32F64 lhs{ir.FPAdd(lhs_a, lhs_b, control)};
    IR::F16F32F64 rhs{ir.FPAdd(rhs_a, rhs_b, control)};
    if (saturate) {
        lhs = ir.FPSaturate(lhs);
        rhs = ir.FPSaturate(rhs);
    }
    X(dest, ir.PackFloat2x16(ir.CompositeConstruct(lhs, rhs)));
}

void Visitor::HMUL2() {
    Require(Form() == 1 || Form() == 4 || Form() == 5, "native half multiply source form");
    const u32 dest = Take(16, 8);
    const HalfOperand a = HalfSource(0);
    const HalfOperand b = HalfSource(1);
    Require(Take(78, 2) == 0, "native half multiply mode");
    const bool multiply_zero = Take(76, 1) != 0;
    const bool saturate = Take(77, 1) != 0;
    const bool ftz = Take(80, 1) != 0;
    Require(!multiply_zero, "native half multiply-zero mode");
    const auto [lhs_a, rhs_a] = HalfLanes(a);
    const auto [lhs_b, rhs_b] = HalfLanes(b);
    const IR::FpControl control{
        .no_contraction = true,
        .rounding = IR::FpRounding::DontCare,
        .fmz_mode = ftz ? IR::FmzMode::FTZ : IR::FmzMode::None,
    };
    IR::F16F32F64 lhs{ir.FPMul(lhs_a, lhs_b, control)};
    IR::F16F32F64 rhs{ir.FPMul(rhs_a, rhs_b, control)};
    if (saturate) {
        lhs = ir.FPSaturate(lhs);
        rhs = ir.FPSaturate(rhs);
    }
    X(dest, ir.PackFloat2x16(ir.CompositeConstruct(lhs, rhs)));
}

void Visitor::HFMA2() {
    const u32 dest = Take(16, 8);
    const HalfOperand a = HalfSource(0);
    const HalfOperand b = HalfSource(1);
    const HalfOperand c = HalfSource(2);
    Require(Take(76, 1) == 0, "native half fused multiply-add multiply-zero mode");
    const bool saturate = Take(77, 1) != 0;
    Require(Take(78, 1) == 0, "native half fused multiply-add float mode");
    const bool relu = Take(79, 1) != 0;
    // The relu form carries a constant 7 in bits 87..90 (observed on every captured program).
    Require(Take(87, 4) == (relu ? 7U : 0U), "native half fused multiply-add mode");
    Require(!relu || !saturate, "native half fused multiply-add relu with saturation");
    const bool ftz = Take(80, 1) != 0;
    Require(Take(85, 1) == 0, "native half fused multiply-add bf16");
    const bool negate_high_b = Take(86, 1) != 0;
    const auto [lhs_a, rhs_a] = HalfLanes(a);
    auto [lhs_b, rhs_b] = HalfLanes(b);
    if (negate_high_b) {
        rhs_b = ir.FPAbsNeg(rhs_b, false, true);
    }
    const auto [lhs_c, rhs_c] = HalfLanes(c);
    const IR::FpControl control{
        .no_contraction = true,
        .rounding = IR::FpRounding::DontCare,
        .fmz_mode = ftz ? IR::FmzMode::FTZ : IR::FmzMode::None,
    };
    IR::F16F32F64 lhs{ir.FPFma(lhs_a, lhs_b, lhs_c, control)};
    IR::F16F32F64 rhs{ir.FPFma(rhs_a, rhs_b, rhs_c, control)};
    if (relu) {
        // Negative lanes clamp to zero; NaN passes through since the comparison is ordered.
        const auto clamp = [&](const IR::F16F32F64& lane) {
            // Select has no half-precision form, so compare and pick in single precision.
            const bool half = lane.Type() == IR::Type::F16;
            const IR::F32 wide{half ? IR::F32{ir.FPConvert(32, lane)} : IR::F32{lane}};
            const IR::F32 zero{ir.Imm32(0.0f)};
            const IR::F32 negative{ir.Select(ir.FPLessThan(wide, zero), zero, wide)};
            // NaN of either sign becomes the canonical quiet NaN.
            const IR::F32 picked{ir.Select(ir.FPIsNan(wide), ir.Imm32(std::bit_cast<f32>(0x7fc00000U)), negative)};
            return half ? IR::F16F32F64{ir.FPConvert(16, picked)} : IR::F16F32F64{picked};
        };
        lhs = clamp(lhs);
        rhs = clamp(rhs);
    }
    if (saturate) {
        lhs = ir.FPSaturate(lhs);
        rhs = ir.FPSaturate(rhs);
    }
    X(dest, ir.PackFloat2x16(ir.CompositeConstruct(lhs, rhs)));
}

// HMNMX2: per-lane half minimum or maximum, selected by a predicate (true selects the minimum).
// The comparison runs in single precision, which is exact for binary16 inputs.
void Visitor::HMNMX2() {
    const u32 dest = Take(16, 8);
    const HalfOperand a = HalfSource(0);
    const HalfOperand b = HalfSource(1);
    Require(Take(76, 11) == 0, "native half min/max mode");
    const u32 pred_index = Take(87, 3);
    const bool pred_negated = Take(90, 1) != 0;
    Require(Take(91, 1) == 0, "native half min/max flags");
    const auto [lhs_a, rhs_a] = HalfLanes(a);
    const auto [lhs_b, rhs_b] = HalfLanes(b);
    const IR::U1 select_min{Pred(pred_index, pred_negated)};
    const auto lane = [&](const IR::F16F32F64& x, const IR::F16F32F64& y) -> IR::F16F32F64 {
        const bool half = x.Type() == IR::Type::F16;
        const IR::F32 wide_x{half ? IR::F32{ir.FPConvert(32, x)} : IR::F32{x}};
        const IR::F32 wide_y{half ? IR::F32{ir.FPConvert(32, y)} : IR::F32{y}};
        const IR::F32 low{ir.FPMin(wide_x, wide_y)};
        const IR::F32 high{ir.FPMax(wide_x, wide_y)};
        const IR::F32 picked{ir.Select(select_min, low, high)};
        return half ? IR::F16F32F64{ir.FPConvert(16, picked)} : IR::F16F32F64{picked};
    };
    X(dest, ir.PackFloat2x16(ir.CompositeConstruct(lane(lhs_a, lhs_b), lane(rhs_a, rhs_b))));
}

// HSETP2: compares the two half lanes of two sources and writes the lane results to two predicates;
// with the horizontal flag the first receives the AND of both lanes and the second its complement.
void Visitor::HSETP2() {
    const HalfOperand a = HalfSource(0);
    const HalfOperand b = HalfSource(1);
    Require(Take(65, 1) == 0, "native half compare mode");
    const u32 set_op = Take(69, 2);
    const bool horizontal = Take(71, 1) != 0;
    const u32 compare = Take(76, 4);
    const bool ftz = Take(80, 1) != 0;
    const u32 dest_a = Take(81, 3);
    const u32 dest_b = Take(84, 3);
    const u32 accum_index = Take(87, 3);
    const bool accum_negated = Take(90, 1) != 0;
    Require(set_op <= 2, "native predicate combine");
    auto [lhs_a, rhs_a] = HalfLanes(a);
    auto [lhs_b, rhs_b] = HalfLanes(b);
    const IR::FpControl control{
        .no_contraction = false,
        .rounding = IR::FpRounding::DontCare,
        .fmz_mode = ftz ? IR::FmzMode::FTZ : IR::FmzMode::None,
    };
    const IR::U1 accum{Pred(accum_index, accum_negated)};
    const auto bop = static_cast<Maxwell::BooleanOp>(set_op);
    const auto lane = [&](const IR::F16F32F64& x, const IR::F16F32F64& y) {
        const IR::U1 comparison{Maxwell::FloatingPointCompare(
            ir, x, y, static_cast<Maxwell::FPCompareOp>(compare), control)};
        return IR::U1{Maxwell::PredicateCombine(ir, comparison, accum, bop)};
    };
    const IR::U1 low{lane(lhs_a, lhs_b)};
    const IR::U1 high{lane(rhs_a, rhs_b)};
    if (horizontal) {
        const IR::U1 both{ir.LogicalAnd(low, high)};
        SetPred(dest_a, both);
        SetPred(dest_b, ir.LogicalNot(both));
    } else {
        SetPred(dest_a, low);
        SetPred(dest_b, high);
    }
}

// ---- Warp-wide matrix operations. A matrix is 8x8 b16 elements; each lane owns the two adjacent
// elements of row lane/4, columns 2*(lane%4) and +1, packed in one register.

IR::U32 Visitor::ShuffleLane(const IR::U32& value, const IR::U32& lane) {
    return ir.ShuffleIndex(value, lane, ir.Imm32(0x1fU), ir.Imm32(0U));
}

// Transposes an 8x8 b16 matrix held one register per lane: the lane gets elements (2q, r) and
// (2q+1, r) of the source for r = lane/4 and q = lane%4.
IR::U32 Visitor::TransposeB16(const IR::U32& value) {
    const IR::U32 lane{ir.LaneId()};
    const IR::U32 low_lane{ir.IAdd(ir.ShiftLeftLogical(ir.BitwiseAnd(lane, ir.Imm32(3U)), ir.Imm32(3U)),
                                   ir.ShiftRightLogical(lane, ir.Imm32(3U)))};
    const IR::U32 shift{ir.ShiftLeftLogical(ir.BitwiseAnd(lane, ir.Imm32(4U)), ir.Imm32(2U))};
    const auto element = [&](const IR::U32& source_lane) {
        return IR::U32{ir.BitwiseAnd(ir.ShiftRightLogical(ShuffleLane(value, source_lane), shift),
                                     ir.Imm32(0xffffU))};
    };
    const IR::U32 first{element(low_lane)};
    const IR::U32 second{element(ir.IAdd(low_lane, ir.Imm32(4U)))};
    return ir.BitwiseOr(first, ir.ShiftLeftLogical(second, ir.Imm32(16U)));
}

// LDSM: loads 2 or 4 8x8 b16 matrices from shared memory. Each matrix row address comes from the
// address register of lane (row + 8 * matrix); a lane then reads its 32 bits of its row.
void Visitor::LDSM() {
    const u32 dest = Take(16, 8);
    const u32 address_reg = Take(24, 8);
    const u32 uniform = Take(32, 6);
    const u32 offset = Take(40, 24);
    const u32 count_select = Take(72, 2);
    const u32 layout_select = Take(78, 2);
    const bool uniform_address = Take(91, 1) != 0;
    Require(count_select == 1 || count_select == 2, "native shared matrix load count");
    Require(layout_select <= 1, "native shared matrix layout");
    const u32 matrices = count_select == 1 ? 2 : 4;
    Require(dest % matrices == 0 && dest + matrices <= 255, "native shared matrix destination");
    IR::U32 address{X(address_reg)};
    if (uniform_address) {
        address = ir.IAdd(address, UR(uniform));
    }
    const IR::U32 lane{ir.LaneId()};
    const IR::U32 row{ir.ShiftRightLogical(lane, ir.Imm32(2U))};
    const IR::U32 column_bytes{ir.ShiftLeftLogical(ir.BitwiseAnd(lane, ir.Imm32(3U)), ir.Imm32(2U))};
    for (u32 matrix = 0; matrix < matrices; ++matrix) {
        const IR::U32 source_lane{ir.IAdd(row, ir.Imm32(matrix * 8))};
        const IR::U32 row_address{ShuffleLane(address, source_lane)};
        const IR::U32 byte_address{ir.IAdd(ir.IAdd(row_address, column_bytes), ir.Imm32(offset))};
        IR::U32 value{ir.LoadShared(32, false, byte_address)};
        if (layout_select == 1) {
            value = TransposeB16(value);
        }
        X(dest + matrix, value);
    }
}

// MOVM.16.MT88: transposes an 8x8 b16 matrix across the warp.
void Visitor::MOVM() {
    const u32 dest = Take(16, 8);
    const u32 source = Take(24, 8);
    Require(Take(78, 2) == 0, "native register matrix transpose mode");
    X(dest, TransposeB16(X(source)));
}

// HMMA.1688 / HMMA.16816 with F16 accumulation: D (16x8) = A * B + C across the warp. Mirrors the
// reference lowering: the products accumulate in single precision and round to half after every
// eight of them.
void Visitor::HMMA() {
    const u32 dest = Take(16, 8);
    const u32 a = Take(24, 8);
    const u32 b = Take(32, 8);
    const u32 c = Take(64, 8);
    const bool deep = Take(75, 1) != 0;
    const u32 depth = deep ? 16 : 8;
    const u32 a_count = depth / 4;
    const u32 b_count = depth / 8;
    Require(dest % 2 == 0 && dest + 2 <= 255, "native half matrix multiply destination");
    Require(a % a_count == 0 && a + a_count <= 255 && b % b_count == 0 && b + b_count <= 255,
            "native half matrix multiply sources");
    Require(c == 255 || (c % 2 == 0 && c + 2 <= 255), "native half matrix multiply accumulator");
    const auto to_f32 = [&](const IR::F16F32F64& x) -> IR::F32 {
        return x.Type() == IR::Type::F16 ? IR::F32{ir.FPConvert(32, x)} : IR::F32{x};
    };
    const auto half_of = [&](const IR::U32& word, u32 which) -> IR::F32 {
        const auto [low, high] = Maxwell::Extract(ir, word, Maxwell::Swizzle::H1_H0);
        return to_f32(which == 0 ? low : high);
    };
    std::array<IR::F32, 4> acc;
    for (u32 row = 0; row < 2; ++row) {
        for (u32 col = 0; col < 2; ++col) {
            acc[row * 2 + col] = c == 255 ? IR::F32{ir.Imm32(0.0f)} : half_of(X(c + row), col);
        }
    }
    const IR::FpControl control{
        .no_contraction = true,
        .rounding = IR::FpRounding::DontCare,
        .fmz_mode = IR::FmzMode::None,
    };
    const IR::U32 lane{ir.LaneId()};
    const IR::U32 row_base{ir.BitwiseAnd(lane, ir.Imm32(0x1cU))};
    const IR::U32 column_base{ir.ShiftLeftLogical(ir.BitwiseAnd(lane, ir.Imm32(3U)), ir.Imm32(3U))};
    for (u32 pair = 0; pair < depth / 2; ++pair) {
        const IR::U32 step{ir.Imm32(pair & 3)};
        const IR::U32 a_lane{ir.IAdd(row_base, step)};
        const IR::U32 b_lane{ir.IAdd(column_base, step)};
        const IR::U32 a_row0{ShuffleLane(X(a + (pair >= 4 ? 2 : 0)), a_lane)};
        const IR::U32 a_row1{ShuffleLane(X(a + (pair >= 4 ? 3 : 1)), a_lane)};
        const IR::U32 b_col0{ShuffleLane(X(b + pair / 4), b_lane)};
        const IR::U32 b_col1{ShuffleLane(X(b + pair / 4), ir.IAdd(b_lane, ir.Imm32(4U)))};
        for (u32 half = 0; half < 2; ++half) {
            const IR::F32 b0{half_of(b_col0, half)};
            const IR::F32 b1{half_of(b_col1, half)};
            for (u32 row = 0; row < 2; ++row) {
                const IR::F32 av{half_of(row == 0 ? a_row0 : a_row1, half)};
                acc[row * 2] = IR::F32{ir.FPFma(av, b0, acc[row * 2], control)};
                acc[row * 2 + 1] = IR::F32{ir.FPFma(av, b1, acc[row * 2 + 1], control)};
            }
        }
        if ((pair & 3) == 3 && pair + 1 < depth / 2) {
            for (auto& value : acc) {
                value = to_f32(ir.FPConvert(16, value));
            }
        }
    }
    for (u32 row = 0; row < 2; ++row) {
        const IR::F16F32F64 low{ir.FPConvert(16, acc[row * 2])};
        const IR::F16F32F64 high{ir.FPConvert(16, acc[row * 2 + 1])};
        X(dest + row, ir.PackFloat2x16(ir.CompositeConstruct(low, high)));
    }
}

} // namespace Shader::Ampere
