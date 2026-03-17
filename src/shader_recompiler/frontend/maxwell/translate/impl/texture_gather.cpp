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

enum class TextureGatherType : u64 {
    _1D,
    ARRAY_1D,
    _2D,
    ARRAY_2D,
    _3D,
    ARRAY_3D,
    CUBE,
    ARRAY_CUBE,
};

enum class OffsetType : u64 {
    None = 0,
    AOFFI,
    PTP,
    Invalid,
};

enum class TextureGatherComponentType : u64 {
    R = 0,
    G = 1,
    B = 2,
    A = 3,
};

Shader::TextureType GetTextureGatherType(TextureGatherType type) {
    switch (type) {
    case TextureGatherType::_1D:
        return Shader::TextureType::Color1D;
    case TextureGatherType::ARRAY_1D:
        return Shader::TextureType::ColorArray1D;
    case TextureGatherType::_2D:
        return Shader::TextureType::Color2D;
    case TextureGatherType::ARRAY_2D:
        return Shader::TextureType::ColorArray2D;
    case TextureGatherType::_3D:
        return Shader::TextureType::Color3D;
    case TextureGatherType::ARRAY_3D:
        throw NotImplementedException("3D array texture type");
    case TextureGatherType::CUBE:
        return Shader::TextureType::ColorCube;
    case TextureGatherType::ARRAY_CUBE:
        return Shader::TextureType::ColorArrayCube;
    }
    throw NotImplementedException("Invalid texture type {}", type);
}

IR::Value MakeTextureGatherCoords(TranslatorVisitor& v, IR::Reg reg, TextureGatherType type) {
    const auto read_array{[&]() -> IR::F32 { return v.ir.ConvertUToF(32, 16, v.X(reg)); }};
    switch (type) {
    case TextureGatherType::_1D:
        return v.F(reg);
    case TextureGatherType::ARRAY_1D:
        return v.ir.CompositeConstruct(v.F(reg + 1), read_array());
    case TextureGatherType::_2D:
        return v.ir.CompositeConstruct(v.F(reg), v.F(reg + 1));
    case TextureGatherType::ARRAY_2D:
        return v.ir.CompositeConstruct(v.F(reg + 1), v.F(reg + 2), read_array());
    case TextureGatherType::_3D:
        return v.ir.CompositeConstruct(v.F(reg), v.F(reg + 1), v.F(reg + 2));
    case TextureGatherType::ARRAY_3D:
        throw NotImplementedException("3D array texture type");
    case TextureGatherType::CUBE:
        return v.ir.CompositeConstruct(v.F(reg), v.F(reg + 1), v.F(reg + 2));
    case TextureGatherType::ARRAY_CUBE:
        return v.ir.CompositeConstruct(v.F(reg + 1), v.F(reg + 2), v.F(reg + 3), read_array());
    }
    throw NotImplementedException("Invalid texture type {}", type);
}

IR::Value MakeOffset(TranslatorVisitor& v, IR::Reg& reg, TextureGatherType type) {
    const IR::U32 value{v.X(reg++)};
    switch (type) {
    case TextureGatherType::_1D:
    case TextureGatherType::ARRAY_1D:
        return v.ir.BitFieldExtract(value, v.ir.Imm32(0), v.ir.Imm32(6), true);
    case TextureGatherType::_2D:
    case TextureGatherType::ARRAY_2D:
        return v.ir.CompositeConstruct(
            v.ir.BitFieldExtract(value, v.ir.Imm32(0), v.ir.Imm32(6), true),
            v.ir.BitFieldExtract(value, v.ir.Imm32(8), v.ir.Imm32(6), true));
    case TextureGatherType::_3D:
    case TextureGatherType::ARRAY_3D:
        return v.ir.CompositeConstruct(
            v.ir.BitFieldExtract(value, v.ir.Imm32(0), v.ir.Imm32(6), true),
            v.ir.BitFieldExtract(value, v.ir.Imm32(8), v.ir.Imm32(6), true),
            v.ir.BitFieldExtract(value, v.ir.Imm32(16), v.ir.Imm32(6), true));
    case TextureGatherType::CUBE:
    case TextureGatherType::ARRAY_CUBE:
        throw NotImplementedException("Illegal offset on CUBE sample");
    }
    throw NotImplementedException("Invalid texture type {}", type);
}

