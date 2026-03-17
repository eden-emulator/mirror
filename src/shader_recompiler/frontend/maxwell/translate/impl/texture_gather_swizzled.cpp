// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/bit_field.h"
#include "common/common_types.h"
#include "shader_recompiler/frontend/ir/modifiers.h"
#include "shader_recompiler/frontend/maxwell/translate/impl/impl.h"

namespace Shader::Maxwell {
namespace {
enum class TextureGatherSwizzledPrecision : u64 {
    F32,
    F16,
};

enum class TextureGatherSwizzledComponentType : u64 {
    R = 0,
    G = 1,
    B = 2,
    A = 3,
};

union EncodinTGS {
    u64 raw;
    BitField<55, 1, TextureGatherSwizzledPrecision> precision;
    BitField<52, 2, TextureGatherSwizzledComponentType> component_type;
    BitField<51, 1, u64> aoffi;
    BitField<50, 1, u64> dc;
    BitField<49, 1, u64> nodep;
    BitField<28, 8, IR::Reg> dest_reg_b;
    BitField<0, 8, IR::Reg> dest_reg_a;
    BitField<8, 8, IR::Reg> src_reg_a;
    BitField<20, 8, IR::Reg> src_reg_b;
    BitField<36, 13, u64> cbuf_offset;
};

void CheckAlignmentTGS(IR::Reg reg, size_t alignment) {
    if (!IR::IsAligned(reg, alignment)) {
        throw NotImplementedException("Unaligned source register {}", reg);
    }
}

IR::Value MakeOffset(TranslatorVisitor& v, IR::Reg reg) {
    const IR::U32 value{v.X(reg)};
    return v.ir.CompositeConstruct(v.ir.BitFieldExtract(value, v.ir.Imm32(0), v.ir.Imm32(6), true),
                                   v.ir.BitFieldExtract(value, v.ir.Imm32(8), v.ir.Imm32(6), true));
}

IR::Value SampleTGS(TranslatorVisitor& v, u64 insn) {
    const EncodinTGS tld4s{insn};
    const IR::U32 handle{v.ir.Imm32(static_cast<u32>(tld4s.cbuf_offset * 4))};
    const IR::Reg reg_a{tld4s.src_reg_a};
    const IR::Reg reg_b{tld4s.src_reg_b};
    IR::TextureInstInfo info{};
    if (tld4s.precision == TextureGatherSwizzledPrecision::F16) {
        info.relaxed_precision.Assign(1);
    }
    info.gather_component.Assign(static_cast<u32>(tld4s.component_type.Value()));
    info.type.Assign(Shader::TextureType::Color2D);
    info.is_depth.Assign(tld4s.dc != 0 ? 1 : 0);
    IR::Value coords;
    if (tld4s.aoffi != 0) {
        CheckAlignmentTGS(reg_a, 2);
        coords = v.ir.CompositeConstruct(v.F(reg_a), v.F(reg_a + 1));
        IR::Value offset = MakeOffset(v, reg_b);
        if (tld4s.dc != 0) {
            CheckAlignmentTGS(reg_b, 2);
            IR::F32 dref = v.F(reg_b + 1);
            return v.ir.ImageGatherDref(handle, coords, offset, {}, dref, info);
        }
        return v.ir.ImageGather(handle, coords, offset, {}, info);
    }
    if (tld4s.dc != 0) {
        CheckAlignmentTGS(reg_a, 2);
        coords = v.ir.CompositeConstruct(v.F(reg_a), v.F(reg_a + 1));
        IR::F32 dref = v.F(reg_b);
        return v.ir.ImageGatherDref(handle, coords, {}, {}, dref, info);
    }
    coords = v.ir.CompositeConstruct(v.F(reg_a), v.F(reg_b));
    return v.ir.ImageGather(handle, coords, {}, {}, info);
}

IR::Reg RegStoreComponent32(u64 insn, size_t index) {
    const EncodinTGS tlds4{insn};
    switch (index) {
    case 0:
        return tlds4.dest_reg_a;
    case 1:
        CheckAlignmentTGS(tlds4.dest_reg_a, 2);
        return tlds4.dest_reg_a + 1;
    case 2:
        return tlds4.dest_reg_b;
    case 3:
        CheckAlignmentTGS(tlds4.dest_reg_b, 2);
        return tlds4.dest_reg_b + 1;
    }
    throw LogicError("Invalid store index {}", index);
}

void Store32TGS(TranslatorVisitor& v, u64 insn, const IR::Value& sample) {
    for (size_t component = 0; component < 4; ++component) {
        const IR::Reg dest{RegStoreComponent32(insn, component)};
        v.F(dest, IR::F32{v.ir.CompositeExtract(sample, component)});
    }
}

IR::U32 PackTGS(TranslatorVisitor& v, const IR::F32& lhs, const IR::F32& rhs) {
    return v.ir.PackHalf2x16(v.ir.CompositeConstruct(lhs, rhs));
}

void Store16TGS(TranslatorVisitor& v, u64 insn, const IR::Value& sample) {
    std::array<IR::F32, 4> swizzled;
    for (size_t component = 0; component < 4; ++component) {
        swizzled[component] = IR::F32{v.ir.CompositeExtract(sample, component)};
    }
    const EncodinTGS tld4s{insn};
    v.X(tld4s.dest_reg_a, PackTGS(v, swizzled[0], swizzled[1]));
    v.X(tld4s.dest_reg_b, PackTGS(v, swizzled[2], swizzled[3]));
}
} // Anonymous namespace

void TranslatorVisitor::TLD4S(u64 insn) {
    const IR::Value sample{SampleTGS(*this, insn)};
    if (EncodinTGS{insn}.precision == TextureGatherSwizzledPrecision::F32) {
        Store32TGS(*this, insn, sample);
    } else {
        Store16TGS(*this, insn, sample);
    }
}

} // namespace Shader::Maxwell
