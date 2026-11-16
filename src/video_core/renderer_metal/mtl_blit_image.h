// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <unordered_map>

#include "common/common_funcs.h"
#include "common/common_types.h"
#include "video_core/engines/fermi_2d.h"
#include <array>

#include "video_core/metal_common/metal_wrapper.h"
#include "video_core/renderer_metal/mtl_render_pass_state.h"
#include "video_core/texture_cache/types.h"

namespace Metal {

class Device;
class Scheduler;

// Texture-to-texture blits, done as a draw. A blit encoder can only copy
// matching regions of matching formats, and the guest routinely asks for a
// scaled, filtered, format-converting or Y-flipped copy, all of which one
// textured draw handles. Each kind of draw keeps its own pipeline cache, keyed
// on whatever that pipeline state bakes in.
class BlitImageHelper : Common::NonCopyable, Common::NonMoveable {
public:
    explicit BlitImageHelper(const Device& device, Scheduler& scheduler);
    ~BlitImageHelper();

    /// Which direction a conversion draw runs in.
    enum class ConvertKind {
        DepthToColor,
        ColorToDepth,
        DepthToDepth,
    };

    /// Reinterprets one aspect as the other, as a draw, since a blit encoder
    /// cannot cross the depth/colour boundary or rescale. Stencil is not
    /// covered, Metal has no stencil export, so a packed destination keeps the
    /// stencil it already had.
    void Convert(const RenderPassState& pass, MTL::Texture* src, ConvertKind kind,
                 const VideoCommon::Region2D& dst_region,
                 const VideoCommon::Region2D& src_region, bool linear);

    /// Writes every sample of a multisampled image from a single-sample one.
    /// The inverse of a resolve, which Metal has no fixed-function form of, a
    /// blit encoder cannot write into a multisampled texture at all. Both images
    /// have to have the same extent and format.
    void ExpandToMultisample(MTL::Texture* dst, MTL::Texture* src);

    /// One rectangle to expand, in texels. Source and destination extents match,
    /// an expand changes the sample count and never the size.
    struct ExpandRegion {
        u32 src_x;
        u32 src_y;
        u32 dst_x;
        u32 dst_y;
        u32 width;
        u32 height;
    };

    /// The same expand, restricted to @p regions. The attachment is loaded
    /// instead of discarded and each region is drawn under its own viewport and
    /// scissor, so nothing outside it is touched. Separate from the whole-image
    /// form on purpose, that one discards the attachment, which is cheaper and
    /// what an upload means.
    void ExpandToMultisample(MTL::Texture* dst, MTL::Texture* src,
                             std::span<const ExpandRegion> regions);

    /// Packs a separate depth plane and stencil plane into guest D24S8 words.
    /// Metal copies one aspect of a depth/stencil texture at a time, so a
    /// readback arrives as two planes while the guest expects one 32-bit word
    /// per texel. Runs as a kernel because the planes do not exist until the
    /// blits producing them have executed.
    /// @p dst_row_texels and @p plane_row_texels are texel counts and not bytes,
    /// the two differ whenever the guest's pitch is not the copied width.
    void PackDepthStencil(MTL::Buffer* dst, u64 dst_offset, u32 dst_row_texels,
                          MTL::Buffer* planes, u64 depth_offset, u64 stencil_offset,
                          u32 plane_row_texels, u32 width, u32 height);

    /// Clears part of the already-open pass's attachments, as a draw. Metal has
    /// no clear-rect, so a clear restricted to the scissor rectangle has to be
    /// drawn. The rectangle is in attachment texels.
    /// @p color_mask is an MTL::ColorWriteMask and @p stencil_mask the guest's
    /// stencil write mask, so a partial clear leaves unnamed channels and bit
    /// planes untouched.
    void ClearAttachments(const RenderPassState& pass, u32 color_target, bool clear_color,
                          bool clear_depth, bool clear_stencil,
                          const std::array<f64, 4>& color, f32 depth, u32 stencil,
                          s32 left, s32 top, s32 right, s32 bottom,
                          u32 color_mask = 0xf, u32 stencil_mask = 0xff);

    /// Records a blit from @p src into @p dst. Regions are in texel coordinates
    /// of their own images. A source region whose start exceeds its end on an
    /// axis flips that axis, which is how the guest expresses a vertical flip.
    void BlitColor(MTL::Texture* dst, MTL::Texture* src, const VideoCommon::Region2D& dst_region,
                   const VideoCommon::Region2D& src_region,
                   Tegra::Engines::Fermi2D::Filter filter);

    [[nodiscard]] bool IsValid() const noexcept {
        return static_cast<bool>(library);
    }

private:
    /// Returns (building if needed) the pipeline writing to @p format.
    [[nodiscard]] MTL::RenderPipelineState* GetPipeline(MTL::PixelFormat format);

    /// Pipeline for an expand draw into a @p samples-sample @p format target.
    [[nodiscard]] MTL::RenderPipelineState* GetExpandPipeline(MTL::PixelFormat format,
                                                              u32 samples);

    /// Pipeline for a drawn clear, matching the pass's attachment formats and
    /// writing only @p color_target, masked by @p color_mask.
    [[nodiscard]] MTL::RenderPipelineState* GetClearPipeline(const RenderPassState& pass,
                                                             u32 color_target, bool clear_color,
                                                             u32 color_mask);

    [[nodiscard]] MTL::RenderPipelineState* GetConvertPipeline(const RenderPassState& pass,
                                                               ConvertKind kind);

    /// Built on first use, a title that never reads back a depth/stencil
    /// surface never pays for compiling it.
    [[nodiscard]] MTL::ComputePipelineState* GetPackDepthStencilPipeline();

    Owned<MTL::ComputePipelineState> pack_depth_stencil_pipeline;

    std::unordered_map<u32, Owned<MTL::RenderPipelineState>> expand_pipelines;
    std::unordered_map<u64, Owned<MTL::RenderPipelineState>> convert_pipelines;
    std::unordered_map<u32, Owned<MTL::DepthStencilState>> convert_depth_states;
    std::unordered_map<u64, Owned<MTL::RenderPipelineState>> clear_pipelines;
    std::unordered_map<u32, Owned<MTL::DepthStencilState>> clear_depth_states;

    const Device& device;
    Scheduler& scheduler;

    Owned<MTL::Library> library;
    Owned<MTL::SamplerState> sampler_linear;
    Owned<MTL::SamplerState> sampler_nearest;

    std::unordered_map<u32, Owned<MTL::RenderPipelineState>> pipelines;
};

} // namespace Metal