std::pair<IR::Value, IR::Value> MakeOffsetPTP(TranslatorVisitor& v, IR::Reg& reg) {
    const IR::U32 value1{v.X(reg++)};
    const IR::U32 value2{v.X(reg++)};
    const IR::U32 bitsize{v.ir.Imm32(6)};
    const auto make_vector{[&v, &bitsize](const IR::U32& value) {
        return v.ir.CompositeConstruct(v.ir.BitFieldExtract(value, v.ir.Imm32(0), bitsize, true),
                                       v.ir.BitFieldExtract(value, v.ir.Imm32(8), bitsize, true),
                                       v.ir.BitFieldExtract(value, v.ir.Imm32(16), bitsize, true),
                                       v.ir.BitFieldExtract(value, v.ir.Imm32(24), bitsize, true));
    }};
    return {make_vector(value1), make_vector(value2)};
}

void Impl(TranslatorVisitor& v, u64 insn, TextureGatherComponentType component_type, OffsetType offset_type,
          bool is_bindless) {
    union {
        u64 raw;
        BitField<35, 1, u64> ndv;
        BitField<49, 1, u64> nodep;
        BitField<50, 1, u64> dc;
        BitField<51, 3, IR::Pred> sparse_pred;
        BitField<0, 8, IR::Reg> dest_reg;
        BitField<8, 8, IR::Reg> coord_reg;
        BitField<20, 8, IR::Reg> meta_reg;
        BitField<28, 3, TextureGatherType> type;
        BitField<31, 4, u64> mask;
        BitField<36, 13, u64> cbuf_offset;
    } const tld4{insn};

    const IR::Value coords{MakeTextureGatherCoords(v, tld4.coord_reg, tld4.type)};

    IR::Reg meta_reg{tld4.meta_reg};
    IR::Value handle;
    IR::Value offset;
    IR::Value offset2;
    IR::F32 dref;
    if (!is_bindless) {
        handle = v.ir.Imm32(static_cast<u32>(tld4.cbuf_offset.Value() * 4));
    } else {
        handle = v.X(meta_reg++);
    }
    switch (offset_type) {
    case OffsetType::None:
        break;
    case OffsetType::AOFFI:
        offset = MakeOffset(v, meta_reg, tld4.type);
        break;
    case OffsetType::PTP:
        std::tie(offset, offset2) = MakeOffsetPTP(v, meta_reg);
        break;
    default:
        throw NotImplementedException("Invalid offset type {}", offset_type);
    }
    if (tld4.dc != 0) {
        dref = v.F(meta_reg++);
    }
    IR::TextureInstInfo info{};
    info.type.Assign(GetTextureGatherType(tld4.type));
    info.is_depth.Assign(tld4.dc != 0 ? 1 : 0);
    info.gather_component.Assign(static_cast<u32>(component_type));
    const IR::Value sample{[&] {
        if (tld4.dc == 0) {
            return v.ir.ImageGather(handle, coords, offset, offset2, info);
        }
        return v.ir.ImageGatherDref(handle, coords, offset, offset2, dref, info);
    }()};

    IR::Reg dest_reg{tld4.dest_reg};
    for (size_t element = 0; element < 4; ++element) {
        if (((tld4.mask >> element) & 1) == 0) {
            continue;
        }
        v.F(dest_reg, IR::F32{v.ir.CompositeExtract(sample, element)});
        ++dest_reg;
    }
    if (tld4.sparse_pred != IR::Pred::PT) {
        v.ir.SetPred(tld4.sparse_pred, v.ir.LogicalNot(v.ir.GetSparseFromOp(sample)));
    }
}
} // Anonymous namespace

void TranslatorVisitor::TLD4(u64 insn) {
    union {
        u64 raw;
        BitField<56, 2, TextureGatherComponentType> component;
        BitField<54, 2, OffsetType> offset;
    } const tld4{insn};
    Impl(*this, insn, tld4.component, tld4.offset, false);
}

void TranslatorVisitor::TLD4_b(u64 insn) {
    union {
        u64 raw;
        BitField<38, 2, TextureGatherComponentType> component;
        BitField<36, 2, OffsetType> offset;
    } const tld4{insn};
    Impl(*this, insn, tld4.component, tld4.offset, true);
}

} // namespace Shader::Maxwell
