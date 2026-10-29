// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// Encoding reference: Mesa NAK sm70_encode.rs (MIT), Copyright 2022 Collabora, Ltd.

#include "shader_recompiler/frontend/ampere/visitor.h"
#include "shader_recompiler/frontend/ir/attribute.h"

namespace Shader::Ampere {

// ALD: attribute load (vertex / geometry inputs, tessellation inputs).
void Visitor::ALD() {
    const u32 dest = Take(16, 8);
    Require(Take(24, 8) == 255, "native indexed attribute access");
    const u32 vertex_reg = Take(32, 8);
    const u32 address = Take(40, 10);
    const u32 components = Take(74, 2) + 1;
    Require(Take(76, 2) == 0, "native patch or physical attribute access");
    Require(Take(79, 1) == 0, "native output attribute read");
    Require(address % 4 == 0 && address + components * 4 <= 0x400, "native attribute address");
    Require(dest + components <= 255, "native attribute register range");
    const IR::U32 vertex{X(vertex_reg)};
    for (u32 element = 0; element < components; ++element) {
        const IR::Attribute attribute{address / 4 + element};
        F(dest + element, ir.GetAttribute(attribute, vertex));
    }
}

// AST: attribute store (vertex outputs).
void Visitor::AST() {
    Require(Take(24, 8) == 255, "native indexed attribute access");
    const u32 data_reg = Take(32, 8);
    const u32 address = Take(40, 10);
    const u32 vertex_reg = Take(64, 8);
    const u32 components = Take(74, 2) + 1;
    Require(Take(76, 2) == 0, "native patch or physical attribute access");
    Require(address % 4 == 0 && address + components * 4 <= 0x400, "native attribute address");
    // RZ as the data source stores zero for every component.
    Require(data_reg == 255 || data_reg + components <= 255, "native attribute register range");
    const IR::U32 vertex{X(vertex_reg)};
    for (u32 element = 0; element < components; ++element) {
        const IR::Attribute attribute{address / 4 + element};
        ir.SetAttribute(attribute, F(data_reg == 255 ? 255 : data_reg + element), vertex);
    }
}

// LDC: indexed constant buffer load, 32 or 64 bits.
void Visitor::LDC() {
    const u32 dest = Take(16, 8);
    const u32 offset_reg = Take(24, 8);
    const s32 offset = static_cast<s16>(Take(38, 16));
    const u32 cbuf_index = Take(54, 5);
    const u32 width = Take(73, 3);
    Require(Take(78, 2) == 0, "native indexed constant load mode");
    Require(Take(91, 1) == 0, "native bindless constant load");
    Require(width <= 6, "native constant load width");
    Require(cbuf_index < 32, "native constant buffer index");
    const IR::U32 binding{ir.Imm32(cbuf_index)};
    const IR::U32 byte_offset{ir.IAdd(X(offset_reg), ir.Imm32(static_cast<u32>(offset)))};
    if (width < 4) {
        // 8 or 16 bit element, zero or sign extended (types U8, S8, U16, S16).
        X(dest, IR::U32{ir.GetCbuf(binding, byte_offset, width < 2 ? 8 : 16, (width & 1) != 0)});
    } else if (width == 4) {
        X(dest, IR::U32{ir.GetCbuf(binding, byte_offset, 32, false)});
    } else if (width == 5) {
        Require(dest % 2 == 0 && dest < 254, "native constant load destination");
        const IR::Value vector{ir.GetCbuf(binding, byte_offset, 64, false)};
        for (u32 i = 0; i < 2; ++i) {
            X(dest + i, IR::U32{ir.CompositeExtract(vector, i)});
        }
    } else {
        Require(dest % 4 == 0 && dest < 252, "native constant load destination");
        for (u32 i = 0; i < 4; ++i) {
            const IR::U32 word_offset{ir.IAdd(byte_offset, ir.Imm32(i * 4))};
            X(dest + i, IR::U32{ir.GetCbuf(binding, word_offset, 32, false)});
        }
    }
}

// CS2R: special register pair read; only SRZ (a constant zero) is modelled.
void Visitor::CS2R() {
    const u32 dest = Take(16, 8);
    Require(Take(72, 8) == 255, "native special register pair");
    const bool wide = Take(80, 1) != 0;
    X(dest, ir.Imm32(0U));
    if (wide) {
        Require(dest + 1 < 255, "native special register pair destination");
        X(dest + 1, ir.Imm32(0U));
    }
}

// S2R: thread and workgroup identities.
void Visitor::S2R() {
    const u32 dest = Dst();
    const u32 special = Take(72, 8);
    switch (special) {
    case 0:
        WriteDst(dest, ir.LaneId());
        break;
    case 33:
        WriteDst(dest, ir.LocalInvocationIdX());
        break;
    case 34:
        WriteDst(dest, ir.LocalInvocationIdY());
        break;
    case 35:
        WriteDst(dest, ir.LocalInvocationIdZ());
        break;
    case 37:
        WriteDst(dest, ir.WorkgroupIdX());
        break;
    case 38:
        WriteDst(dest, ir.WorkgroupIdY());
        break;
    case 39:
        WriteDst(dest, ir.WorkgroupIdZ());
        break;
    case 30:
    case 31: // SR_WSCALEFACTOR_Z: read next to XY and multiplied into a float (FMUL) in the Kirby shaders
        // SR_WSCALEFACTOR_XY (assumed from the Maxwell/Volta register numbering, not in NAK). Without
        // viewport W scaling the factor is the identity; the 32-bit float 1.0 is an unverified assumption.
        WriteDst(dest, ir.Imm32(0x3f800000U));
        break;
    case 56:
        WriteDst(dest, ir.SubgroupEqMask());
        break;
    case 57:
        WriteDst(dest, ir.SubgroupLtMask());
        break;
    case 58:
        WriteDst(dest, ir.SubgroupLeMask());
        break;
    case 59:
        WriteDst(dest, ir.SubgroupGtMask());
        break;
    case 60:
        WriteDst(dest, ir.SubgroupGeMask());
        break;
    default:
        Unsupported("native special register");
    }
}

// UMOV: move an immediate or a uniform register into a uniform register.
void Visitor::UMOV() {
    const u32 dest = Dst();
    if (op == 0x882) {
        UR(dest, ir.Imm32(Take(32, 32)));
    } else {
        const u32 source = Take(32, 6);
        Require(Take(91, 1) == 1, "native uniform move flag");
        UR(dest, UR(source));
    }
}

// ULDC: constant buffer load into uniform registers; 0xabb indexes the offset with a uniform register.
void Visitor::ULDC() {
    const u32 dest = Dst();
    const bool indexed = op == 0xabb;
    const u32 index_reg = indexed ? Take(24, 6) : 63;
    if (!indexed) {
        Ignore(24, 6);
    }
    const u32 offset = Take(38, 16);
    const u32 cbuf_index = Take(54, 5);
    const u32 width = Take(73, 3);
    Require(Take(91, 1) == (indexed ? 1U : 0U), "native uniform constant load flag");
    Require(width <= 6, "native uniform constant load width");
    Require(cbuf_index < 32 && offset % 4 == 0, "native uniform constant buffer address");
    const IR::U32 binding{ir.Imm32(cbuf_index)};
    const IR::U32 byte_offset{indexed ? IR::U32{ir.IAdd(UR(index_reg), ir.Imm32(offset))} : ir.Imm32(offset)};
    if (width < 4) {
        // 8 or 16 bit element, zero or sign extended (types U8, S8, U16, S16).
        UR(dest, IR::U32{ir.GetCbuf(binding, byte_offset, width < 2 ? 8 : 16, (width & 1) != 0)});
    } else if (width == 4) {
        UR(dest, IR::U32{ir.GetCbuf(binding, byte_offset, 32, false)});
    } else if (width == 5) {
        Require(dest % 2 == 0 && dest < 62, "native uniform constant load destination");
        const IR::Value vector{ir.GetCbuf(binding, byte_offset, 64, false)};
        for (u32 i = 0; i < 2; ++i) {
            UR(dest + i, IR::U32{ir.CompositeExtract(vector, i)});
        }
    } else {
        Require(dest % 4 == 0 && dest < 60, "native uniform constant load destination");
        for (u32 i = 0; i < 4; ++i) {
            const IR::U32 word_offset{ir.IAdd(byte_offset, ir.Imm32(i * 4))};
            UR(dest + i, IR::U32{ir.GetCbuf(binding, word_offset, 32, false)});
        }
    }
}

// MATCH.ALL on a 32 or 64 bit value with only the all-equal predicate kept (no result register): the
// predicate says whether every active lane holds the same value as the first active lane.
void Visitor::MATCH() {
    const u32 dest = Take(16, 8);
    const u32 source = Take(24, 8);
    const bool wide = Take(73, 1) != 0;
    const bool any = Take(79, 1) != 0;
    const u32 predicate = Take(81, 3);
    Require(dest == 255 && !any, "native warp match mode");
    Require(source < 255 && (!wide || (source % 2 == 0 && source + 1 < 255)), "native warp match source");
    const IR::U32 active{ir.SubgroupBallot(ir.Imm1(true))};
    const IR::U32 first_lane{ir.FindUMsb(ir.BitwiseAnd(active, ir.INeg(active)))};
    IR::U1 equal{ir.IEqual(X(source), ShuffleLane(X(source), first_lane))};
    if (wide) {
        equal = ir.LogicalAnd(equal, ir.IEqual(X(source + 1), ShuffleLane(X(source + 1), first_lane)));
    }
    PredOut(predicate, ir.VoteAll(equal));
}

} // namespace Shader::Ampere
