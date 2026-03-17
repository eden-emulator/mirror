// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/bit_field.h"
#include "common/common_types.h"
#include "shader_recompiler/frontend/maxwell/translate/impl/impl.h"

namespace Shader::Maxwell {
namespace {
enum class ISBERDMode : u64 {
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

enum class ISBERDShift : u64 {
    Default,
    U16,
    B32,
};

IR::U32 scaleIndex(IR::IREmitter& ir, IR::U32 index, ISBERDShift shift) {
    switch (shift) {
        case ISBERDShift::Default: return index;
        case ISBERDShift::U16: return ir.ShiftLeftLogical(index, ir.Imm32(1));
        case ISBERDShift::B32: return ir.ShiftLeftLogical(index, ir.Imm32(2));
        default: UNREACHABLE();
    }
}

IR::U32 skewBytes(IR::IREmitter& ir, SZ sizeRead) {
    const IR::U32 lane = ir.LaneId();
    switch (sizeRead) {
        case SZ::U8:  return lane;
        case SZ::U16: return ir.ShiftLeftLogical(lane, ir.Imm32(1));
        case SZ::U32:
        case SZ::F32: return ir.ShiftLeftLogical(lane, ir.Imm32(2));
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
        BitField<33, 2, ISBERDMode> mode;
        BitField<36, 4, SZ> sz;
        BitField<47, 2, ISBERDShift> shift;
    } const isberd{insn};

    IR::U32 index{};
    if (isberd.src_reg_num.Value() == 0xFF) {
        index = ir.Imm32(isberd.imm.Value());
    } else {
        const IR::U32 scaledIndex = scaleIndex(ir, X(isberd.src_reg.Value()), isberd.shift.Value());
        index = ir.IAdd(scaledIndex, ir.Imm32(isberd.imm.Value()));
    }

    if (isberd.o.Value()) {
        if (isberd.skew.Value()) {
            index = ir.IAdd(index, skewBytes(ir, isberd.sz.Value()));
        }

        const IR::U64 index64 = ir.UConvert(64, index);
        IR::U32 globalLoaded{};
        switch (isberd.sz.Value()) {
        case SZ::U8:  globalLoaded = ir.LoadGlobalU8 (index64); break;
        case SZ::U16: globalLoaded = ir.LoadGlobalU16(index64); break;
        case SZ::U32:
        case SZ::F32: globalLoaded = ir.LoadGlobal32(index64);  break;
        default: UNREACHABLE();
        }
        X(isberd.dest_reg.Value(), globalLoaded);

        return;
    }

    if (isberd.mode.Value() != ISBERDMode::Default) {
        if (isberd.skew.Value()) {
            index = ir.IAdd(index, skewBytes(ir, SZ::U32));
        }

        IR::F32 float_index{};
        switch (isberd.mode.Value()) {
        case ISBERDMode::Patch: float_index = ir.GetPatch(index.Patch());
            break;
        case ISBERDMode::Prim:  float_index = ir.GetAttribute(index.Attribute());
            break;
        case ISBERDMode::Attr:  float_index = ir.GetAttributeIndexed(index);
            break;
        default: UNREACHABLE();
        }
        X(isberd.dest_reg.Value(), ir.BitCast<IR::U32>(float_index));

        return;
    }

    if (isberd.skew.Value()) {
        X(isberd.dest_reg.Value(), ir.IAdd(X(isberd.src_reg.Value()), ir.LaneId()));

        return;
    }

    // Fallback copy
    X(isberd.dest_reg.Value(), X(isberd.src_reg.Value()));
}

} // namespace Shader::Maxwell
