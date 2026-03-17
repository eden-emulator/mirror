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
enum class SurfaceAtomicType : u64 {
    _1D = 0,
    _1D_BUFFER = 1,
    _1D_ARRAY = 2,
    _2D = 3,
    _2D_ARRAY = 4,
    _3D = 5,
    _UNK6 = 6,
    _UNK7 = 7,
};

/// For any would be newcomer to here: Yes - GPU dissasembly says S64 should
/// be after F16x2FTZRN. However if you do plan to revert this, you MUST test
/// ToTK beforehand. As the game will break with the subtle change
enum class SurfaceAtomicSize : u64 {
    U32,
    S32,
    U64,
    S64,
    F32FTZRN,
    F16x2FTZRN,
    SD32,
    SD64,
};

enum class AtomicOp : u64 {
    ADD,
    MIN,
    MAX,
    INC,
    DEC,
    AND,
    OR,
    XOR,
    EXCH,
};

enum class SurfaceAtomicClamp : u64 {
    IGN,
    Default,
    TRAP,
};

TextureType GetType(SurfaceAtomicType type) {
    switch (type) {
    case SurfaceAtomicType::_1D:
        return TextureType::Color1D;
    case SurfaceAtomicType::_1D_BUFFER:
        return TextureType::Buffer;
    case SurfaceAtomicType::_1D_ARRAY:
        return TextureType::ColorArray1D;
    case SurfaceAtomicType::_2D:
        return TextureType::Color2D;
    case SurfaceAtomicType::_2D_ARRAY:
        return TextureType::ColorArray2D;
    case SurfaceAtomicType::_3D:
        return TextureType::Color3D;
    default:
        throw NotImplementedException("Invalid type {}", type);
    }
}

IR::Value MakeCoords(TranslatorVisitor& v, IR::Reg reg, SurfaceAtomicType type) {
    const auto array{[&](int index) {
        return v.ir.BitFieldExtract(v.X(reg + index), v.ir.Imm32(0), v.ir.Imm32(16));
    }};
    switch (type) {
    case SurfaceAtomicType::_1D:
    case SurfaceAtomicType::_1D_BUFFER:
        return v.X(reg);
    case SurfaceAtomicType::_1D_ARRAY:
        return v.ir.CompositeConstruct(v.X(reg), array(1));
    case SurfaceAtomicType::_2D:
        return v.ir.CompositeConstruct(v.X(reg), v.X(reg + 1));
    case SurfaceAtomicType::_2D_ARRAY:
        return v.ir.CompositeConstruct(v.X(reg), v.X(reg + 1), array(2));
    case SurfaceAtomicType::_3D:
        return v.ir.CompositeConstruct(v.X(reg), v.X(reg + 1), v.X(reg + 2));
    default:
        throw NotImplementedException("Invalid type {}", type);
    }
}

IR::Value ApplyAtomicOp(IR::IREmitter& ir, const IR::U32& handle, const IR::Value& coords,
                        const IR::Value& op_b, IR::TextureInstInfo info, AtomicOp op,
                        bool is_signed) {
    switch (op) {
    case AtomicOp::ADD:
        return ir.ImageAtomicIAdd(handle, coords, op_b, info);
    case AtomicOp::MIN:
        return ir.ImageAtomicIMin(handle, coords, op_b, is_signed, info);
    case AtomicOp::MAX:
        return ir.ImageAtomicIMax(handle, coords, op_b, is_signed, info);
    case AtomicOp::INC:
        return ir.ImageAtomicInc(handle, coords, op_b, info);
    case AtomicOp::DEC:
        return ir.ImageAtomicDec(handle, coords, op_b, info);
    case AtomicOp::AND:
        return ir.ImageAtomicAnd(handle, coords, op_b, info);
    case AtomicOp::OR:
        return ir.ImageAtomicOr(handle, coords, op_b, info);
    case AtomicOp::XOR:
        return ir.ImageAtomicXor(handle, coords, op_b, info);
    case AtomicOp::EXCH:
        return ir.ImageAtomicExchange(handle, coords, op_b, info);
    default:
        throw NotImplementedException("Atomic Operation {}", op);
    }
}

