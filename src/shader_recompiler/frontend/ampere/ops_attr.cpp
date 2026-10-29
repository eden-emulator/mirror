// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// Encoding reference: Mesa NAK sm70_encode.rs (MIT), Copyright 2022 Collabora, Ltd.

#include "shader_recompiler/frontend/ampere/visitor.h"
#include "shader_recompiler/frontend/ir/attribute.h"
#include "shader_recompiler/program_header.h"

namespace Shader::Ampere {

void Visitor::IPA() {
    // Fragment varying read. Only the default sample location and the pass / constant
    // interpolation frequencies are modelled; the flat state comes from the program header.
    const u32 dest = Take(16, 8);
    Require(Take(32, 8) == 255, "native IPA offset register");
    const u32 attribute_index = Take(64, 8);
    // Default and centroid locations read the same interpolant (only multisampled targets tell them apart).
    Require(Take(76, 2) <= 1, "native IPA sample location");
    const u32 frequency = Take(78, 2);
    Require(frequency == 0 || frequency == 1, "native IPA interpolation frequency");
    Require(Take(81, 3) == 7, "native IPA predicate output");

    const IR::Attribute attribute{attribute_index};
    IR::F32 value{ir.GetAttribute(attribute)};
    const bool is_legacy{IR::IsLegacyAttribute(attribute)};
    if (IR::IsGeneric(attribute) || is_legacy) {
        bool is_perspective{is_legacy};
        if (!is_legacy) {
            const ProgramHeader& sph{env.SPH()};
            const u32 attr_index{IR::GenericAttributeIndex(attribute)};
            const std::array input_map{sph.ps.GenericInputMap(attr_index)};
            Shader::PixelImap effective_imap{Shader::PixelImap::Unused};
            for (const Shader::PixelImap component : input_map) {
                if (component != Shader::PixelImap::Unused) {
                    effective_imap = component;
                    break;
                }
            }
            is_perspective = effective_imap == Shader::PixelImap::Perspective ||
                             effective_imap == Shader::PixelImap::Unused;
        }
        if (is_perspective) {
            const IR::F32 position_w{ir.GetAttribute(IR::Attribute::PositionW)};
            value = IR::F32{ir.FPMul(value, position_w)};
        }
    }
    F(dest, value);
}

void Visitor::EXIT() {
    Require(Take(84, 2) == 0, "native conditional/special EXIT");
    Take(87, 4); // extra exit condition: resolved by the control flow builder
    if (env.ShaderStage() != Stage::Fragment) {
        return;
    }
    const ProgramHeader sph{env.SPH()};
    u32 src_reg = 0;
    for (u32 render_target = 0; render_target < 8; ++render_target) {
        if (!sph.ps.HasOutputComponents(render_target)) {
            continue;
        }
        const std::array<bool, 4> mask{sph.ps.EnabledOutputComponents(render_target)};
        for (u32 component = 0; component < 4; ++component) {
            if (mask[component]) {
                Require(src_reg < 255, "native fragment output register range");
                ir.SetFragColor(render_target, component, F(src_reg));
            }
            ++src_reg;
        }
    }
    if (sph.ps.omap.sample_mask != 0) {
        Require(src_reg < 255, "native fragment output register range");
        ir.SetSampleMask(X(src_reg));
    }
    if (sph.ps.omap.depth != 0) {
        Require(src_reg + 1 < 255, "native fragment output register range");
        ir.SetFragDepth(F(src_reg + 1));
    }
}

// ISBERD: reads the internal stage buffer. Follows the semantics of the Maxwell translation: the
// offset register plus immediate selects an entry; the access type 0 copies the offset (adding the
// lane with skew), 3 reads an indexed input attribute, and the output flag reads global memory.
void Visitor::ISBERD() {
    const u32 dest = Take(16, 8);
    const u32 offset_reg = Take(24, 8);
    const u32 immediate = Take(40, 16);
    const u32 mem_type = Take(74, 2);
    const u32 access = Take(76, 2);
    const bool skew = Take(78, 1) != 0;
    const bool output = Take(79, 1) != 0;
    Require(mem_type <= 2, "native internal stage buffer element type");
    IR::U32 entry{ir.Imm32(immediate)};
    if (offset_reg != 255) {
        entry = ir.IAdd(X(offset_reg), entry);
    }
    if (output) {
        if (skew) {
            const IR::U32 lane{ir.LaneId()};
            const IR::U32 scaled{mem_type == 0 ? lane : IR::U32{ir.ShiftLeftLogical(lane, ir.Imm32(mem_type))}};
            entry = ir.IAdd(entry, scaled);
        }
        const IR::U64 address{ir.UConvert(64, entry)};
        if (mem_type == 0) {
            X(dest, ir.LoadGlobalU8(address));
        } else if (mem_type == 1) {
            X(dest, ir.LoadGlobalU16(address));
        } else {
            X(dest, ir.LoadGlobal32(address));
        }
        return;
    }
    if (access == 3) {
        if (skew) {
            entry = ir.IAdd(entry, ir.ShiftLeftLogical(ir.LaneId(), ir.Imm32(2U)));
        }
        X(dest, ir.BitCast<IR::U32>(ir.GetAttributeIndexed(entry)));
        return;
    }
    Require(access == 0, "native internal stage buffer access type");
    const IR::U32 base{X(offset_reg)}; // RZ reads as zero
    X(dest, skew ? IR::U32{ir.IAdd(base, ir.LaneId())} : base);
}

// ISBEWR: writes an output attribute selected by a register. Mirrors ISBERD's attribute access: the
// offset register plus immediate is the attribute address in bytes.
void Visitor::ISBEWR() {
    const u32 offset_reg = Take(24, 8);
    const u32 data = Take(32, 8);
    const u32 immediate = Take(40, 16);
    const u32 mem_type = Take(74, 2);
    const u32 access = Take(76, 2);
    const bool skew = Take(78, 1) != 0;
    const bool output = Take(79, 1) != 0;
    Require(mem_type == 2 && access == 3 && output, "native internal stage buffer store mode");
    IR::U32 entry{ir.Imm32(immediate)};
    if (offset_reg != 255) {
        entry = ir.IAdd(X(offset_reg), entry);
    }
    if (skew) {
        entry = ir.IAdd(entry, ir.ShiftLeftLogical(ir.LaneId(), ir.Imm32(2U)));
    }
    ir.SetAttributeIndexed(entry, F(data), ir.Imm32(0U));
}

} // namespace Shader::Ampere
