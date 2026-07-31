// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/bit_field.h"
#include "common/common_types.h"
#include "shader_recompiler/frontend/maxwell/translate/impl/impl.h"

namespace Shader::Maxwell {
namespace {
enum class Mode : u64 {
    Default,
    Patch,
    Prim,
    Attr,
};

enum class SZ : u64 {
    U8,
    U16,
    U32,
    F32
};

enum class Shift : u64 {
    Default,
    U16,
    B32,
};

IR::U32 scaleIndex(IR::IREmitter& ir, IR::U32 index, Shift shift) {
    switch (shift) {
        case Shift::Default: return index;
        case Shift::U16: return ir.ShiftLeftLogical(index, ir.Imm32(1));
        case Shift::B32: return ir.ShiftLeftLogical(index, ir.Imm32(2));
        default: UNREACHABLE();
    }
}

} // Anonymous namespace

void TranslatorVisitor::ISBERD(u64 insn) {
    LOG_DEBUG(Shader, "called with insn={:#X}", insn);

    union {
        u64 raw;
        BitField<0, 8, IR::Reg> dest_reg;
        BitField<8, 8, IR::Reg> src_reg;
        BitField<8, 8, u32> src_reg_num;
        BitField<24, 8, u32> imm;
        BitField<31, 1, u64> skew;
        BitField<32, 1, u64> o;
        BitField<33, 2, Mode> mode;
        BitField<36, 4, SZ> sz;
        BitField<47, 2, Shift> shift;
    } const isberd{insn};

    if (isberd.skew != 0) {
        throw NotImplementedException("ISBERD SKEW");
    }
    if (isberd.o != 0) {
        throw NotImplementedException("ISBERD O");
    }
    if (isberd.sz.Value() > SZ::F32) {
        throw NotImplementedException("ISBERD SZ {}",
                                      static_cast<u64>(isberd.sz.Value()));
    }
    if (isberd.shift.Value() > Shift::B32) {
        throw NotImplementedException("ISBERD Shift {}",
                                      static_cast<u64>(isberd.shift.Value()));
    }

    switch (isberd.mode.Value()) {
    case Mode::Default:
        X(isberd.dest_reg.Value(), X(isberd.src_reg.Value()));
        return;
    case Mode::Attr: {
        IR::U32 offset{};
        if (isberd.src_reg_num.Value() == 0xFF) {
            offset = ir.Imm32(isberd.imm.Value());
        } else {
            const IR::U32 index{
                scaleIndex(ir, X(isberd.src_reg.Value()), isberd.shift.Value())};
            offset = ir.IAdd(index, ir.Imm32(isberd.imm.Value()));
        }
        X(isberd.dest_reg.Value(), ir.BitCast<IR::U32>(ir.GetAttributeIndexed(offset)));
        return;
    }
    default:
        throw NotImplementedException("ISBERD Mode {}",
                                      static_cast<u64>(isberd.mode.Value()));
    }
}

} // namespace Shader::Maxwell
