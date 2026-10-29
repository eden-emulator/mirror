// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// Encoding reference: Mesa NAK sm70_encode.rs (MIT), Copyright 2022 Collabora, Ltd.

#include <limits>

#include "shader_recompiler/frontend/ampere/visitor.h"
#include "shader_recompiler/frontend/ir/modifiers.h"
#include "shader_recompiler/frontend/maxwell/translate/impl/common_funcs.h"
#include "shader_recompiler/frontend/maxwell/translate/impl/half_floating_point_helper.h"

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

// F2I: float to integer, rounding by the rounding field then clamping to the destination range.
void Visitor::F2I() {
    const u32 dest = Take(16, 8);
    const Operand source = Source(1, true);
    const bool dst_signed = Take(72, 1) != 0;
    const u32 dst_size = Take(75, 2);
    Ignore(77, 1); // NTZ
    const u32 rounding = Take(78, 2);
    const bool ftz = Take(80, 1) != 0;
    const u32 src_size = Take(84, 2);
    Require(dst_size <= 2, "native float-to-integer destination size");
    Require(src_size == 2 || src_size == 1, "native float-to-integer source size");
    const bool half_source = src_size == 1;
    const u32 half_select = half_source ? Take(60, 1) : 0;

    IR::F16F32F64 value;
    if (half_source) {
        Require(source.kind == Operand::Kind::Reg, "native half-precision float-to-integer source");
        auto [lhs, rhs] = Maxwell::Extract(ir, X(source.reg), Maxwell::Swizzle::H1_H0);
        value = half_select ? rhs : lhs;
    } else {
        value = ir.BitCast<IR::F32>(Value(source));
    }
    const bool denorm_cares = !half_source;
    const IR::FpControl control{
        .no_contraction = true,
        .rounding = IR::FpRounding::DontCare,
        .fmz_mode = denorm_cares ? (ftz ? IR::FmzMode::FTZ : IR::FmzMode::None) : IR::FmzMode::DontCare,
    };
    const IR::F16F32F64 op_a{ir.FPAbsNeg(value, source.abs, source.negate)};
    IR::F16F32F64 rounded;
    switch (rounding) {
    case 0:
        rounded = ir.FPRoundEven(op_a, control);
        break;
    case 1:
        rounded = ir.FPFloor(op_a, control);
        break;
    case 2:
        rounded = ir.FPCeil(op_a, control);
        break;
    default:
        rounded = ir.FPTrunc(op_a, control);
        break;
    }
    // Destination widths: 8, 16 or 32 bits, saturating to the destination range.
    const u32 dst_bits = 8U << dst_size;
    f64 max_bound, min_bound;
    if (dst_signed) {
        max_bound = static_cast<f64>((s64{1} << (dst_bits - 1)) - 1);
        min_bound = -static_cast<f64>(s64{1} << (dst_bits - 1));
    } else {
        max_bound = static_cast<f64>((s64{1} << dst_bits) - 1);
        min_bound = 0.0;
    }
    IR::F16F32F64 intermediate;
    if (half_source) {
        const IR::F16 max_val{ir.FPConvert(16, ir.Imm32(static_cast<f32>(max_bound)))};
        const IR::F16 min_val{ir.FPConvert(16, ir.Imm32(static_cast<f32>(min_bound)))};
        intermediate = ir.FPClamp(rounded, min_val, max_val);
    } else {
        const IR::F32 max_val{ir.Imm32(static_cast<f32>(max_bound))};
        const IR::F32 min_val{ir.Imm32(static_cast<f32>(min_bound))};
        intermediate = ir.FPClamp(rounded, min_val, max_val);
    }
    IR::U32 result{ir.ConvertFToI(32, dst_signed, intermediate)};
    if (dst_signed) {
        result = IR::U32{ir.Select(ir.FPIsNan(op_a), ir.Imm32(0U), result)};
    }
    X(dest, result);
}