ImageFormat Format(SurfaceAtomicSize size) {
    switch (size) {
    case SurfaceAtomicSize::U32:
    case SurfaceAtomicSize::S32:
    case SurfaceAtomicSize::SD32:
        return ImageFormat::R32_UINT;
    default:
        break;
    }
    throw NotImplementedException("Invalid size {}", size);
}

bool IsSizeInt32(SurfaceAtomicSize size) {
    switch (size) {
    case SurfaceAtomicSize::U32:
    case SurfaceAtomicSize::S32:
    case SurfaceAtomicSize::SD32:
        return true;
    default:
        return false;
    }
}

void ImageAtomOp(TranslatorVisitor& v, IR::Reg dest_reg, IR::Reg operand_reg, IR::Reg coord_reg,
                 std::optional<IR::Reg> bindless_reg, AtomicOp op, SurfaceAtomicClamp clamp, SurfaceAtomicSize size, SurfaceAtomicType type,
                 u64 bound_offset, bool is_bindless, bool write_result) {
    if (clamp != SurfaceAtomicClamp::IGN) {
        throw NotImplementedException("SurfaceAtomicClamp {}", clamp);
    }
    if (!IsSizeInt32(size)) {
        throw NotImplementedException("SurfaceAtomicSize {}", size);
    }
    const bool is_signed{size == SurfaceAtomicSize::S32};
    const ImageFormat format{Format(size)};
    const TextureType tex_type{GetType(type)};
    const IR::Value coords{MakeCoords(v, coord_reg, type)};

    const IR::U32 handle = is_bindless ? v.X(*bindless_reg) : v.ir.Imm32(u32(bound_offset * 4));
    IR::TextureInstInfo info{};
    info.type.Assign(tex_type);
    info.image_format.Assign(format);

    // TODO: float/64-bit operand
    const IR::Value op_b{v.X(operand_reg)};
    const IR::Value color{ApplyAtomicOp(v.ir, handle, coords, op_b, info, op, is_signed)};

    if (write_result) {
        v.X(dest_reg, IR::U32{color});
    }
}
} // Anonymous namespace

void TranslatorVisitor::SUATOM(u64 insn) {
    union {
        u64 raw;
        BitField<54, 1, u64> is_bindless;
        BitField<29, 4, AtomicOp> op;
        BitField<33, 3, SurfaceAtomicType> type;
        BitField<51, 3, SurfaceAtomicSize> size;
        BitField<49, 2, SurfaceAtomicClamp> clamp;
        BitField<0, 8, IR::Reg> dest_reg;
        BitField<8, 8, IR::Reg> coord_reg;
        BitField<20, 8, IR::Reg> operand_reg;
        BitField<36, 13, u64> bound_offset; // !is_bindless
        BitField<39, 8, IR::Reg> bindless_reg; // is_bindless
    } const suatom{insn};

    ImageAtomOp(*this, suatom.dest_reg, suatom.operand_reg, suatom.coord_reg, suatom.bindless_reg,
                suatom.op, suatom.clamp, suatom.size, suatom.type, suatom.bound_offset,
                suatom.is_bindless != 0, true);
}

void TranslatorVisitor::SURED(u64 insn) {
    // TODO: confirm offsets
    union {
        u64 raw;
        BitField<51, 1, u64> is_bound;
        BitField<24, 3, AtomicOp> op; //OK - 24 (SURedOp)
        BitField<33, 3, SurfaceAtomicType> type; //OK? - 33 (Dim)
        BitField<20, 3, SurfaceAtomicSize> size; //?
        BitField<49, 2, SurfaceAtomicClamp> clamp; //OK - 49 (Clamp4)
        BitField<0, 8, IR::Reg> operand_reg; //RA?
        BitField<8, 8, IR::Reg> coord_reg; //RB?
        BitField<36, 13, u64> bound_offset; //OK 33 (TidB)
        BitField<39, 8, IR::Reg> bindless_reg; // !is_bound
    } const sured{insn};
    ImageAtomOp(*this, IR::Reg::RZ, sured.operand_reg, sured.coord_reg, sured.bindless_reg,
                sured.op, sured.clamp, sured.size, sured.type, sured.bound_offset,
                sured.is_bound == 0, false);
}

} // namespace Shader::Maxwell
