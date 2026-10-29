// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// Encoding reference: Mesa NAK sm70_encode.rs (MIT), Copyright 2022 Collabora, Ltd.

#include <cmath>

#include "shader_recompiler/frontend/ampere/visitor.h"
#include "shader_recompiler/frontend/ir/modifiers.h"
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

float ScaleFactor(u32 scale) {
    // The field is a power of two around 4 (no scaling): 3 halves, 5 doubles, 6 quadruples; the
    // Maxwell divide-by-4/8 and multiply-by-8 scales continue the same series at 2, 1 and 7.
    return std::ldexp(1.0f, static_cast<int>(scale) - 4);
}
} // Anonymous namespace

void Visitor::FMUL() {
    const u32 dest = Take(16, 8);
    const Operand a = Source(0, true);
    const Operand b = Source(1, true);
    // Source 2 (bits 64..71) is the unused zero register of a two-source encoding.
    Ignore(64, 8);
    Require(Take(76, 1) == 0, "native FMUL multiply-zero mode");
    const bool saturate = Take(77, 1) != 0;
    const u32 rounding = Take(78, 2);
    const bool ftz = Take(80, 1) != 0;
    const u32 scale = Take(84, 3);
    Require(scale >= 1 && scale <= 7, "native FMUL scale");
    Require(scale == 4 || (ftz && rounding == 0), "native FMUL scale with non-FTZ or non-RN modifiers");

    IR::F32 op_a{FloatValue(a)};
    if (scale != 4) {
        op_a = IR::F32{ir.FPMul(op_a, ir.Imm32(ScaleFactor(scale)))};
    }
    const IR::F32 op_b{FloatValue(b)};
    const IR::FpControl control{
        .no_contraction = true,
        .rounding = RoundingOf(rounding),
        .fmz_mode = ftz ? IR::FmzMode::FTZ : IR::FmzMode::None,
    };
    IR::F32 value{ir.FPMul(op_a, op_b, control)};
    if (saturate) {
        value = IR::F32{ir.FPSaturate(value)};
    }
    F(dest, value);
}

void Visitor::MUFU() {
    const u32 dest = Take(16, 8);
    const Operand source = Source(1, true);
    const bool half = Take(73, 1) != 0;
    const u32 operation = Take(74, 6);
    IR::F32 value_in;
    if (half) {
        // Half-precision source: select a half of the register, widen, compute in F32 and
        // narrow the result into the low half of the destination.
        Require(source.kind == Operand::Kind::Reg, "native half special-function source");
        const bool high = Take(60, 1) != 0;
        auto [lhs, rhs] = Maxwell::Extract(ir, X(source.reg), Maxwell::Swizzle::H1_H0);
        IR::F16F32F64 selected{high ? rhs : lhs};
        selected = ir.FPAbsNeg(selected, source.abs, source.negate);
        const IR::FpControl widen{
            .no_contraction = false,
            .rounding = IR::FpRounding::RN,
            .fmz_mode = IR::FmzMode::None,
        };
        value_in = IR::F32{ir.FPConvert(32, selected, widen)};
    } else {
        value_in = FloatValue(source);
    }
    IR::F32 value;
    switch (operation) {
    case 0:
        value = ir.FPCos(value_in);
        break;
    case 1:
        value = ir.FPSin(value_in);
        break;
    case 2:
        value = ir.FPExp2(value_in);
        break;
    case 3:
        value = ir.FPLog2(value_in);
        break;
    case 4:
        value = ir.FPRecip(value_in);
        break;
    case 5:
        value = ir.FPRecipSqrt(value_in);
        break;
    case 8:
        value = ir.FPSqrt(value_in);
        break;
    default:
        Unsupported("native MUFU operation");
    }
    if (half) {
        const IR::FpControl narrow{
            .no_contraction = false,
            .rounding = IR::FpRounding::RN,
            .fmz_mode = IR::FmzMode::None,
        };
        const IR::F16F32F64 converted{ir.FPConvert(16, value, narrow)};
        const IR::F16 zero{ir.FPConvert(16, ir.Imm32(0.0f))};
        X(dest, ir.PackFloat2x16(ir.CompositeConstruct(converted, zero)));
    } else {
        F(dest, value);
    }
}

// FSWZADD: quad-lane swizzled add. Only the two sub-operations the captured shaders use are
// accepted; the Maxwell swizzle byte is the complement of the sub-operation byte.
void Visitor::FSWZADD() {
    const u32 dest = Take(16, 8);
    const u32 source_a = Take(24, 8);
    const u32 subop = Take(32, 8);
    const u32 source_b = Take(64, 8);
    Require(Take(77, 4) == 0, "native swizzled add mode");
    Require(subop == 0x5a || subop == 0x66, "native swizzled add operation");
    const IR::FpControl control{
        .no_contraction = false,
        .rounding = IR::FpRounding::RN,
        .fmz_mode = IR::FmzMode::None,
    };
    F(dest, ir.FSwizzleAdd(F(source_a), F(source_b), ir.Imm32(~subop & 0xffU), control));
}

} // namespace Shader::Ampere