// I2FP / I2F: 32-bit (or small) integer to F32.
void Visitor::I2F() {
    const u32 dest = Take(16, 8);
    const Operand source = Source(1, false);
    const bool is_signed = Take(74, 1) != 0;
    const u32 dst_size = Take(75, 2);
    const u32 rounding = Take(78, 2);
    const u32 src_size = Take(84, 2);
    Require(dst_size == 2, "native integer-to-float destination size");
    Require(src_size <= 2, "native integer-to-float source size");
    // Bits 60..61 pick which byte or half of the register is converted (zero for a full word).
    const u32 select = Take(60, 2);
    Require(src_size < 2 || select == 0, "native integer-to-float source select");
    Require(src_size == 2 || select < (32U >> (3 + src_size)), "native integer-to-float source select");
    IR::U32 value{Value(source)};
    if (src_size < 2) {
        const u32 width = 8U << src_size;
        value = ir.BitFieldExtract(value, ir.Imm32(select * width), ir.Imm32(width), is_signed);
    }
    const IR::FpControl control{
        .no_contraction = false,
        .rounding = RoundingOf(rounding),
        .fmz_mode = IR::FmzMode::DontCare,
    };
    F(dest, IR::F32{ir.ConvertIToF(32, 32, is_signed, value, control)});
}

// FRND: round a float to an integral float.
void Visitor::FRND() {
    const u32 dest = Take(16, 8);
    const Operand source = Source(1, true);
    Require(Take(75, 2) == 2, "native float round destination size");
    const u32 rounding = Take(78, 2);
    const bool ftz = Take(80, 1) != 0;
    Require(Take(84, 2) == 2, "native float round source size");
    const IR::FpControl control{
        .no_contraction = false,
        .rounding = IR::FpRounding::DontCare,
        .fmz_mode = ftz ? IR::FmzMode::FTZ : IR::FmzMode::None,
    };
    const IR::F32 input{FloatValue(source)};
    IR::F32 result;
    switch (rounding) {
    case 0:
        result = IR::F32{ir.FPRoundEven(input, control)};
        break;
    case 1:
        result = IR::F32{ir.FPFloor(input, control)};
        break;
    case 2:
        result = IR::F32{ir.FPCeil(input, control)};
        break;
    default:
        result = IR::F32{ir.FPTrunc(input, control)};
        break;
    }
    F(dest, result);
}

// F2F: F32 to F16 conversion into the low half of the destination.
void Visitor::F2F() {
    const u32 dest = Take(16, 8);
    const Operand source = Source(1, true);
    Require(Take(75, 2) == 1, "native float conversion destination size");
    const u32 rounding = Take(78, 2);
    const bool ftz = Take(80, 1) != 0;
    Require(Take(84, 2) == 2, "native float conversion source size");
    const IR::FpControl control{
        .no_contraction = false,
        .rounding = RoundingOf(rounding),
        .fmz_mode = ftz ? IR::FmzMode::FTZ : IR::FmzMode::None,
    };
    const IR::F32 input{FloatValue(source)};
    const IR::F16F32F64 converted{ir.FPConvert(16, input, control)};
    const IR::F16 zero{ir.FPConvert(16, ir.Imm32(0.0f))};
    X(dest, ir.PackFloat2x16(ir.CompositeConstruct(converted, zero)));
}

// F2FP.PACK_AB: two F32 sources converted to F16 and packed (first source in the high half).
// MERGE_C keeps the low half of the third source raw in the high half instead.
void Visitor::F2FP() {
    const u32 dest = Take(16, 8);
    const Operand a = Source(0, false);
    const Operand b = Source(1, false);
    Ignore(64, 8);
    Require(Take(72, 1) == 0, "native float packing selector");
    const bool merge = Take(78, 1) != 0;
    Require(Take(79, 2) == 0, "native float packing rounding");
    const IR::FpControl control{
        .no_contraction = false,
        .rounding = IR::FpRounding::RN,
        .fmz_mode = IR::FmzMode::None,
    };
    const IR::F16 zero{ir.FPConvert(16, ir.Imm32(0.0f))};
    const auto to_half = [&](const IR::F32& value) {
        const IR::F16F32F64 converted{ir.FPConvert(16, value, control)};
        return IR::U32{ir.PackFloat2x16(ir.CompositeConstruct(converted, zero))};
    };
    IR::U32 high;
    if (merge) {
        const Operand c = Source(2, false);
        high = Value(c);
    } else {
        high = to_half(ir.BitCast<IR::F32>(Value(a)));
    }
    const IR::U32 low{to_half(ir.BitCast<IR::F32>(Value(b)))};
    X(dest, ir.IAdd(ir.ShiftLeftLogical(high, ir.Imm32(16U)), low));
}

} // namespace Shader::Ampere
