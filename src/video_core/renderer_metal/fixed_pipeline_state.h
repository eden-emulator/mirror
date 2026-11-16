// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstring>
#include <type_traits>

#include "common/bit_field.h"
#include "common/common_funcs.h"
#include "common/common_types.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/metal_common/metal_wrapper.h"
#include "video_core/surface.h"

namespace Metal {

constexpr size_t MAX_VERTEX_ATTRIBUTES = 32;
constexpr size_t MAX_VERTEX_BUFFERS = 32;
constexpr size_t MAX_COLOR_ATTACHMENTS = 8;

#pragma pack(push, 1)
struct FixedPipelineState {
    struct VertexAttribute {
        u32 buffer_index{};
        u32 offset{};
        MTL::VertexFormat format{MTL::VertexFormatInvalid};

        [[nodiscard]] bool operator==(const VertexAttribute&) const noexcept = default;
    };

    struct VertexBinding {
        u32 stride{};
        u32 divisor{};
        bool instanced{};
        bool enabled{};

        [[nodiscard]] bool operator==(const VertexBinding&) const noexcept = default;
    };

    struct ColorAttachment {
        MTL::PixelFormat format{MTL::PixelFormatInvalid};
        MTL::ColorWriteMask write_mask{MTL::ColorWriteMaskAll};

        bool blend_enable{};
        MTL::BlendFactor src_rgb{MTL::BlendFactorOne};
        MTL::BlendFactor dst_rgb{MTL::BlendFactorZero};
        MTL::BlendOperation rgb_op{MTL::BlendOperationAdd};
        MTL::BlendFactor src_alpha{MTL::BlendFactorOne};
        MTL::BlendFactor dst_alpha{MTL::BlendFactorZero};
        MTL::BlendOperation alpha_op{MTL::BlendOperationAdd};

        [[nodiscard]] bool operator==(const ColorAttachment&) const noexcept = default;
    };

    std::array<VertexAttribute, MAX_VERTEX_ATTRIBUTES> attributes{};
    std::array<VertexBinding, MAX_VERTEX_BUFFERS> bindings{};
    std::array<ColorAttachment, MAX_COLOR_ATTACHMENTS> color_attachments{};

    u32 num_color_attachments{};
    u32 sample_count{1};
    u32 alpha_test_func{};
    u32 alpha_test_ref{};
    u32 dual_source_blend{};
    u32 point_size{};
    std::array<u8, MAX_VERTEX_ATTRIBUTES> attribute_types{};
    std::array<u8, MAX_COLOR_ATTACHMENTS> color_output_types{};

    MTL::PrimitiveTopologyClass topology_class{MTL::PrimitiveTopologyClassTriangle};
    u8 topology{};

    u8 constant_binding{};

    bool alpha_to_coverage{};
    bool alpha_to_one{};
    bool rasterizer_discard{};
    bool uses_constant_attributes{};
    bool y_negate{};
    bool y_direction_negate{};
    bool force_early_z{};
    bool ndc_minus_one_to_one{};
    bool xfb_enabled{};
    u8 tessellation_domain{};
    u8 tessellation_spacing{};
    u8 tessellation_clockwise{};
    u8 patch_control_points{};

    [[nodiscard]] bool operator==(const FixedPipelineState&) const noexcept = default;

    [[nodiscard]] size_t Hash() const noexcept;
};
#pragma pack(pop)

static_assert(std::has_unique_object_representations_v<FixedPipelineState>,
              "FixedPipelineState must be flat so it can be hashed and compared bytewise");

struct DepthStencilState {
    struct StencilFace {
        MTL::CompareFunction compare{MTL::CompareFunctionAlways};
        MTL::StencilOperation fail{MTL::StencilOperationKeep};
        MTL::StencilOperation depth_fail{MTL::StencilOperationKeep};
        MTL::StencilOperation pass{MTL::StencilOperationKeep};
        u32 read_mask{0xFFFFFFFF};
        u32 write_mask{0xFFFFFFFF};

        [[nodiscard]] bool operator==(const StencilFace&) const noexcept = default;
    };

    MTL::CompareFunction depth_compare{MTL::CompareFunctionAlways};
    bool depth_write{};
    bool stencil_enable{};
    StencilFace front{};
    StencilFace back{};

    [[nodiscard]] bool operator==(const DepthStencilState&) const noexcept = default;

    [[nodiscard]] size_t Hash() const noexcept;
};

} // namespace Metal

namespace std {

template <>
struct hash<Metal::FixedPipelineState> {
    size_t operator()(const Metal::FixedPipelineState& state) const noexcept {
        return state.Hash();
    }
};

template <>
struct hash<Metal::DepthStencilState> {
    size_t operator()(const Metal::DepthStencilState& state) const noexcept {
        return state.Hash();
    }
};

} // namespace std
