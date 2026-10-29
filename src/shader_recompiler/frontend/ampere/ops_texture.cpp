// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// Encoding reference: Mesa NAK sm70_encode.rs (MIT), Copyright 2022 Collabora, Ltd.

#include <array>
#include <bit>
#include <functional>

#include "shader_recompiler/frontend/ampere/visitor.h"
#include "shader_recompiler/frontend/ir/modifiers.h"

namespace Shader::Ampere {
namespace {
struct Dimension {
    Shader::TextureType type;
    u32 coordinates; // including the array index
};

Dimension DimensionOf(u32 value) {
    switch (value) {
    case 0:
        return {Shader::TextureType::Color1D, 1};
    case 4:
        return {Shader::TextureType::ColorArray1D, 2};
    case 1:
        return {Shader::TextureType::Color2D, 2};
    case 5:
        return {Shader::TextureType::ColorArray2D, 3};
    case 2:
        return {Shader::TextureType::Color3D, 3};
    case 3:
        return {Shader::TextureType::ColorCube, 3};
    case 7:
        return {Shader::TextureType::ColorArrayCube, 4};
    default:
        return {Shader::TextureType::Color2D, 0};
    }
}
} // Anonymous namespace

void Visitor::TEX() {
    const bool bindless = op == 0xf60; // handle in a uniform register instead of a constant buffer slot
    const bool register_handle = op == 0x361; // handle in a general register after the operands
    const u32 dest = Take(16, 8);
    const u32 source_a = Take(24, 8);
    const u32 source_b = Take(32, 8);
    u32 cbuf_offset = 0;
    u32 cbuf_index = 0;
    u32 handle_register = 0;
    if (register_handle) {
        Require(Take(59, 1) == 1, "native bindless texture handle");
    } else if (bindless) {
        handle_register = Take(40, 6);
        Require(Take(91, 1) == 1 && handle_register < 63, "native bindless texture handle");
    } else {
        cbuf_offset = Take(40, 14) * 4;
        cbuf_index = Take(54, 5);
        Require(Take(59, 1) == 0, "native bindless texture");
    }
    // Without the scalar flag the coordinates are consecutive registers at A and the lod, bias or depth
    // reference sits at B (the vector layout); bit 79 selects half precision results.
    const bool scalar = Take(60, 1) != 0;
    const Dimension dim = DimensionOf(Take(61, 3));
    Require(dim.coordinates != 0, "native texture dimension");
    const u32 dest_b = Take(64, 8);
    const u32 mask = Take(72, 4);
    // Bit 76 adds a texel offset register at B (2D, no lod register): two signed 4-bit fields, x in
    // bits 0..3 and y in 4..7, exactly as the Maxwell translation reads it.
    const bool texel_offset = Take(76, 1) != 0;
    Require(Take(77, 1) == 0, "native non-divergent texture derivative");
    const bool depth_compare = Take(78, 1) != 0;
    const bool half = Take(79, 1) != 0;
    Require(Take(81, 3) == 7, "native sparse texture residency");
    Ignore(84, 3); // cache eviction priority
    const u32 lod_mode = Take(87, 3);
    Ignore(90, 1); // nodep scoreboard hint
    Require(lod_mode <= 3, "native texture clamp mode");
    Require(mask != 0, "native empty texture channel mask");
    if (!bindless && !register_handle) {
        Require(cbuf_index < 18, "native texture descriptor buffer");
        // The descriptor table is the constant buffer named by the binding field.
        if (program_info) {
            Require(program_info->texture_buffer == ~0U || program_info->texture_buffer == cbuf_index,
                    "native texture descriptor buffer conflict");
            program_info->texture_buffer = cbuf_index;
        } else {
            Require(env.TextureBoundBuffer() == cbuf_index, "native texture descriptor buffer");
        }
    }

    // Operands are one list (array index first, then coordinates, lod or bias, depth reference)
    // spread over two register groups: a single register each for two operands, otherwise a pair
    // at A followed by the remainder at B.
    const bool has_lod = lod_mode == 2 || lod_mode == 3;
    const u32 count = dim.coordinates + (has_lod ? 1 : 0) + (depth_compare ? 1 : 0);
    Require(count <= 6, "native texture operand count");
    std::array<u32, 8> regs{};
    u32 gpr_handle = 0;
    u32 offset_register = source_b;
    if (texel_offset) {
        // Coordinates start at A. A lod register, when there is one (explicit lod), is B and the
        // offset register follows it (B + 1); otherwise the offset register is B.
        Require(scalar && !half && !register_handle && dim.type == Shader::TextureType::Color2D &&
                    !depth_compare && (lod_mode <= 1 || lod_mode == 3) && source_a % 2 == 0 &&
                    source_a + 1 < 255 && source_b + 1 < 255 && (lod_mode != 3 || source_b % 2 == 0),
                "native texture offset");
        regs[0] = source_a;
        regs[1] = source_a + 1;
        if (lod_mode == 3) {
            regs[2] = source_b;
            offset_register = source_b + 1;
        }
    } else if (register_handle) {
        // Coordinates are consecutive registers from A; the lod, bias or depth reference and then the
        // handle follow from B (as the reference lowering lays out the 2D and cube forms).
        Require(scalar && !half && count <= dim.coordinates + 1, "native bindless texture operands");
        for (u32 k = 0; k < count; ++k) {
            regs[k] = k < dim.coordinates ? source_a + k : source_b;
            Require(regs[k] < 255, "native texture operand register");
        }
        gpr_handle = source_b + (count > dim.coordinates ? 1 : 0);
        Require(gpr_handle < 255, "native bindless texture handle");
    } else if (scalar) {
        for (u32 k = 0; k < count; ++k) {
            u32 reg;
            if (count <= 2) {
                reg = k == 0 ? source_a : source_b;
            } else {
                reg = k < 2 ? source_a + k : source_b + (k - 2);
            }
            Require(reg < 255, "native texture operand register");
            regs[k] = reg;
        }
    } else {
        // Vector layout: every coordinate (array index first) follows from A, then the one extra
        // operand, if any, is B.
        Require(count <= dim.coordinates + 1, "native vector texture operand count");
        for (u32 k = 0; k < count; ++k) {
            regs[k] = k < dim.coordinates ? source_a + k : source_b;
            Require(regs[k] < 255, "native texture operand register");
        }
    }
    u32 next = 0;
    const auto array_index = [&]() -> IR::F32 {
        const IR::U32 raw = X(regs[next++]);
        return IR::F32{
            ir.ConvertUToF(32, 16, ir.BitFieldExtract(raw, ir.Imm32(0), ir.Imm32(16)))};
    };
    IR::Value coords;
    switch (dim.type) {
    case Shader::TextureType::Color1D:
        coords = F(regs[next++]);
        break;
    case Shader::TextureType::ColorArray1D: {
        const IR::F32 layer = array_index();
        coords = ir.CompositeConstruct(F(regs[next++]), layer);
        break;
    }
    case Shader::TextureType::Color2D: {
        const IR::F32 x = F(regs[next++]);
        coords = ir.CompositeConstruct(x, F(regs[next++]));
        break;
    }
    case Shader::TextureType::ColorArray2D: {
        const IR::F32 layer = array_index();
        const IR::F32 x = F(regs[next++]);
        coords = ir.CompositeConstruct(x, F(regs[next++]), layer);
        break;
    }
    case Shader::TextureType::Color3D:
    case Shader::TextureType::ColorCube: {
        const IR::F32 x = F(regs[next++]);
        const IR::F32 y = F(regs[next++]);
        coords = ir.CompositeConstruct(x, y, F(regs[next++]));
        break;
    }
    case Shader::TextureType::ColorArrayCube: {
        const IR::F32 layer = array_index();
        const IR::F32 x = F(regs[next++]);
        const IR::F32 y = F(regs[next++]);
        coords = ir.CompositeConstruct(x, y, F(regs[next++]), layer);
        break;
    }
    default:
        Unsupported("native texture dimension");
    }
    IR::F32 lod = ir.Imm32(0.0f);
    if (has_lod) {
        lod = F(regs[next++]);
    }
    IR::F32 dref;
    if (depth_compare) {
        dref = F(regs[next++]);
    }

    IR::TextureInstInfo info{};
    info.type.Assign(dim.type);
    info.is_depth.Assign(depth_compare ? 1 : 0);
    info.has_bias.Assign(lod_mode == 2 ? 1 : 0);
    info.relaxed_precision.Assign(half ? 1 : 0);
    const IR::U32 handle = register_handle ? X(gpr_handle)
                                           : bindless ? UR(handle_register) : ir.Imm32(cbuf_offset);
    const bool explicit_lod = lod_mode == 1 || lod_mode == 3;
    const IR::F32 bias = lod_mode == 2 ? lod : IR::F32{};
    IR::Value offset;
    if (texel_offset) {
        const IR::U32 packed{X(offset_register)};
        offset = ir.CompositeConstruct(ir.BitFieldExtract(packed, ir.Imm32(0), ir.Imm32(4), true),
                                       ir.BitFieldExtract(packed, ir.Imm32(4), ir.Imm32(4), true));
    }
    const IR::Value sample{[&]() -> IR::Value {
        if (!depth_compare) {
            if (explicit_lod) {
                return ir.ImageSampleExplicitLod(handle, coords, lod, offset, info);
            }
            return ir.ImageSampleImplicitLod(handle, coords, bias, offset, {}, info);
        }
        if (explicit_lod) {
            return ir.ImageSampleDrefExplicitLod(handle, coords, dref, lod, {}, info);
        }
        return ir.ImageSampleDrefImplicitLod(handle, coords, dref, bias, {}, {}, info);
    }()};

    // Channels in R,G,B,A order are packed into dest, dest + 1, then dest_b, dest_b + 1.
    const u32 outputs = static_cast<u32>(std::popcount(mask));
    Require(outputs <= 2 ? dest_b == 255 : dest_b != 255, "native texture result layout");
    u32 stored = 0;
    std::array<IR::F32, 4> values;
    for (u32 component = 0; component < 4; ++component) {
        if (((mask >> component) & 1) == 0) {
            continue;
        }
        IR::F32 value;
        if (depth_compare) {
            value = component < 3 ? IR::F32{sample} : ir.Imm32(1.0f);
        } else {
            value = IR::F32{ir.CompositeExtract(sample, component)};
        }
        if (!half) {
            const u32 target = stored < 2 ? dest + stored : dest_b + (stored - 2);
            Require(target < 255, "native texture result register");
            F(target, value);
        } else {
            values[stored] = value;
        }
        ++stored;
    }
    if (half) {
        // Half precision results: pairs of channels, in order, share one register (low half first);
        // a missing last channel reads as zero, as for the Maxwell TEXS.F16 encoding.
        const auto channel = [&](u32 k) -> IR::F32 {
            return k < stored ? values[k] : IR::F32{ir.Imm32(0.0f)};
        };
        Require(dest < 255, "native texture result register");
        X(dest, ir.PackHalf2x16(ir.CompositeConstruct(channel(0), channel(1))));
        if (stored > 2) {
            Require(dest_b < 255, "native texture result register");
            X(dest_b, ir.PackHalf2x16(ir.CompositeConstruct(channel(2), channel(3))));
        }
    }
}

void Visitor::TLD() {
    const bool bindless = op == 0xf66; // handle in a uniform register instead of a constant buffer slot
    const u32 dest = Take(16, 8);
    const u32 source_a = Take(24, 8);
    const u32 source_b = Take(32, 8);
    u32 cbuf_offset = 0;
    u32 cbuf_index = 0;
    u32 handle_register = 0;
    if (bindless) {
        handle_register = Take(40, 6);
        Require(Take(91, 1) == 1 && handle_register < 63, "native bindless texture handle");
    } else {
        cbuf_offset = Take(40, 14) * 4;
        cbuf_index = Take(54, 5);
        Require(Take(59, 1) == 0, "native bindless texture");
    }
    Require(Take(60, 1) != 0, "native packed half-precision texel fetch result");
    const Dimension dim = DimensionOf(Take(61, 3));
    Require(dim.coordinates != 0, "native texture dimension");
    const u32 dest_b = Take(64, 8);
    const u32 mask = Take(72, 4);
    Require(Take(76, 1) == 0, "native texel fetch offset");
    Require(Take(77, 2) == 0, "native texel fetch multisample or clamp");
    const bool half = Take(79, 1) != 0; // bit 79: half precision results (.F16)
    Require(Take(81, 3) == 7, "native sparse texture residency");
    Ignore(84, 3);
    const u32 lod_mode = Take(87, 3);
    Ignore(90, 1);
    Require(lod_mode == 1 || lod_mode == 3, "native texel fetch lod mode");
    Require(mask != 0, "native empty texture channel mask");
    if (!bindless) {
        Require(cbuf_index < 18, "native texture descriptor buffer");
        if (program_info) {
            Require(program_info->texture_buffer == ~0U || program_info->texture_buffer == cbuf_index,
                    "native texture descriptor buffer conflict");
            program_info->texture_buffer = cbuf_index;
        } else {
            Require(env.TextureBoundBuffer() == cbuf_index, "native texture descriptor buffer");
        }
    }
    const bool has_lod = lod_mode == 3;
    const u32 count = dim.coordinates + (has_lod ? 1 : 0);
    std::array<u32, 8> regs{};
    for (u32 k = 0; k < count; ++k) {
        u32 reg;
        if (count <= 2) {
            reg = k == 0 ? source_a : source_b;
        } else {
            reg = k < 2 ? source_a + k : source_b + (k - 2);
        }
        Require(reg < 255, "native texture operand register");
        regs[k] = reg;
    }
    u32 next = 0;
    const auto array_index = [&]() -> IR::U32 {
        return ir.BitFieldExtract(X(regs[next++]), ir.Imm32(0), ir.Imm32(16));
    };
    IR::Value coords;
    switch (dim.type) {
    case Shader::TextureType::Color1D:
        coords = X(regs[next++]);
        break;
    case Shader::TextureType::ColorArray1D: {
        const IR::U32 layer = array_index();
        coords = ir.CompositeConstruct(X(regs[next++]), layer);
        break;
    }
    case Shader::TextureType::Color2D: {
        const IR::U32 x = X(regs[next++]);
        coords = ir.CompositeConstruct(x, X(regs[next++]));
        break;
    }
    case Shader::TextureType::ColorArray2D: {
        const IR::U32 layer = array_index();
        const IR::U32 x = X(regs[next++]);
        coords = ir.CompositeConstruct(x, X(regs[next++]), layer);
        break;
    }
    case Shader::TextureType::Color3D: {
        const IR::U32 x = X(regs[next++]);
        const IR::U32 y = X(regs[next++]);
        coords = ir.CompositeConstruct(x, y, X(regs[next++]));
        break;
    }
    default:
        Unsupported("native texel fetch dimension");
    }
    const IR::U32 lod = has_lod ? X(regs[next++]) : ir.Imm32(0U);
    IR::TextureInstInfo info{};
    info.type.Assign(dim.type);
    const IR::U32 handle = bindless ? UR(handle_register) : ir.Imm32(cbuf_offset);
    const IR::Value sample{ir.ImageFetch(handle, coords, {}, lod, {}, info)};
    const u32 outputs = static_cast<u32>(std::popcount(mask));
    Require(outputs <= 2 ? dest_b == 255 : dest_b != 255, "native texture result layout");
    u32 stored = 0;
    std::array<IR::F32, 4> values;
    for (u32 component = 0; component < 4; ++component) {
        if (((mask >> component) & 1) == 0) {
            continue;
        }
        const IR::F32 value{ir.CompositeExtract(sample, component)};
        if (half) {
            values[stored] = value;
        } else {
            const u32 target = stored < 2 ? dest + stored : dest_b + (stored - 2);
            Require(target < 255, "native texture result register");
            F(target, value);
        }
        ++stored;
    }
    if (half) {
        // Pairs of channels share one register as half precision; a missing last channel is zero.
        const auto channel = [&](u32 k) -> IR::F32 {
            return k < stored ? values[k] : IR::F32{ir.Imm32(0.0f)};
        };
        Require(dest < 255, "native texture result register");
        X(dest, ir.PackHalf2x16(ir.CompositeConstruct(channel(0), channel(1))));
        if (stored > 2) {
            Require(dest_b < 255, "native texture result register");
            X(dest_b, ir.PackHalf2x16(ir.CompositeConstruct(channel(2), channel(3))));
        }
    }
}


namespace {
// Image dimension codes of surface instructions (NAK ImageDim).
Dimension ImageDimensionOf(u32 value) {
    switch (value) {
    case 0:
        return {Shader::TextureType::Color1D, 1};
    case 1:
        return {Shader::TextureType::Buffer, 1};
    case 2:
        return {Shader::TextureType::ColorArray1D, 2};
    case 3:
        return {Shader::TextureType::Color2D, 2};
    case 4:
        return {Shader::TextureType::ColorArray2D, 3};
    case 5:
        return {Shader::TextureType::Color3D, 3};
    default:
        return {Shader::TextureType::Color2D, 0};
    }
}
} // Anonymous namespace

// Registers the descriptor buffer a surface or texture instruction names.
void Visitor::UseDescriptorBuffer(u32 cbuf_index) {
    Require(cbuf_index < 18, "native descriptor buffer");
    if (program_info) {
        Require(program_info->texture_buffer == ~0U || program_info->texture_buffer == cbuf_index,
                "native descriptor buffer conflict");
        program_info->texture_buffer = cbuf_index;
    } else {
        Require(env.TextureBoundBuffer() == cbuf_index, "native descriptor buffer");
    }
}

namespace {
// Integer coordinates of a surface access starting at `reg`; an array index is the low 16 bits.
IR::Value SurfaceCoordinates(IR::IREmitter& ir, const Dimension& dim, u32 reg,
                             const std::function<IR::U32(u32)>& read) {
    const auto x = [&](u32 k) { return read(reg + k); };
    switch (dim.type) {
    case Shader::TextureType::Color1D:
    case Shader::TextureType::Buffer:
        return x(0);
    case Shader::TextureType::ColorArray1D:
        return ir.CompositeConstruct(x(0), ir.BitFieldExtract(x(1), ir.Imm32(0), ir.Imm32(16)));
    case Shader::TextureType::Color2D:
        return ir.CompositeConstruct(x(0), x(1));
    case Shader::TextureType::ColorArray2D:
        return ir.CompositeConstruct(x(0), x(1), ir.BitFieldExtract(x(2), ir.Imm32(0), ir.Imm32(16)));
    default:
        return ir.CompositeConstruct(x(0), x(1), x(2));
    }
}
} // Anonymous namespace

// SUST: formatted surface store. The data registers hold the channels selected by the mask in order;
// only masks covering the leading channels are modelled, since the store writes whole texels.
void Visitor::SUST() {
    const u32 coord_reg = Take(24, 8);
    const u32 data_reg = Take(32, 8);
    const u32 cbuf_offset = Take(40, 14) * 4;
    const u32 cbuf_index = Take(54, 5);
    Require(Take(59, 2) == 0, "native surface handle mode");
    const Dimension dim = ImageDimensionOf(Take(61, 3));
    Require(dim.coordinates != 0, "native surface dimension");
    const u32 mask = Take(72, 4);
    Ignore(76, 4); // memory ordering
    Ignore(84, 3); // cache eviction priority
    Require(mask == 1 || mask == 3 || mask == 7 || mask == 15, "native surface channel mask");
    UseDescriptorBuffer(cbuf_index);
    const u32 count = static_cast<u32>(std::popcount(mask));
    Require(data_reg + count < 255 && coord_reg + dim.coordinates < 255, "native surface registers");
    const IR::Value coords{SurfaceCoordinates(ir, dim, coord_reg, [this](u32 r) { return X(r); })};
    std::array<IR::U32, 4> colors;
    for (u32 k = 0; k < 4; ++k) {
        colors[k] = k < count ? X(data_reg + k) : ir.Imm32(0U);
    }
    IR::TextureInstInfo info{};
    info.type.Assign(dim.type);
    info.image_format.Assign(ImageFormat::Typeless);
    ir.ImageWrite(ir.Imm32(cbuf_offset), coords,
                  ir.CompositeConstruct(colors[0], colors[1], colors[2], colors[3]), info);
}

// SULD: formatted surface load into consecutive registers, one per selected channel.
void Visitor::SULD() {
    const u32 dest = Take(16, 8);
    const u32 coord_reg = Take(24, 8);
    const u32 cbuf_offset = Take(40, 14) * 4;
    const u32 cbuf_index = Take(54, 5);
    Require(Take(59, 2) == 0, "native surface handle mode");
    const Dimension dim = ImageDimensionOf(Take(61, 3));
    Require(dim.coordinates != 0, "native surface dimension");
    const u32 mask = Take(72, 4);
    Ignore(76, 4); // memory ordering
    Require(Take(81, 3) == 7, "native sparse surface residency");
    Ignore(84, 3);
    Require(mask != 0, "native empty surface channel mask");
    UseDescriptorBuffer(cbuf_index);
    Require(coord_reg + dim.coordinates < 255, "native surface registers");
    const IR::Value coords{SurfaceCoordinates(ir, dim, coord_reg, [this](u32 r) { return X(r); })};
    IR::TextureInstInfo info{};
    info.type.Assign(dim.type);
    info.image_format.Assign(ImageFormat::Typeless);
    const IR::Value result{ir.ImageRead(ir.Imm32(cbuf_offset), coords, info)};
    u32 stored = 0;
    for (u32 component = 0; component < 4; ++component) {
        if (((mask >> component) & 1) == 0) {
            continue;
        }
        Require(dest + stored < 255, "native surface result register");
        X(dest + stored, IR::U32{ir.CompositeExtract(result, component)});
        ++stored;
    }
}

// TLD4: gather of one component, descriptor in a constant buffer (0xb63) or a uniform register (0xf63).
void Visitor::TLD4() {
    const bool bindless = op == 0xf63;
    const u32 dest = Take(16, 8);
    const u32 source_a = Take(24, 8);
    const u32 source_b = Take(32, 8);
    u32 cbuf_offset = 0;
    u32 handle_register = 0;
    if (bindless) {
        handle_register = Take(40, 6);
        Require(Take(91, 1) == 1 && handle_register < 63, "native bindless texture handle");
    } else {
        cbuf_offset = Take(40, 14) * 4;
        UseDescriptorBuffer(Take(54, 5));
        Require(Take(59, 1) == 0, "native bindless texture");
    }
    Require(Take(60, 1) != 0, "native packed half-precision gather result");
    const Dimension dim = DimensionOf(Take(61, 3));
    Require(dim.coordinates != 0, "native texture dimension");
    const u32 dest_b = Take(64, 8);
    const u32 mask = Take(72, 4);
    // 1: one packed register of signed 6-bit offsets (x at bit 0, y at bit 8), 2: per-texel offsets in two
    // registers (four x/y pairs). The registers follow the coordinates and precede the depth reference
    // (the order of the Maxwell instruction; the combination with a reference is not seen in logs).
    const u32 offset_mode = Take(76, 2);
    Require(offset_mode <= 2, "native gather offset");
    const bool depth_compare = Take(78, 1) != 0;
    Require(Take(81, 3) == 7, "native sparse texture residency");
    Ignore(84, 3);
    const u32 component = Take(87, 2);
    Ignore(90, 1);
    Require(mask != 0, "native empty texture channel mask");
    const u32 offset_registers = offset_mode == 0 ? 0 : offset_mode;
    const u32 count = dim.coordinates + offset_registers + (depth_compare ? 1 : 0);
    Require(offset_mode == 0 || dim.type == Shader::TextureType::Color2D ||
                dim.type == Shader::TextureType::ColorArray2D, "native gather offset dimension");
    std::array<u32, 8> regs{};
    for (u32 k = 0; k < count; ++k) {
        const u32 reg = count <= 2 ? (k == 0 ? source_a : source_b)
                                   : (k < 2 ? source_a + k : source_b + (k - 2));
        Require(reg < 255, "native texture operand register");
        regs[k] = reg;
    }
    u32 next = 0;
    IR::Value coords;
    switch (dim.type) {
    case Shader::TextureType::Color2D: {
        const IR::F32 x = F(regs[next++]);
        coords = ir.CompositeConstruct(x, F(regs[next++]));
        break;
    }
    case Shader::TextureType::ColorArray2D: {
        const IR::F32 layer{
            ir.ConvertUToF(32, 16, ir.BitFieldExtract(X(regs[next++]), ir.Imm32(0), ir.Imm32(16)))};
        const IR::F32 x = F(regs[next++]);
        coords = ir.CompositeConstruct(x, F(regs[next++]), layer);
        break;
    }
    case Shader::TextureType::ColorCube: {
        const IR::F32 x = F(regs[next++]);
        const IR::F32 y = F(regs[next++]);
        coords = ir.CompositeConstruct(x, y, F(regs[next++]));
        break;
    }
    default:
        Unsupported("native gather dimension");
    }
    IR::Value offset;
    IR::Value offset2;
    const auto pair = [&](const IR::U32& value) {
        return ir.CompositeConstruct(ir.BitFieldExtract(value, ir.Imm32(0), ir.Imm32(6), true),
                                     ir.BitFieldExtract(value, ir.Imm32(8), ir.Imm32(6), true));
    };
    const auto quad = [&](const IR::U32& value) {
        return ir.CompositeConstruct(ir.BitFieldExtract(value, ir.Imm32(0), ir.Imm32(6), true),
                                     ir.BitFieldExtract(value, ir.Imm32(8), ir.Imm32(6), true),
                                     ir.BitFieldExtract(value, ir.Imm32(16), ir.Imm32(6), true),
                                     ir.BitFieldExtract(value, ir.Imm32(24), ir.Imm32(6), true));
    };
    if (offset_mode == 1) {
        offset = pair(X(regs[next++]));
    } else if (offset_mode == 2) {
        const IR::U32 first{X(regs[next++])};
        const IR::U32 second{X(regs[next++])};
        offset = quad(first);
        offset2 = quad(second);
    }
    IR::F32 dref;
    if (depth_compare) {
        dref = F(regs[next++]);
    }
    IR::TextureInstInfo info{};
    info.type.Assign(dim.type);
    info.is_depth.Assign(depth_compare ? 1 : 0);
    info.gather_component.Assign(component);
    const IR::U32 handle = bindless ? UR(handle_register) : ir.Imm32(cbuf_offset);
    const IR::Value sample = depth_compare ? ir.ImageGatherDref(handle, coords, offset, offset2, dref, info)
                                           : ir.ImageGather(handle, coords, offset, offset2, info);
    const u32 outputs = static_cast<u32>(std::popcount(mask));
    Require(outputs <= 2 ? dest_b == 255 : dest_b != 255, "native texture result layout");
    u32 stored = 0;
    for (u32 c = 0; c < 4; ++c) {
        if (((mask >> c) & 1) == 0) {
            continue;
        }
        const u32 target = stored < 2 ? dest + stored : dest_b + (stored - 2);
        Require(target < 255, "native texture result register");
        F(target, IR::F32{ir.CompositeExtract(sample, c)});
        ++stored;
    }
}

// TXQ: texture dimension query through a bound descriptor; the selected channels of
// (width, height, depth, mip count) go to consecutive registers.
void Visitor::TXQ() {
    const u32 dest = Take(16, 8);
    const u32 lod_reg = Take(24, 8);
    const u32 cbuf_offset = Take(40, 14) * 4;
    UseDescriptorBuffer(Take(54, 5));
    Require(Take(59, 3) == 0, "native texture query handle mode");
    Require(Take(62, 2) == 0, "native texture query kind");
    Require(Take(64, 8) == 255, "native texture query extra register");
    const u32 mask = Take(72, 4);
    Ignore(90, 1);
    Require(mask != 0, "native empty texture query mask");
    const IR::Value query{ir.ImageQueryDimension(ir.Imm32(cbuf_offset), X(lod_reg), ir.Imm1(((mask >> 3) & 1) == 0))};
    u32 stored = 0;
    for (u32 element = 0; element < 4; ++element) {
        if (((mask >> element) & 1) == 0) {
            continue;
        }
        Require(dest + stored < 255, "native texture query result register");
        X(dest + stored, IR::U32{ir.CompositeExtract(query, element)});
        ++stored;
    }
}

// TXD: texture sample with explicit gradients through a bound descriptor. The coordinates and the
// four derivatives are consecutive registers (an array layer follows the coordinates); the selected
// channels go to the first destination (two channels) and the second one (the rest).
void Visitor::TXD() {
    const u32 dest = Take(16, 8);
    const u32 coord_reg = Take(24, 8);
    const u32 derivative_reg = Take(32, 8);
    const u32 cbuf_offset = Take(40, 14) * 4;
    UseDescriptorBuffer(Take(54, 5));
    Require(Take(59, 2) == 0, "native gradient texture handle mode");
    const Dimension dim = DimensionOf(Take(61, 3));
    const u32 dest_b = Take(64, 8);
    const u32 mask = Take(72, 4);
    Require(Take(76, 1) == 0, "native gradient texture offset");
    Require(Take(81, 3) == 7, "native sparse texture residency");
    Ignore(84, 3);
    Ignore(90, 1);
    Require(mask != 0, "native empty texture channel mask");
    Require(dim.type == Shader::TextureType::Color2D || dim.type == Shader::TextureType::ColorArray2D,
            "native gradient texture dimension");
    const bool array = dim.type == Shader::TextureType::ColorArray2D;
    Require(coord_reg + (array ? 2 : 1) < 255 && derivative_reg + 3 < 255, "native gradient operand registers");
    IR::Value coords;
    if (array) {
        const IR::F32 layer{
            ir.ConvertUToF(32, 16, ir.BitFieldExtract(X(coord_reg + 2), ir.Imm32(0), ir.Imm32(16)))};
        coords = ir.CompositeConstruct(F(coord_reg), F(coord_reg + 1), layer);
    } else {
        coords = ir.CompositeConstruct(F(coord_reg), F(coord_reg + 1));
    }
    const IR::Value derivatives{ir.CompositeConstruct(F(derivative_reg), F(derivative_reg + 1),
                                                      F(derivative_reg + 2), F(derivative_reg + 3))};
    IR::TextureInstInfo info{};
    info.type.Assign(dim.type);
    info.num_derivatives.Assign(2);
    info.has_lod_clamp.Assign(0);
    const IR::Value sample{ir.ImageGradient(ir.Imm32(cbuf_offset), coords, derivatives, {}, {}, info)};
    const u32 outputs = static_cast<u32>(std::popcount(mask));
    Require(outputs <= 2 ? dest_b == 255 : dest_b != 255, "native texture result layout");
    u32 stored = 0;
    for (u32 c = 0; c < 4; ++c) {
        if (((mask >> c) & 1) == 0) {
            continue;
        }
        const u32 target = stored < 2 ? dest + stored : dest_b + (stored - 2);
        Require(target < 255, "native texture result register");
        F(target, IR::F32{ir.CompositeExtract(sample, c)});
        ++stored;
    }
}

// TMML: mipmap level query (clamped and unclamped LOD, as 8.8 fixed point) through a bound descriptor. The coordinates
// are consecutive registers starting at source A; an array index register is skipped, as the
// Maxwell translation does.
void Visitor::TMML() {
    const u32 dest = Take(16, 8);
    const u32 coord_reg = Take(24, 8);
    Require(Take(32, 8) == 255, "native mipmap query extra register");
    const u32 cbuf_offset = Take(40, 14) * 4;
    UseDescriptorBuffer(Take(54, 5));
    Require(Take(59, 2) == 0, "native mipmap query handle mode");
    const Dimension dim = DimensionOf(Take(61, 3));
    Require(dim.coordinates != 0, "native texture dimension");
    Require(Take(64, 8) == 255, "native mipmap query extra register");
    const u32 mask = Take(72, 4);
    Require((mask & 0xc) == 0 && mask != 0, "native mipmap query channels");
    Ignore(77, 1);
    Ignore(90, 1);
    Require(coord_reg + dim.coordinates < 255, "native mipmap query registers");
    IR::Value coords;
    switch (dim.type) {
    case Shader::TextureType::Color1D:
        coords = F(coord_reg);
        break;
    case Shader::TextureType::ColorArray1D:
        coords = F(coord_reg + 1);
        break;
    case Shader::TextureType::Color2D:
        coords = ir.CompositeConstruct(F(coord_reg), F(coord_reg + 1));
        break;
    case Shader::TextureType::ColorArray2D:
        coords = ir.CompositeConstruct(F(coord_reg + 1), F(coord_reg + 2));
        break;
    case Shader::TextureType::Color3D:
    case Shader::TextureType::ColorCube:
        coords = ir.CompositeConstruct(F(coord_reg), F(coord_reg + 1), F(coord_reg + 2));
        break;
    default:
        coords = ir.CompositeConstruct(F(coord_reg + 1), F(coord_reg + 2), F(coord_reg + 3));
        break;
    }
    IR::TextureInstInfo info{};
    info.type.Assign(dim.type);
    const IR::Value lod{ir.ImageQueryLod(ir.Imm32(cbuf_offset), coords, info)};
    u32 stored = 0;
    for (u32 element = 0; element < 2; ++element) {
        if (((mask >> element) & 1) == 0) {
            continue;
        }
        Require(dest + stored < 255, "native mipmap query result register");
        // The hardware returns the LODs as 8.8 fixed-point 16-bit integers (shaders convert them back
        // with I2F.S16 and a scale). The host query returns absolute then relative LOD, and TMML the
        // reverse; the first result is signed and the second unsigned.
        const IR::F32 level{ir.CompositeExtract(lod, element ^ 1)};
        const IR::F32 scaled{ir.FPMul(level, ir.Imm32(256.0f))};
        const IR::U32 fixed{element == 0 ? IR::U32{ir.ConvertFToS(32, scaled)}
                                         : IR::U32{ir.ConvertFToU(32, scaled)}};
        X(dest + stored, ir.BitwiseAnd(fixed, ir.Imm32(0xffffU)));
        ++stored;
    }
}

// SUATOM (bound descriptor): atomic operation on one texel of a surface, returning the old value.
// Laid out like SUST/SULD: destination 16, coordinates 24, data 32, descriptor 40..58, dimension
// 61..63, type 73..75, fault predicate 81..83 and the operation at 87..90 (NAK's SuAtom fields).
void Visitor::SUATOM() {
    const u32 dest = Take(16, 8);
    const u32 coord_reg = Take(24, 8);
    const u32 data_reg = Take(32, 8);
    const u32 cbuf_offset = Take(40, 14) * 4;
    UseDescriptorBuffer(Take(54, 5));
    Require(Take(59, 2) == 0, "native surface handle mode");
    const Dimension dim = ImageDimensionOf(Take(61, 3));
    Require(dim.coordinates != 0, "native surface dimension");
    Require(Take(72, 1) == 0, "native surface atomic mode");
    const u32 type = Take(73, 3);
    Ignore(77, 4); // memory ordering
    Require(Take(81, 3) == 7, "native sparse surface residency");
    Ignore(84, 3);
    const u32 atom_op = Take(87, 4);
    Require(type <= 1 && atom_op <= 8, "native surface atomic operation");
    Require(coord_reg + dim.coordinates < 255, "native surface registers");
    const IR::Value coords{SurfaceCoordinates(ir, dim, coord_reg, [this](u32 r) { return X(r); })};
    IR::TextureInstInfo info{};
    info.type.Assign(dim.type);
    info.image_format.Assign(ImageFormat::Typeless);
    const IR::Value handle{ir.Imm32(cbuf_offset)};
    const IR::Value value{X(data_reg)};
    const bool is_signed = type == 1;
    IR::Value old;
    switch (atom_op) {
    case 0:
        old = ir.ImageAtomicIAdd(handle, coords, value, info);
        break;
    case 1:
        old = ir.ImageAtomicIMin(handle, coords, value, is_signed, info);
        break;
    case 2:
        old = ir.ImageAtomicIMax(handle, coords, value, is_signed, info);
        break;
    case 3:
        old = ir.ImageAtomicInc(handle, coords, value, info);
        break;
    case 4:
        old = ir.ImageAtomicDec(handle, coords, value, info);
        break;
    case 5:
        old = ir.ImageAtomicAnd(handle, coords, value, info);
        break;
    case 6:
        old = ir.ImageAtomicOr(handle, coords, value, info);
        break;
    case 7:
        old = ir.ImageAtomicXor(handle, coords, value, info);
        break;
    default:
        old = ir.ImageAtomicExchange(handle, coords, value, info);
        break;
    }
    if (dest != 255) {
        X(dest, IR::U32{old});
    }
}

} // namespace Shader::Ampere
