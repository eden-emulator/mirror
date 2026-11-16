// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <fstream>
#include <cstdlib>
#include <filesystem>
#include <atomic>
#include <bit>
#include <bitset>
#include <cstring>
#include <limits>
#include <set>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <boost/container/small_vector.hpp>

#include "common/alignment.h"
#include "common/div_ceil.h"
#include "common/fs/fs.h"
#include "common/logging.h"
#include "common/scope_exit.h"
#include "common/settings.h"
#include "video_core/control/channel_state.h"
#include "video_core/dirty_flags.h"
#include "video_core/engines/fermi_2d.h"
#include "video_core/engines/kepler_compute.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/host1x/host1x.h"
#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/runtime_info.h"
#include "video_core/memory_manager.h"
#include "video_core/metal_common/metal_device.h"
#include "video_core/surface.h"
#include "video_core/renderer_metal/mtl_rasterizer.h"
#include "video_core/renderer_metal/maxwell_to_mtl.h"
#include "video_core/renderer_metal/mtl_scheduler.h"
#include "video_core/textures/texture.h"

namespace Metal {
using Tegra::Engines::Maxwell3D;
using VideoCommon::CacheType;

namespace {
// Whether captured transform feedback is written back to guest RAM. Not dead
// code: Legends Z-A's character creator captures a mesh, fences and reads the
// vertices back on the CPU, so having this off is a known correctness
// compromise. Off anyway - the guest only sees the write if the drain waits for
// the capture first, and that wait costs 1758 flushes a second, GPU occupancy
// 88% -> 42%, Legends Z-A 21 -> 17 fps. Waiting on the capture's own tick
// instead of Finish()ing changes nothing, it is still in the unsubmitted chunk.
// Re-enable once captures can be published without a pipeline stall.
constexpr bool XFB_WRITEBACK_ENABLED = false;

/// Maxwell states the clear colour as four floats, but Metal reinterprets
/// MTLClearColor as integers for an integer attachment, so those get scaled.
[[nodiscard]] std::array<double, 4> ConvertClearColor(
    const Tegra::Engines::Maxwell3D::Regs& regs, Tegra::RenderTargetFormat rt_format) {
    using VideoCore::Surface::IsPixelFormatInteger;
    using VideoCore::Surface::IsPixelFormatSignedInteger;
    using VideoCore::Surface::PixelComponentSizeBitsInteger;
    using VideoCore::Surface::PixelFormatFromRenderTargetFormat;

    const auto format = PixelFormatFromRenderTargetFormat(rt_format);
    std::array<double, 4> out{};
    if (!IsPixelFormatInteger(format)) {
        for (size_t i = 0; i < 4; ++i) {
            out[i] = static_cast<double>(regs.clear_color[i]);
        }
        return out;
    }
    const size_t bits = PixelComponentSizeBitsInteger(format);
    if (IsPixelFormatSignedInteger(format)) {
        for (size_t i = 0; i < 4; ++i) {
            out[i] = static_cast<double>(
                static_cast<s32>(static_cast<f32>(static_cast<s64>(bits - 1) << 1) *
                                 (regs.clear_color[i] - 0.5f)));
        }
    } else {
        for (size_t i = 0; i < 4; ++i) {
            out[i] = static_cast<double>(static_cast<u32>(
                static_cast<f32>(static_cast<u64>(bits) << 1U) * regs.clear_color[i]));
        }
    }
    return out;
}

/// A *storage* texel buffer decodes through the format the shader declared, not
/// the view's - guests routinely declare an image as R32_UINT over bytes
/// written as something else. Sampled texel buffers keep the view's.
[[nodiscard]] VideoCore::Surface::PixelFormat TexelBufferFormat(
    VideoCore::Surface::PixelFormat view_format, bool is_image, Shader::ImageFormat declared) {
    if (!is_image) {
        return view_format;
    }
    using VideoCore::Surface::PixelFormat;
    switch (declared) {
    case Shader::ImageFormat::Typeless:
        return view_format;
    case Shader::ImageFormat::R8_UINT:
        return PixelFormat::R8_UINT;
    case Shader::ImageFormat::R8_SINT:
        return PixelFormat::R8_SINT;
    case Shader::ImageFormat::R16_UINT:
        return PixelFormat::R16_UINT;
    case Shader::ImageFormat::R16_SINT:
        return PixelFormat::R16_SINT;
    case Shader::ImageFormat::R32_UINT:
        return PixelFormat::R32_UINT;
    case Shader::ImageFormat::R32G32_UINT:
        return PixelFormat::R32G32_UINT;
    case Shader::ImageFormat::R32G32B32A32_UINT:
        return PixelFormat::R32G32B32A32_UINT;
    }
    return view_format;
}

/// The sampler variant that is legal against this texture. Metal defines
/// neither linear filtering of an integer texture nor a comparison sampler on a
/// colour one, and it does not reject them either.
[[nodiscard]] MTL::SamplerState* SamplerFor(const Sampler& sampler, const ImageView& view) {
    using VideoCore::Surface::IsPixelFormatInteger;
    const bool is_depth = VideoCore::Surface::GetFormatType(view.format) !=
                          VideoCore::Surface::SurfaceType::ColorTexture;
    if (sampler.HasLinearFiltering() && IsPixelFormatInteger(view.format)) {
        return sampler.HandleWithNearestFilter();
    }
    // Kept narrow. "Not a depth format" is too broad - a depth buffer sampled
    // through an R32_FLOAT view fails that test, and stripping there turns every
    // shadow lookup into a raw depth read.
    if (sampler.HasDepthComparison() && !is_depth && IsPixelFormatInteger(view.format)) {
        return sampler.HandleWithoutDepthComparison();
    }
    if (sampler.HasAddedAnisotropy() && !view.SupportsAnisotropy()) {
        return sampler.HandleWithDefaultAnisotropy();
    }
    return sampler.Handle();
}

constexpr size_t NUM_VIEWPORTS = Tegra::Engines::Maxwell3D::Regs::NumViewports;

/// Vertex buffer slot reserved for constant attributes. Metal has only 31
/// slots, so NumVertexArrays (32) cannot be a slot index, it trips an assertion
/// inside setBufferIndex.
constexpr u32 CONSTANT_ATTRIBUTE_BINDING =
    BindingMap::MAX_VERTEX_BUFFER_SLOT - BindingMap::VERTEX_BUFFER_BASE;

struct DrawParams {
    u32 base_instance{};
    u32 num_instances{};
    u32 base_vertex{};
    u32 num_vertices{};
    u32 first_index{};
    bool is_indexed{};
};

DrawParams MakeDrawParams(const Tegra::Engines::Maxwell3D::DrawManager::State& draw_state,
                          u32 num_instances, bool is_indexed) {
    DrawParams params{
        .base_instance = draw_state.base_instance,
        .num_instances = num_instances,
        .base_vertex = is_indexed ? draw_state.base_index : draw_state.vertex_buffer.first,
        .num_vertices = is_indexed ? draw_state.index_buffer.count : draw_state.vertex_buffer.count,
        .first_index = is_indexed ? draw_state.index_buffer.first : 0,
        .is_indexed = is_indexed,
    };

    // An expanded topology becomes indexed, and `first` is folded into the
    // generated indices, which is why the base vertex drops to zero.
    if (!is_indexed && NeedsIndexConversion(draw_state.topology)) {
        params.num_vertices = ConvertedIndexCount(draw_state.topology, params.num_vertices);
        params.base_vertex = 0;
        params.first_index = 0;
        params.is_indexed = true;
    }
    return params;
}

/// Kept out of the pipeline key: a depth function change is not a recompile.
DepthStencilState MakeDepthStencilState(const Tegra::Engines::Maxwell3D::Regs& regs,
                                        bool has_depth, bool has_stencil) {
    DepthStencilState state{};
    // Gated on the attachment existing, since Metal (unlike Vulkan) tests a
    // stencil-enabled state against undefined contents when there is no stencil
    // attachment and kills every fragment. And gated on the *test*, not the
    // write flag: Metal has no test-enable, so Always+write really writes and
    // every test-off/write-on fullscreen quad becomes a depth eraser.
    state.depth_write =
        has_depth && regs.depth_test_enable != 0 && regs.depth_write_enabled != 0;
    state.depth_compare = has_depth && regs.depth_test_enable != 0
                              ? ComparisonOp(regs.depth_test_func)
                              : MTL::CompareFunctionAlways;

    state.stencil_enable = has_stencil && regs.stencil_enable != 0;
    if (state.stencil_enable) {
        state.front.compare = ComparisonOp(regs.stencil_front_op.func);
        state.front.fail = StencilOp(regs.stencil_front_op.fail);
        state.front.depth_fail = StencilOp(regs.stencil_front_op.zfail);
        state.front.pass = StencilOp(regs.stencil_front_op.zpass);
        state.front.read_mask = regs.stencil_front_func_mask;
        state.front.write_mask = regs.stencil_front_mask;

        if (regs.stencil_two_side_enable != 0) {
            state.back.compare = ComparisonOp(regs.stencil_back_op.func);
            state.back.fail = StencilOp(regs.stencil_back_op.fail);
            state.back.depth_fail = StencilOp(regs.stencil_back_op.zfail);
            state.back.pass = StencilOp(regs.stencil_back_op.zpass);
            state.back.read_mask = regs.stencil_back_func_mask;
            state.back.write_mask = regs.stencil_back_mask;
        } else {
            state.back = state.front;
        }
    }
    return state;
}

MTL::PrimitiveTopologyClass TopologyClass(
    Tegra::Engines::Maxwell3D::Regs::PrimitiveTopology topology) {
    using Topology = Tegra::Engines::Maxwell3D::Regs::PrimitiveTopology;
    switch (topology) {
    case Topology::Points:
        return MTL::PrimitiveTopologyClassPoint;
    case Topology::Lines:
    case Topology::LineLoop:
    case Topology::LineStrip:
        return MTL::PrimitiveTopologyClassLine;
    default:
        return MTL::PrimitiveTopologyClassTriangle;
    }
}

} // Anonymous namespace

// ---------------------------------------------------------------------------
// AccelerateDMA
// ---------------------------------------------------------------------------

AccelerateDMA::AccelerateDMA(BufferCache& buffer_cache_, TextureCache& texture_cache_,
                             Scheduler& scheduler_)
    : buffer_cache{buffer_cache_}, texture_cache{texture_cache_}, scheduler{scheduler_} {}

bool AccelerateDMA::BufferCopy(GPUVAddr start_address, GPUVAddr end_address, u64 amount) {
    std::scoped_lock lock{buffer_cache.mutex};
    return buffer_cache.DMACopy(start_address, end_address, amount);
}

bool AccelerateDMA::BufferClear(GPUVAddr src_address, u64 amount, u32 value) {
    std::scoped_lock lock{buffer_cache.mutex};
    return buffer_cache.DMAClear(src_address, amount, value);
}

template <bool IS_IMAGE_UPLOAD>
bool AccelerateDMA::DmaBufferImageCopy(const Tegra::DMA::ImageCopy& copy_info,
                                       const Tegra::DMA::BufferOperand& buffer_operand,
                                       const Tegra::DMA::ImageOperand& image_operand) {
    std::scoped_lock lock{buffer_cache.mutex, texture_cache.mutex};
    const VideoCommon::ImageId image_id = texture_cache.DmaImageId(image_operand, IS_IMAGE_UPLOAD);
    if (image_id == VideoCommon::NULL_IMAGE_ID) {
        return false;
    }
    const u32 buffer_size = static_cast<u32>(buffer_operand.pitch * buffer_operand.height);
    constexpr auto sync_info = VideoCommon::ObtainBufferSynchronize::FullSynchronize;
    const auto post_op = IS_IMAGE_UPLOAD ? VideoCommon::ObtainBufferOperation::DoNothing
                                         : VideoCommon::ObtainBufferOperation::MarkAsWritten;
    const auto [buffer, offset] =
        buffer_cache.ObtainBuffer(buffer_operand.address, buffer_size, sync_info, post_op);
    if (buffer == nullptr) {
        return false;
    }

    const auto [image, copy] = texture_cache.DmaBufferImageCopy(
        copy_info, buffer_operand, image_operand, image_id, IS_IMAGE_UPLOAD);
    const std::span copy_span{&copy, 1};

    if constexpr (IS_IMAGE_UPLOAD) {
        texture_cache.PrepareImage(image_id, true, false);
        image->UploadMemory(buffer->Handle(), offset, copy_span);
    } else {
        if (offset % VideoCore::Surface::BytesPerBlock(image->info.format) != 0) {
            return false;
        }
        texture_cache.DownloadImageIntoBuffer(image, buffer->Handle(), offset, copy_span,
                                              buffer_operand.address, buffer_size);
    }
    return true;
}

bool AccelerateDMA::ImageToBuffer(const Tegra::DMA::ImageCopy& copy_info,
                                  const Tegra::DMA::ImageOperand& image_operand,
                                  const Tegra::DMA::BufferOperand& buffer_operand) {
    return DmaBufferImageCopy<false>(copy_info, buffer_operand, image_operand);
}

bool AccelerateDMA::BufferToImage(const Tegra::DMA::ImageCopy& copy_info,
                                  const Tegra::DMA::BufferOperand& buffer_operand,
                                  const Tegra::DMA::ImageOperand& image_operand) {
    return DmaBufferImageCopy<true>(copy_info, buffer_operand, image_operand);
}

// ---------------------------------------------------------------------------
// RasterizerMetal
// ---------------------------------------------------------------------------

RasterizerMetal::RasterizerMetal(Tegra::GPU& gpu, Tegra::MaxwellDeviceMemoryManager& device_memory,
                                 const Device& device, MemoryAllocator& memory_allocator,
                                 StagingBufferPool& staging_pool, Scheduler& scheduler)
    : m_gpu{gpu}, m_device_memory{device_memory}, m_device{device}, m_scheduler{scheduler},
      m_staging_pool{staging_pool}, m_blit_image{device, scheduler},
      m_texture_cache_runtime{device, scheduler, memory_allocator, staging_pool, m_blit_image},
      m_texture_cache{m_texture_cache_runtime, device_memory},
      m_buffer_cache_runtime{device, memory_allocator, scheduler, staging_pool},
      m_buffer_cache{device_memory, m_buffer_cache_runtime},
      m_pipeline_cache{device_memory, device, scheduler, gpu.ShaderNotify()},
      m_argument_buffer{device, scheduler, staging_pool},
      m_transform_feedback{device, scheduler, memory_allocator},
      m_fence_manager{*this,        gpu,           m_texture_cache,
                      m_buffer_cache, m_query_cache, scheduler},
      m_accelerate_dma{m_buffer_cache, m_texture_cache, scheduler} {}

RasterizerMetal::~RasterizerMetal() = default;

bool RasterizerMetal::UpdateRenderTargets(bool is_clear, const ClearAspects& aspects) {
    if (maxwell3d == nullptr) {
        return false;
    }

    std::scoped_lock lock{m_texture_cache.mutex};
    m_texture_cache.SynchronizeDescriptors(false);
    m_texture_cache.UpdateRenderTargets(is_clear);

    Framebuffer* const framebuffer = m_texture_cache.GetFramebuffer();
    if (framebuffer == nullptr) {
        return false;
    }

    RenderPassState pass = framebuffer->GetRenderPassState();

    if (is_clear) {
        // A clear is free on tile-based hardware when expressed as the pass's
        // load action, and expensive as a draw. Only the attachments the guest
        // named, though - clearing all of them wipes a target it meant to keep.
        const auto& regs = maxwell3d->regs;

        // A load action has no channel mask, no stencil write mask and no
        // rectangle. Decided BEFORE any, since the drawn path reuses `pass`.
        const bool partial_channels = aspects.color && aspects.color_mask != 0xfu;
        const bool partial_stencil = aspects.stencil && aspects.stencil_mask != 0xffu;

        const f32 clear_scale = m_texture_cache.IsRescaling()
                                    ? Settings::values.resolution_info.up_factor
                                    : 1.0f;
        const bool clear_lower_left =
            regs.window_origin.mode != Tegra::Engines::Maxwell3D::Regs::WindowOrigin::Mode::UpperLeft;
        const s64 area_w = static_cast<s64>(pass.render_area_width);
        const s64 area_h = static_cast<s64>(pass.render_area_height);
        s64 left = 0;
        s64 top = 0;
        s64 right = area_w;
        s64 bottom = area_h;
        if (regs.clear_control.use_scissor != 0) {
            const auto& sc = regs.scissor_test[0];
            s64 min_y = static_cast<s64>(sc.min_y);
            s64 max_y = static_cast<s64>(sc.max_y);
            if (clear_lower_left) {
                const s64 clip_h = static_cast<s64>(regs.surface_clip.height);
                const s64 flipped_min = clip_h - max_y;
                max_y = clip_h - min_y;
                min_y = flipped_min;
            }
            left = std::clamp<s64>(static_cast<s64>(sc.min_x * clear_scale), 0, area_w);
            top = std::clamp<s64>(static_cast<s64>(min_y * clear_scale), 0, area_h);
            right = std::clamp<s64>(static_cast<s64>(sc.max_x * clear_scale), left, area_w);
            bottom = std::clamp<s64>(static_cast<s64>(max_y * clear_scale), top, area_h);
        }
        const bool partial_area =
            left != 0 || top != 0 || right != area_w || bottom != area_h;
        const bool needs_draw = partial_area || partial_channels || partial_stencil;

        if (!needs_draw) {
            for (u32 index = 0; index < pass.num_color; ++index) {
                if (pass.color[index].texture == nullptr) {
                    continue;
                }
                if (!aspects.color || index != aspects.color_target) {
                    continue;
                }
                pass.color[index].load_action = LoadAction::Clear;
                pass.color[index].clear_color =
                    ConvertClearColor(regs, regs.rt[index].format);
            }
            if (pass.depth_stencil.texture != nullptr) {
                if (aspects.depth) {
                    pass.depth_stencil.depth_load = LoadAction::Clear;
                    pass.depth_stencil.clear_depth = regs.clear_depth;
                }
                if (aspects.stencil && pass.depth_stencil.has_stencil) {
                    pass.depth_stencil.stencil_load = LoadAction::Clear;
                    pass.depth_stencil.clear_stencil = regs.clear_stencil;
                }
            }
        }

        // A load action otherwise covers every slice of the pass, and Metal
        // will not trim an over-long range.
        const u32 attachment_layers = (std::max)(1u, pass.num_layers);
        const u32 first_layer = (std::min)(aspects.layer, attachment_layers - 1);
        const u32 layer_count =
            (std::min)((std::max)(1u, aspects.layer_count), attachment_layers - first_layer);
        if (aspects.color && aspects.color_target < pass.color.size()) {
            pass.color[aspects.color_target].slice = first_layer;
        }
        if (aspects.depth || aspects.stencil) {
            pass.depth_stencil.slice = first_layer;
        }
        pass.num_layers = layer_count;

        if (needs_draw) {
            // Metal has no clear-rect, so a partial clear is a scissored draw.
            m_render_pass = pass;
            m_has_render_pass = true;
            m_scheduler.RequestRenderPass(pass);
            m_blit_image.ClearAttachments(
                pass, aspects.color_target, aspects.color,
                aspects.depth && pass.depth_stencil.texture != nullptr,
                aspects.stencil && pass.depth_stencil.has_stencil,
                {regs.clear_color[0], regs.clear_color[1], regs.clear_color[2],
                 regs.clear_color[3]},
                regs.clear_depth, regs.clear_stencil, static_cast<s32>(left),
                static_cast<s32>(top), static_cast<s32>(right), static_cast<s32>(bottom),
                aspects.color_mask, aspects.stencil_mask);
            return true;
        }
    }

    // Not opened here on purpose. One encoder is open per command buffer, so an
    // upload recorded after this closes the render encoder and the draw does
    // nothing. The pass is remembered and reopened right before the draw.
    m_render_pass = pass;
    m_has_render_pass = true;

    if (is_clear) {
        m_scheduler.RequestRenderPass(pass);
    }
    return true;
}

void RasterizerMetal::Draw(bool is_indexed, u32 instance_count) {
    DrawImpl(is_indexed, instance_count, nullptr);
}

/// Metal has no multi-draw, so the count becomes a loop over the stride.
void RasterizerMetal::DrawIndirect() {
    if (!maxwell3d) {
        return;
    }
    const auto& params = maxwell3d->draw_manager.indirect_state;
    {
        std::scoped_lock lock{m_buffer_cache.mutex};
        m_buffer_cache.SetDrawIndirect(&params);
    }

    // buffer/offset stay null here: GetDrawIndirectBuffer reads channel state
    // only the buffer cache update inside DrawImpl fills in.
    IndirectDraw indirect{
        .buffer = nullptr,
        .offset = 0,
        .stride = static_cast<u32>(params.stride),
        .count = static_cast<u32>(params.max_draw_counts),
    };
    // The real count lives in a second buffer the GPU writes this frame, so it
    // must NOT be read on the CPU (that gives last frame's value). DrawImpl
    // resolves it on the GPU via IndexPass::PatchIndirectCount.
    indirect.include_count = params.include_count;
    indirect.is_byte_count = params.is_byte_count;
    if (indirect.count == 0) {
        return;
    }

    DrawImpl(params.is_indexed, 1, &indirect);
    {
        std::scoped_lock lock{m_buffer_cache.mutex};
        m_buffer_cache.SetDrawIndirect(nullptr);
    }
}

#define DROP_DRAW(why)                                                                     \
    do {                                                                                       \
        static std::atomic<u64> drop_count{0};                                                 \
        const u64 drawdiag_c = drop_count.fetch_add(1, std::memory_order_relaxed) + 1;         \
        if (std::has_single_bit(drawdiag_c)) {                                                 \
            LOG_WARNING(Render_Metal, "Dropped {} draw(s): {}", drawdiag_c, why);     \
        }                                                                                      \
    } while (0)

void RasterizerMetal::DrawImpl(bool is_indexed, u32 instance_count,
                               const IndirectDraw* indirect) {
    if (!maxwell3d) {
        return;
    }
    // Read per draw: it toggles around the objects a title tests for visibility.
    m_zpass_enabled = maxwell3d->regs.zpass_pixel_count_enable != 0;

    FlushWork();
    m_gpu.TickWork();
    // Guest writes are coalesced in the invalidate accumulator; undrained, a
    // draw can be served from a buffer whose guest memory was overwritten.
    if (gpu_memory) {
        gpu_memory->FlushCaching();
    }

    // Attachment formats are part of the pipeline key.
    if (!UpdateRenderTargets(false, ClearAspects{})) {
        DROP_DRAW("render targets could not be resolved");
        return;
    }
    UpdatePipelineState();

    if (m_has_render_pass && m_render_pass.incomplete) {
        // The pass lost a declared attachment (see RenderPassState::incomplete)
        // so the pipeline's attachment formats no longer match it. The mismatch
        // was observed writing into the previously open pass's tile memory.
        static std::atomic<u64> skipped{0};
        const u64 count = skipped.fetch_add(1, std::memory_order_relaxed) + 1;
        if (count == 1 || count % 10000 == 0) {
            LOG_WARNING(Render_Metal, "Skipped {} draw(s): incomplete render pass", count);
        }
        return;
    }

    GraphicsPipeline* const pipeline = m_pipeline_cache.CurrentGraphicsPipeline();
    if (pipeline == nullptr) {
        static std::atomic<u64> skipped{0};
        const u64 count = skipped.fetch_add(1, std::memory_order_relaxed) + 1;
        if (count == 1 || count % 10000 == 0) {
            LOG_WARNING(Render_Metal, "Skipped {} draw(s): no usable pipeline", count);
        }
        return;
    }
    // Non-null when the guest's geometry stage is emulated: the vertex program
    // runs as a compute kernel, a second expands its output into triangles, and
    // the render pipeline's vertex function is a generated passthrough.
    const GeometryEmulation* geometry = pipeline->GeometryParams();

    const GeometryEmulation* xfb_capture = nullptr;
    if (geometry != nullptr && geometry->xfb_only) {
        xfb_capture = geometry;
        geometry = nullptr;
    }

    if (pipeline->IsRasterizerDiscard() && xfb_capture == nullptr) {
        // Culling both faces produces no fragments and Metal has no cull mode
        // for it. NOT dropped for a capture draw - discard is the point there.
        DROP_DRAW("rasterizer discard (cull both faces), no xfb capture");
        return;
    }

    if (geometry != nullptr && indirect != nullptr) {
        LOG_WARNING(Render_Metal, "Indirect draw with an emulated geometry stage is unsupported");
        return;
    }

    m_buffer_cache_runtime.ResetDrawBindings();

    std::array<StageResources, NUM_PIPELINE_STAGES> stage_resources{};
    ConfigureGraphicsResources(*pipeline, is_indexed, stage_resources);

    const auto& regs = maxwell3d->regs;
    const auto& draw_state = maxwell3d->draw_manager.draw_state;
    DrawParams draw = MakeDrawParams(draw_state, instance_count, is_indexed);
    // What gl_BaseVertex and gl_BaseInstance read in capture and geometry kernels.
    m_capture_draw_base = {draw.base_vertex, draw.base_instance};

    // A submission with four render encoders on a 1024x1024 depth target ran 45
    // SECONDS at 100% GPU, which is not slow, that is a draw asking for work
    // that cannot finish. The counts come straight from guest registers and
    // that same session showed index data landing far outside anything the
    // bound streams hold.
    if (draw.num_vertices == 0 && indirect == nullptr) {
        if (geometry != nullptr || xfb_capture != nullptr) {
            static std::atomic<u32> logged{0};
            if (logged.fetch_add(1, std::memory_order_relaxed) < 16) {
                LOG_WARNING(Render_Metal,
                            "geometry/xfb draw dropped: zero CPU vertex count (indexed={} "
                            "topology={} index_count={})",
                            is_indexed, static_cast<u32>(draw_state.topology),
                            draw_state.index_buffer.count);
            }
        }
        DROP_DRAW("zero CPU vertex count");
        return;
    }

    // Non-indexed fans, polygons and line loops. The shared buffer cache only
    // generates indices for Quads and QuadStrip, so the rest get expanded here.
    if (!is_indexed && NeedsIndexConversion(draw_state.topology) &&
        !m_buffer_cache_runtime.GetIndexBinding().converted) {
        m_buffer_cache_runtime.BindGeneratedIndexBuffer(draw_state.topology,
                                                        draw_state.vertex_buffer.first,
                                                        draw_state.vertex_buffer.count);
    }

    {
        const IndexBufferBinding& converted = m_buffer_cache_runtime.GetIndexBinding();
        if (converted.converted) {
            draw.is_indexed = true;
            draw.num_vertices = converted.num_indices;
            draw.first_index = 0;
            draw.base_vertex = 0;
        }
        if (draw.is_indexed && !converted.valid) {
            if (geometry != nullptr || xfb_capture != nullptr) {
                static std::atomic<u32> logged{0};
                if (logged.fetch_add(1, std::memory_order_relaxed) < 16) {
                    LOG_WARNING(Render_Metal,
                                "geometry/xfb draw dropped: invalid index binding (converted={} "
                                "topology={} count={} format={})",
                                converted.converted, static_cast<u32>(draw_state.topology),
                                draw.num_vertices,
                                static_cast<u32>(draw_state.index_buffer.format));
                }
            }
            return;
        }
    }

    const f32 scale = m_texture_cache.IsRescaling()
                          ? Settings::values.resolution_info.up_factor
                          : 1.0f;
    const bool scale_offset_enabled = regs.viewport_scale_offset_enabled != 0;
    const bool lower_left =
        regs.window_origin.mode != Tegra::Engines::Maxwell3D::Regs::WindowOrigin::Mode::UpperLeft;
    {
        static std::atomic<bool> logged{false};
        if (!logged.exchange(true, std::memory_order_relaxed)) {
            LOG_INFO(Render_Metal,
                     "orientation: scale_offset={} lower_left={} flip_y={} scale_y={} swizzle_y={}",
                     scale_offset_enabled, lower_left, regs.window_origin.flip_y.Value(),
                     regs.viewport_transform[0].scale_y,
                     static_cast<u32>(regs.viewport_transform[0].swizzle.y.Value()));
        }
    }

    std::array<MTL::Viewport, NUM_VIEWPORTS> mtl_viewports{};
    std::array<MTL::ScissorRect, NUM_VIEWPORTS> mtl_scissors{};
    bool any_scissor = false;
    // Binding all sixteen makes the driver validate sixteen rectangles per
    // draw, and titles almost always leave them identical.
    size_t viewport_count = 1;

    const s64 area_width = static_cast<s64>(m_render_pass.render_area_width);
    const s64 area_height = static_cast<s64>(m_render_pass.render_area_height);

    for (size_t index = 0; index < NUM_VIEWPORTS; ++index) {
        const auto& src_viewport = regs.viewport_transform[index];
        const auto& src_scissor = regs.scissor_test[index];

        if (index > 0 &&
            std::memcmp(&src_viewport, &regs.viewport_transform[0], sizeof(src_viewport)) == 0 &&
            std::memcmp(&src_scissor, &regs.scissor_test[0], sizeof(src_scissor)) == 0) {
            mtl_viewports[index] = mtl_viewports[0];
            mtl_scissors[index] = mtl_scissors[0];
            continue;
        }
        viewport_count = NUM_VIEWPORTS;

        MTL::Viewport& mtl_viewport = mtl_viewports[index];
        // THE ORIENTATION CONTRACT. Build the Y rect exactly as Vulkan does,
        // lower-left window-origin flip and NegativeY swizzle included, then
        // hand it to Metal mirrored:
        //
        //   originY' = y + height;  height' = -height
        //
        // Metal's NDC->window map is Y-inverted against Vulkan's, so we owe this
        // mirror on EVERY draw, not just when scale_y is negative. A negative
        // height is legal and Metal does the flip.
        double vk_y;
        double vk_height;
        if (!scale_offset_enabled) {
            mtl_viewport.originX = static_cast<double>(regs.surface_clip.x) * scale;
            vk_y = static_cast<double>(regs.surface_clip.y) * scale;
            mtl_viewport.width =
                static_cast<double>((std::max)(1, static_cast<s32>(regs.surface_clip.width))) *
                scale;
            vk_height =
                static_cast<double>((std::max)(1, static_cast<s32>(regs.surface_clip.height))) *
                scale;
            if (lower_left) {
                vk_y += vk_height;
                vk_height = -vk_height;
            }
            mtl_viewport.znear = 0.0;
            mtl_viewport.zfar = 1.0;
        } else {
            mtl_viewport.originX = (src_viewport.translate_x - src_viewport.scale_x) * scale;
            vk_y = (src_viewport.translate_y - src_viewport.scale_y) * scale;
            mtl_viewport.width = src_viewport.scale_x * 2.0f * scale;
            vk_height = src_viewport.scale_y * 2.0f * scale;
            if (lower_left) {
                vk_y += static_cast<double>(regs.surface_clip.height) * scale;
                vk_height = -vk_height;
            }
            if (src_viewport.swizzle.y.Value() ==
                Tegra::Engines::Maxwell3D::Regs::ViewportSwizzle::NegativeY) {
                // Vulkan only honours this when the NV viewport-swizzle
                // extension is absent. Metal never has it, so always.
                vk_y += vk_height;
                vk_height = -vk_height;
            }
            // The near plane is translate_z - scale_z only for -1..1 depth.
            // With 0..1 translate_z already IS the near plane, and subtracting
            // scale_z pushes it negative, which Metal rejects.
            const f32 reduce_z =
                regs.depth_mode == Tegra::Engines::Maxwell3D::Regs::DepthMode::MinusOneToOne
                    ? 1.0f
                    : 0.0f;
            mtl_viewport.znear =
                std::clamp(src_viewport.translate_z - src_viewport.scale_z * reduce_z, 0.0f, 1.0f);
            mtl_viewport.zfar =
                std::clamp(src_viewport.translate_z + src_viewport.scale_z, 0.0f, 1.0f);
        }
        mtl_viewport.originY = vk_y + vk_height;
        mtl_viewport.height = -vk_height;
        if (mtl_viewport.width < 0.0) {
            // Metal's X map matches Vulkan's, so there is no mirror to fold a
            // negative width into and the guest's X mirror is dropped.
            mtl_viewport.originX += mtl_viewport.width;
            mtl_viewport.width = -mtl_viewport.width;
        }

        // Metal treats a scissor leaving the render target as a hard encoder
        // error instead of clamping it, so the arithmetic below is signed and
        // clamped to the attachment before it narrows to NS::UInteger.
        s64 left = 0;
        s64 top = 0;
        s64 right = area_width;
        s64 bottom = area_height;
        if (src_scissor.enable != 0) {
            s64 min_y = static_cast<s64>(src_scissor.min_y);
            s64 max_y = static_cast<s64>(src_scissor.max_y);
            if (lower_left) {
                const s64 clip_h = static_cast<s64>(regs.surface_clip.height);
                const s64 flipped_min = clip_h - max_y;
                max_y = clip_h - min_y;
                min_y = flipped_min;
            }
            left = static_cast<s64>(src_scissor.min_x * scale);
            top = static_cast<s64>(min_y * scale);
            right = static_cast<s64>(src_scissor.max_x * scale);
            bottom = static_cast<s64>(max_y * scale);
        } else if (!scale_offset_enabled) {
            left = static_cast<s64>(regs.surface_clip.x * scale);
            top = static_cast<s64>(regs.surface_clip.y * scale);
            right = left + static_cast<s64>(
                               (std::max)(1, static_cast<s32>(regs.surface_clip.width)) * scale);
            bottom = top + static_cast<s64>(
                               (std::max)(1, static_cast<s32>(regs.surface_clip.height)) * scale);
            if (lower_left) {
                const s64 clip_h = static_cast<s64>(regs.surface_clip.height * scale);
                const s64 flipped_top = clip_h - bottom;
                bottom = clip_h - top;
                top = flipped_top;
            }
        }
        left = std::clamp<s64>(left, 0, area_width);
        top = std::clamp<s64>(top, 0, area_height);
        right = std::clamp<s64>(right, left, area_width);
        bottom = std::clamp<s64>(bottom, top, area_height);

        MTL::ScissorRect& mtl_scissor = mtl_scissors[index];
        mtl_scissor.x = static_cast<NS::UInteger>(left);
        mtl_scissor.y = static_cast<NS::UInteger>(top);
        mtl_scissor.width = static_cast<NS::UInteger>(right - left);
        mtl_scissor.height = static_cast<NS::UInteger>(bottom - top);
        if (mtl_scissor.width != 0 && mtl_scissor.height != 0) {
            any_scissor = true;
        }
    }

    if (!any_scissor) {
        // A degenerate rect is an encoder error, not an empty draw.
        static std::atomic<u32> skipped{0};
        const u32 count = ++skipped;
        if (std::has_single_bit(count)) {
            LOG_WARNING(Render_Metal, "Skipped {} draw(s): every scissor is empty", count);
        }
        return;
    }

    const MTL::CullMode cull_mode =
        regs.gl_cull_test_enabled != 0 ? CullFace(regs.gl_cull_face) : MTL::CullModeNone;
    MTL::Winding winding = FrontFace(regs.gl_front_face);
    if (regs.window_origin.flip_y != 0) {
        // A flip_y guest hands geometry wound for a bottom-up raster.
        winding = winding == MTL::WindingClockwise ? MTL::WindingCounterClockwise
                                                   : MTL::WindingClockwise;
    }
    // An expanded topology is drawn as whatever its generated indices describe,
    // not as whatever PrimitiveTopology() would fall back to.
    const MTL::PrimitiveType primitive =
        geometry != nullptr             ? MTL::PrimitiveTypeTriangle
        : NeedsIndexConversion(draw_state.topology)
            ? ConvertedPrimitive(draw_state.topology)
            : PrimitiveTopology(m_device, draw_state.topology);

    MTL::DepthStencilState* depth_stencil =
        m_pipeline_cache.GetDepthStencilCache().Get(MakeDepthStencilState(
            regs, m_render_pass.depth_stencil.texture != nullptr,
            m_render_pass.depth_stencil.has_stencil));
    const u32 stencil_ref_front = regs.stencil_front_ref;
    const u32 stencil_ref_back =
        regs.stencil_two_side_enable != 0 ? regs.stencil_back_ref : regs.stencil_front_ref;

    const auto geometry_clip = regs.viewport_clip_control.geometry_clip;
    using GeometryClip = Tegra::Engines::Maxwell3D::Regs::ViewportClipControl::GeometryClip;
    const bool depth_clamp_disabled = geometry_clip == GeometryClip::Passthrough ||
                                      geometry_clip == GeometryClip::FrustumXYZ ||
                                      geometry_clip == GeometryClip::FrustumZ;
    const MTL::DepthClipMode depth_clip_mode =
        depth_clamp_disabled ? MTL::DepthClipModeClip : MTL::DepthClipModeClamp;

    const bool offset_enabled = [&] {
        switch (TopologyClass(draw_state.topology)) {
        case MTL::PrimitiveTopologyClassPoint:
            return regs.polygon_offset_point_enable != 0;
        case MTL::PrimitiveTopologyClassLine:
            return regs.polygon_offset_line_enable != 0;
        default:
            return regs.polygon_offset_fill_enable != 0;
        }
    }();
    // Halved because Maxwell's depth-bias units are twice the host's.
    const f32 depth_bias_units = offset_enabled ? regs.depth_bias / 2.0f : 0.0f;
    const f32 depth_bias_slope = offset_enabled ? regs.slope_scale_depth_bias : 0.0f;
    const f32 depth_bias_clamp = offset_enabled ? regs.depth_bias_clamp : 0.0f;

    // Kept out of the pipeline key, Metal takes the blend constant per encoder.
    // It still has to be set, or Constant* blend factors multiply by Metal's
    // default of zero.
    const auto& blend_color = regs.blend_color;

    // Metal has no point-fill mode, so points fall back to lines.
    const MTL::TriangleFillMode fill_mode =
        regs.polygon_mode_front == Tegra::Engines::Maxwell3D::Regs::PolygonMode::Fill
            ? MTL::TriangleFillModeFill
            : MTL::TriangleFillModeLines;

    // Copied out because the command runs on the worker thread, after the cache
    // has moved on. Inline capacity stays at 8: sizing it to the full slot
    // count puts ~800 more bytes into every recorded draw lambda.
    boost::container::small_vector<BufferBinding, 8> vertex_bindings;
    for (const BufferBinding& binding : m_buffer_cache_runtime.GetVertexBindings()) {
        vertex_bindings.push_back(binding);
    }
    const IndexBufferBinding index_binding = m_buffer_cache_runtime.GetIndexBinding();

    // Only the slots this pipeline's descriptor reads may be bound: the binding
    // array persists between draws, so an untouched slot can still hold a
    // buffer from an unrelated draw that has since been destroyed.
    u32 enabled_vertex_slots = 0;
    for (size_t index = 0; index < pipeline->State().bindings.size(); ++index) {
        if (pipeline->State().bindings[index].enabled) {
            enabled_vertex_slots |= 1u << index;
        }
    }

    MTL::Buffer* const null_buffer = m_buffer_cache_runtime.GetNullBuffer();

    if (regs.transform_feedback_enabled != 0) {
        // With the capture off the guest's feedback buffers keep what they had.
        const bool xfb_disabled = !Settings::values.metal_transform_feedback.GetValue();
        if (!xfb_disabled && xfb_capture != nullptr && indirect == nullptr) {
            RecordXfbCapture(*pipeline, *xfb_capture, draw_state.topology, draw.is_indexed,
                             draw.num_vertices, draw.first_index, draw.base_vertex, instance_count,
                             stage_resources,
                             std::span<const BufferBinding>{vertex_bindings.data(),
                                                            vertex_bindings.size()},
                             enabled_vertex_slots);
        } else if (!xfb_disabled && xfb_capture != nullptr) {
            // Never silent: a guest captures once per mesh change and consumes
            // for hundreds of frames, so one skipped batch starves all of them.
            LOG_WARNING(Render_Metal, "XFB capture SKIPPED: indirect draw");
        }
    }

    if (pipeline->IsRasterizerDiscard()) {
        DROP_DRAW("rasterizer discard after xfb capture (capture-only draw)");
        return;
    }

    // Recorded here for the same reason the index expansion is: both need a
    // compute encoder, and opening one closes the render encoder.
    GeometryDispatch geometry_dispatch{};
    if (geometry != nullptr) {
        if (!RecordGeometryDispatch(*pipeline, *geometry, draw_state.topology, draw.is_indexed,
                                    draw.num_vertices, draw.first_index, draw.base_vertex,
                                    draw.num_instances, stage_resources,
                                    std::span<const BufferBinding>{vertex_bindings.data(),
                                                                   vertex_bindings.size()},
                                    enabled_vertex_slots, geometry_dispatch)) {
            return;
        }
        draw.is_indexed = false;
        draw.num_vertices = geometry_dispatch.emitted_vertices;
        draw.num_instances = 1;
        draw.base_vertex = 0;
        draw.base_instance = 0;
        enabled_vertex_slots = 0;
    }

    const bool geometry_emulated = geometry != nullptr;
    MTL::Buffer* const geometry_emitted = geometry_dispatch.emitted;
    const u64 geometry_emitted_offset = geometry_dispatch.emitted_offset;

    IndirectDraw indirect_draw = indirect != nullptr ? *indirect : IndirectDraw{};
    if (indirect != nullptr) {
        // Resolved only now: ConfigureGraphicsResources above ran the buffer
        // cache update, which gives the indirect binding its buffer id.
        std::scoped_lock lock{m_buffer_cache.mutex};
        const auto [indirect_buffer, indirect_offset] = m_buffer_cache.GetDrawIndirectBuffer();
        if (indirect_buffer == nullptr || indirect_buffer->Handle() == nullptr) {
            DROP_DRAW("indirect buffer could not be resolved");
            return;
        }
        indirect_draw.buffer = indirect_buffer->Handle();
        indirect_draw.offset = indirect_offset;

        if (indirect_draw.is_byte_count) {
            // The "indirect buffer" IS the counter, not draw arguments.
            const PatchedIndirect args =
                m_buffer_cache_runtime.GetIndexPass().MakeByteCountDrawArgs(
                    indirect_draw.buffer, indirect_draw.offset, indirect_draw.stride);
            if (!args.valid) {
                return;
            }
            indirect_draw.buffer = args.buffer;
            indirect_draw.offset = args.offset;
            indirect_draw.stride = args.stride;
            indirect_draw.count = 1;
        } else if (indirect_draw.include_count) {
            // Metal encodes one command per entry and cannot take a count
            // buffer, so every allowed entry is encoded and the ones past the
            // real count are rewritten to draw nothing.
            const auto [count_buffer, count_offset] = m_buffer_cache.GetDrawIndirectCount();
            if (count_buffer != nullptr && count_buffer->Handle() != nullptr) {
                const PatchedIndirect patched = m_buffer_cache_runtime.GetIndexPass().PatchIndirectCount(
                    indirect_draw.buffer, indirect_draw.offset, indirect_draw.stride,
                    indirect_draw.count, count_buffer->Handle(), count_offset, is_indexed);
                if (patched.valid) {
                    indirect_draw.buffer = patched.buffer;
                    indirect_draw.offset = patched.offset;
                    indirect_draw.stride = patched.stride;
                } else if (gpu_memory != nullptr) {
                    const auto& params = maxwell3d->draw_manager.indirect_state;
                    const u32 count = gpu_memory->Read<u32>(params.count_start_address);
                    indirect_draw.count = (std::min)(count, indirect_draw.count);
                }
            }
        }
        if (indirect_draw.count == 0) {
            DROP_DRAW("indirect draw count is zero");
            return;
        }
    }
    // A stage that wanted an argument buffer and did not get one cannot be
    // drawn: the encoder still holds the previous draw's buffer, so the draw
    // would sample another object's resources.
    if (std::ranges::any_of(stage_resources,
                            [](const StageResources& stage) { return stage.resources_failed; })) {
        static std::atomic<u32> skipped{0};
        const u32 count = ++skipped;
        if (std::has_single_bit(count)) {
            LOG_WARNING(Render_Metal, "Skipped {} draw(s): argument buffer allocation failed",
                        count);
        }
        return;
    }

    // Open the pass now that every upload is recorded, so the encoder is still
    // current when this draw runs. Must stay BELOW the indirect-argument
    // resolution above: MakeByteCountDrawArgs and PatchIndirectCount both need
    // a compute encoder, which closes the render one, so opening the pass first
    // left every DrawIndirectCount and DrawIndirectByteCount draw with a null
    // CurrentRender() and dropped it on the worker.
    if (m_has_render_pass) {
        // Before the descriptor is built: the visibility buffer is named by it,
        // and an encoder opened without one cannot start counting later.
        BeginPassVisibility(m_render_pass);
        m_scheduler.RequestRenderPass(m_render_pass);
    }

    // What this draw can modify, for the feedback-loop test.
    // Mirrors the pipeline's write masks: an output the shader never stores is masked off.
    u32 written_attachments = 0;
    {
        const FixedPipelineState& key = pipeline->State();
        const Shader::Info* const fragment_info = pipeline->StageInfo(PipelineStage::Fragment);
        for (u32 index = 0; index < key.num_color_attachments; ++index) {
            if (key.color_attachments[index].write_mask == MTL::ColorWriteMaskNone ||
                !GraphicsPipeline::StoresColor(key, fragment_info, index)) {
                continue;
            }
            written_attachments |= 1u << index;
        }
        const bool writes_depth = m_render_pass.depth_stencil.texture != nullptr &&
                                  regs.depth_test_enable != 0 && regs.depth_write_enabled != 0;
        const bool writes_stencil =
            m_render_pass.depth_stencil.has_stencil && regs.stencil_enable != 0;
        if (writes_depth || writes_stencil) {
            written_attachments |= Scheduler::DEPTH_ATTACHMENT_BIT;
        }
    }

    const u32 visibility_slot = m_visibility_pass_slot;
    m_scheduler.Record([=, viewports = mtl_viewports, scissors = mtl_scissors,
                        bindings = std::move(vertex_bindings),
                        stages = std::move(stage_resources)](CommandContext& context) {
        RenderEncoder encoder = context.CurrentRender();
        if (encoder == nullptr) {
            // Anything needing a blit or compute encoder between the pass
            // request and this draw closes it, and the draw vanishes silently.
            static std::atomic<u32> skipped{0};
            const u32 count = ++skipped;
            if (std::has_single_bit(count)) {
                LOG_WARNING(Render_Metal, "Skipped {} draw(s): render encoder closed underneath",
                            count);
            }
            return;
        }

        pipeline->WaitBuilt();
        MTL::RenderPipelineState* const pipeline_state = pipeline->Handle();
        if (pipeline_state == nullptr) {
            static std::atomic<u32> skipped{0};
            const u32 count = ++skipped;
            if (std::has_single_bit(count)) {
                LOG_WARNING(Render_Metal, "Skipped {} draw(s): pipeline failed to build", count);
            }
            return;
        }
        // Only what changed since the last draw on this encoder. Re-setting a
        // pipeline or depth-stencil object is real driver work.
        auto& cache = context.RenderCache();
        const auto same = [](const auto& a, const auto& b) {
            return std::memcmp(&a, &b, sizeof(a)) == 0;
        };
        if (cache.visibility_slot != visibility_slot) {
            // Counting is only legal when the pass this encoder was opened with
            // named a visibility buffer. Asking anyway does not fail cleanly -
            // the process dies inside setVisibilityResultMode.
            const bool can_count = visibility_slot != NO_VISIBILITY_SLOT &&
                                   context.CurrentPassVisibilityBuffer() != nullptr;
            if (visibility_slot != NO_VISIBILITY_SLOT && !can_count) {
                static std::atomic<u32> mismatched{0};
                const u32 count = mismatched.fetch_add(1, std::memory_order_relaxed) + 1;
                if (std::has_single_bit(count)) {
                    LOG_WARNING(Render_Metal,
                                "Dropped {} occlusion quer(y/ies): the open pass has no "
                                "visibility buffer",
                                count);
                }
            }
            cache.visibility_slot = can_count ? visibility_slot : NO_VISIBILITY_SLOT;
            if (!can_count) {
                encoder->setVisibilityResultMode(MTL::VisibilityResultModeDisabled, 0);
            } else {
                encoder->setVisibilityResultMode(MTL::VisibilityResultModeCounting,
                                                 visibility_slot * sizeof(u64));
            }
        }
        if (cache.pipeline != pipeline_state) {
            cache.pipeline = pipeline_state;
            encoder->setRenderPipelineState(pipeline_state);
        }
        if (depth_stencil != nullptr && cache.depth_stencil != depth_stencil) {
            cache.depth_stencil = depth_stencil;
            encoder->setDepthStencilState(depth_stencil);
        }
        const bool count_changed = cache.viewport_count != viewport_count;
        if (count_changed || !std::equal(viewports.begin(), viewports.begin() + viewport_count,
                                         cache.viewports.begin(), same)) {
            std::copy_n(viewports.begin(), viewport_count, cache.viewports.begin());
            encoder->setViewports(viewports.data(), viewport_count);
        }
        if (count_changed || !std::equal(scissors.begin(), scissors.begin() + viewport_count,
                                         cache.scissors.begin(), same)) {
            std::copy_n(scissors.begin(), viewport_count, cache.scissors.begin());
            encoder->setScissorRects(scissors.data(), viewport_count);
        }
        cache.viewport_count = static_cast<u32>(viewport_count);
        if (cache.cull_mode != static_cast<u32>(cull_mode)) {
            cache.cull_mode = static_cast<u32>(cull_mode);
            encoder->setCullMode(cull_mode);
        }
        if (cache.winding != static_cast<u32>(winding)) {
            cache.winding = static_cast<u32>(winding);
            encoder->setFrontFacingWinding(winding);
        }
        if (cache.depth_clip != static_cast<u32>(depth_clip_mode)) {
            cache.depth_clip = static_cast<u32>(depth_clip_mode);
            encoder->setDepthClipMode(depth_clip_mode);
        }
        if (cache.fill_mode != static_cast<u32>(fill_mode)) {
            cache.fill_mode = static_cast<u32>(fill_mode);
            encoder->setTriangleFillMode(fill_mode);
        }
        if (cache.stencil_front != stencil_ref_front || cache.stencil_back != stencil_ref_back) {
            cache.stencil_front = stencil_ref_front;
            cache.stencil_back = stencil_ref_back;
            encoder->setStencilReferenceValues(stencil_ref_front, stencil_ref_back);
        }
        const std::array<f32, 3> bias{depth_bias_units, depth_bias_slope, depth_bias_clamp};
        if (!cache.depth_bias_valid || cache.depth_bias != bias) {
            cache.depth_bias_valid = true;
            cache.depth_bias = bias;
            encoder->setDepthBias(depth_bias_units, depth_bias_slope, depth_bias_clamp);
        }
        const std::array<f32, 4> blend{blend_color.r, blend_color.g, blend_color.b,
                                       blend_color.a};
        if (!cache.blend_color_valid || cache.blend_color != blend) {
            cache.blend_color_valid = true;
            cache.blend_color = blend;
            encoder->setBlendColor(blend[0], blend[1], blend[2], blend[3]);
        }

        const StageResources& vertex_stage = stages[static_cast<size_t>(PipelineStage::Vertex)];
        const StageResources& fragment_stage =
            stages[static_cast<size_t>(PipelineStage::Fragment)];
        if (geometry_emulated) {
            // The vertex stage is a generated passthrough with no argument
            // buffer, the guest's resources went to the capture kernel.
            constexpr u32 slot = GEOMETRY_PASSTHROUGH_BUFFER_INDEX;
            if (cache.vertex_buffer[slot] != geometry_emitted ||
                cache.vertex_offset[slot] != geometry_emitted_offset) {
                cache.vertex_buffer[slot] = geometry_emitted;
                cache.vertex_offset[slot] = geometry_emitted_offset;
                encoder->setVertexBuffer(geometry_emitted, geometry_emitted_offset, slot);
            }
        } else if (vertex_stage.argument_buffer.buffer != nullptr) {
            if (cache.vertex_buffer[BindingMap::ARGUMENT_BUFFER_INDEX] !=
                    vertex_stage.argument_buffer.buffer ||
                cache.vertex_offset[BindingMap::ARGUMENT_BUFFER_INDEX] !=
                    vertex_stage.argument_buffer.offset) {
                cache.vertex_buffer[BindingMap::ARGUMENT_BUFFER_INDEX] = vertex_stage.argument_buffer.buffer;
                cache.vertex_offset[BindingMap::ARGUMENT_BUFFER_INDEX] = vertex_stage.argument_buffer.offset;
                encoder->setVertexBuffer(vertex_stage.argument_buffer.buffer,
                                         vertex_stage.argument_buffer.offset,
                                         BindingMap::ARGUMENT_BUFFER_INDEX);
            }
        }
        if (fragment_stage.argument_buffer.buffer != nullptr) {
            if (cache.fragment_buffer != fragment_stage.argument_buffer.buffer ||
                cache.fragment_offset != fragment_stage.argument_buffer.offset) {
                cache.fragment_buffer = fragment_stage.argument_buffer.buffer;
                cache.fragment_offset = fragment_stage.argument_buffer.offset;
                encoder->setFragmentBuffer(fragment_stage.argument_buffer.buffer,
                                           fragment_stage.argument_buffer.offset,
                                           BindingMap::ARGUMENT_BUFFER_INDEX);
            }
        }

        // Metal has 31 buffer slots and the buffer cache offers 33 bindings, so
        // anything past the limit is dropped. The pipeline's vertex descriptor
        // is clamped the same way.
        constexpr size_t MAX_VERTEX_SLOTS =
            BindingMap::MAX_BUFFER_SLOTS - BindingMap::VERTEX_BUFFER_BASE;
        const size_t slot_count = std::min<size_t>(bindings.size(), MAX_VERTEX_SLOTS);
        for (size_t index = 0; index < slot_count; ++index) {
            if ((enabled_vertex_slots & (1u << index)) == 0) {
                continue;
            }
            // Always the zero buffer, whatever the buffer cache put there.
            const bool is_constant_slot =
                pipeline->State().uses_constant_attributes &&
                index == pipeline->State().constant_binding;
            // Do NOT add `geometry_emulated` here. It looks right, the emulated
            // stage should only read the emitted vertices and their residency is
            // not declared, but zeroing the guest's streams for those draws
            // brought the screen-wide smeared geometry straight back on a build
            // that rendered the overworld fine. It does read them. The theory
            // that sent me here was disproven anyway, every escaping stream
            // logged geometry_emulated=false.
            if (bindings[index].buffer == nullptr || is_constant_slot) {
                if (null_buffer == nullptr) {
                    continue;
                }
                const u32 slot = BindingMap::VERTEX_BUFFER_BASE + static_cast<u32>(index);
                if (cache.vertex_buffer[slot] != null_buffer || cache.vertex_offset[slot] != 0) {
                    cache.vertex_buffer[slot] = null_buffer;
                    cache.vertex_offset[slot] = 0;
                    encoder->setVertexBuffer(null_buffer, 0, slot);
                }
                continue;
            }
            const u32 slot = BindingMap::VERTEX_BUFFER_BASE + static_cast<u32>(index);
            MTL::Buffer* const buffer = bindings[index].buffer;
            const size_t offset = bindings[index].offset;

            if (cache.vertex_buffer[slot] != buffer) {
                cache.vertex_buffer[slot] = buffer;
                cache.vertex_offset[slot] = offset;
                encoder->setVertexBuffer(buffer, offset, slot);
            } else if (cache.vertex_offset[slot] != offset) {
                // Same buffer at a new offset. Metal 4 has no offset-only
                // rebind, the binding IS a GPU address, so re-issue all of it.
                cache.vertex_offset[slot] = offset;
                encoder->setVertexBuffer(buffer, offset, slot);
            }
        }
        if (indirect_draw.buffer != nullptr) {
            for (u32 entry = 0; entry < indirect_draw.count; ++entry) {
                const u32 at = indirect_draw.offset + entry * indirect_draw.stride;
                if (draw.is_indexed) {
                    encoder->drawIndexedPrimitives(primitive, index_binding.type,
                                                   index_binding.buffer, index_binding.offset,
                                                   indirect_draw.buffer, at);
                } else {
                    encoder->drawPrimitives(primitive, indirect_draw.buffer, at);
                }
            }
        } else if (draw.is_indexed) {
            const u32 index_size = index_binding.type == MTL::IndexTypeUInt32 ? 4 : 2;
            // Metal rejects a zero instance count ("instanceCount (0) must be
            // non-zero"). A converted index buffer has its own count (a quad
            // becomes six indices from four) and is already rebased.
            const u32 index_count =
                index_binding.converted ? index_binding.num_indices : draw.num_vertices;
            const u32 index_offset =
                index_binding.converted ? index_binding.offset
                                        : index_binding.offset + draw.first_index * index_size;
            if (index_count == 0 || draw.num_instances == 0) {
                return;
            }
            encoder->drawIndexedPrimitives(
                primitive, index_count, index_binding.type, index_binding.buffer, index_offset,
                draw.num_instances, static_cast<NS::Integer>(draw.base_vertex),
                draw.base_instance);
        } else {
            if (draw.num_vertices == 0 || draw.num_instances == 0) {
                return;
            }
            encoder->drawPrimitives(primitive, draw.base_vertex, draw.num_vertices,
                                    draw.num_instances, draw.base_instance);
        }
    });

    // Once depth is written, the next shader sampling it needs a pass break.
    m_scheduler.MarkPassDrawn(written_attachments, pipeline->HasStorageWrites());
    if (regs.depth_write_enabled != 0) {
        m_scheduler.MarkDepthWritten();
    }

    ++m_draw_counter;
}

bool RasterizerMetal::RecordGeometryDispatch(
    GraphicsPipeline& pipeline, const GeometryEmulation& geometry,
    Tegra::Engines::Maxwell3D::Regs::PrimitiveTopology topology, bool is_indexed, u32 count,
    u32 first_index, u32 base_vertex, u32 num_instances,
    const std::array<StageResources, NUM_PIPELINE_STAGES>& stages,
    std::span<const BufferBinding> vertex_bindings, u32 enabled_vertex_slots,
    GeometryDispatch& out) {
    using Topology = Tegra::Engines::Maxwell3D::Regs::PrimitiveTopology;
    using IndexFormat = Tegra::Engines::Maxwell3D::Regs::IndexFormat;

    const auto refuse = [](const char* why) {
        static std::atomic<u64> refused{0};
        const u64 n = refused.fetch_add(1, std::memory_order_relaxed) + 1;
        if (n <= 8 || n % 10000 == 0) {
            LOG_WARNING(Render_Metal, "Geometry draw refused ({}), {} refused so far", why, n);
        }
        return false;
    };

    if (count == 0 || num_instances == 0) {
        return refuse("empty draw");
    }

    // Everything not already rewritten into a 32-bit triangle list gets
    // flattened: the geometry kernel reads primitive p's vertices at a fixed
    // stride and a strip does not visit them in that order.
    const IndexBufferBinding& index_binding = m_buffer_cache_runtime.GetIndexBinding();
    Topology effective_topology = topology;
    IndexFormat index_format = IndexFormat::UnsignedInt;
    MTL::Buffer* index_source = nullptr;
    u64 index_source_offset = 0;
    bool source_indexed = false;

    if (index_binding.converted) {
        if (ConvertedPrimitive(topology) != MTL::PrimitiveTypeTriangle) {
            return refuse("rewritten topology is not a triangle list");
        }
        effective_topology = Topology::Triangles;
        source_indexed = true;
        index_source = index_binding.buffer;
        index_source_offset = index_binding.offset;
    } else if (is_indexed) {
        if (!index_binding.valid || index_binding.buffer == nullptr) {
            return refuse("indexed draw with no index binding");
        }
        const u32 index_size = index_binding.type == MTL::IndexTypeUInt32 ? 4 : 2;
        index_format = index_binding.type == MTL::IndexTypeUInt32 ? IndexFormat::UnsignedInt
                                                                  : IndexFormat::UnsignedShort;
        source_indexed = true;
        index_source = index_binding.buffer;
        index_source_offset = index_binding.offset + u64{first_index} * index_size;
    }

    const u32 verts_per_prim = IndexPass::VerticesPerPrimitive(effective_topology);
    if (verts_per_prim == 0) {
        return refuse("topology has no primitive expansion");
    }
    if (verts_per_prim != geometry.input_vertices) {
        LOG_WARNING(Render_Metal,
                    "Geometry shader expects {} vertices per primitive, draw supplies {}",
                    geometry.input_vertices, verts_per_prim);
        return refuse("input topology mismatch");
    }

    const u32 primitives = IndexPass::PrimitiveCount(effective_topology, count);
    if (primitives == 0) {
        return refuse("no complete primitives");
    }
    const u32 captured_vertices = primitives * verts_per_prim;
    const u64 total_captured = u64{captured_vertices} * num_instances;
    const u64 total_primitives = u64{primitives} * num_instances;
    const u64 emitted_vertices = total_primitives * geometry.output_capacity;

    constexpr u64 FLOAT4 = 16;
    const u64 order_bytes = u64{captured_vertices} * sizeof(u32);
    const u64 captured_bytes = total_captured * geometry.input_stride * FLOAT4;
    const u64 emitted_bytes = emitted_vertices * geometry.output_stride * FLOAT4;
    if (captured_bytes == 0 || emitted_bytes == 0) {
        return refuse("zero-sized scratch");
    }

    // The staging pool holds a huge request for several frames.
    const u64 scratch_bytes = order_bytes + captured_bytes + emitted_bytes;
    if (scratch_bytes > MAX_GEOMETRY_SCRATCH_PER_DRAW ||
        m_geometry_scratch_bytes + scratch_bytes > MAX_GEOMETRY_SCRATCH_PER_FRAME) {
        static std::atomic<u32> reported{0};
        if (reported++ < 16) {
            LOG_WARNING(Render_Metal,
                        "Geometry emulation skipped: {} primitives want {} MiB of scratch ({} MiB "
                        "already used this frame, {} float4s per emitted vertex, {} slots each)",
                        total_primitives, scratch_bytes / (1024 * 1024),
                        m_geometry_scratch_bytes / (1024 * 1024), geometry.output_stride,
                        geometry.output_capacity);
        }
        return false;
    }
    m_geometry_scratch_bytes += scratch_bytes;

    // Only now, once every refusal is behind us. This ends the render pass and
    // the recording mirror has to be told, or RequestRenderPass skips
    // re-opening a pass it thinks is still open. It used to be the first
    // statement here, so all eleven declining paths stored and reloaded every
    // attachment - on a TBDR at 1080p, megabytes each way per refused draw.
    m_scheduler.RequestOutsideRenderPassOperationContext();

    ConvertedIndices order = m_buffer_cache_runtime.GetIndexPass().ExpandPrimitives(
        effective_topology, index_format, source_indexed, count, base_vertex, index_source,
        static_cast<u32>(index_source_offset));
    if (!order.valid || order.num_indices != captured_vertices) {
        LOG_WARNING(Render_Metal, "Could not flatten {} primitives for geometry emulation",
                    primitives);
        return false;
    }

    const StagingBufferRef captured =
        m_staging_pool.Request(captured_bytes, MemoryUsage::DeviceLocal);
    const StagingBufferRef emitted =
        m_staging_pool.Request(emitted_bytes, MemoryUsage::DeviceLocal);
    if (captured.buffer == nullptr || emitted.buffer == nullptr) {
        LOG_WARNING(Render_Metal, "No staging space for {} captured and {} emitted vertices",
                    total_captured, emitted_vertices);
        return false;
    }

    GraphicsPipeline* const pipeline_ptr = &pipeline;
    MTL::Buffer* const null_buffer = m_buffer_cache_runtime.GetNullBuffer();

    const StageResources& vertex_stage = stages[static_cast<size_t>(PipelineStage::Vertex)];
    boost::container::small_vector<BufferBinding, 8> streams;
    for (const BufferBinding& binding : vertex_bindings) {
        streams.push_back(binding);
    }
    const std::array<u32, 2> draw_base = m_capture_draw_base;
    m_scheduler.Record([pipeline_ptr, captured, order, captured_vertices, num_instances,
                        enabled_vertex_slots, null_buffer, vertex_stage, draw_base,
                        streams = std::move(streams)](CommandContext& context) {
        pipeline_ptr->WaitBuilt();
        MTL::ComputePipelineState* const state = pipeline_ptr->CaptureHandle();
        if (state == nullptr) {
            return;
        }
        ComputeEncoder encoder = context.RequestCompute();
        if (encoder == nullptr) {
            return;
        }
        encoder->setComputePipelineState(state);

        if (vertex_stage.argument_buffer.buffer != nullptr) {
            encoder->setBuffer(vertex_stage.argument_buffer.buffer,
                               vertex_stage.argument_buffer.offset,
                               BindingMap::ARGUMENT_BUFFER_INDEX);
        }

        // Clamped as the draw path clamps it. An out-of-range setBuffer aborts
        // inside Metal rather than reporting a validation error.
        constexpr size_t MAX_VERTEX_SLOTS =
            BindingMap::MAX_BUFFER_SLOTS - BindingMap::VERTEX_BUFFER_BASE;
        const size_t slot_count = std::min<size_t>(streams.size(), MAX_VERTEX_SLOTS);
        for (size_t index = 0; index < slot_count; ++index) {
            if ((enabled_vertex_slots & (1u << index)) == 0) {
                continue;
            }
            const u32 slot = BindingMap::VERTEX_BUFFER_BASE + static_cast<u32>(index);
            if (IsCaptureReservedBufferSlot(slot)) {
                continue;
            }
            MTL::Buffer* const buffer =
                streams[index].buffer != nullptr ? streams[index].buffer : null_buffer;
            if (buffer == nullptr) {
                continue;
            }
            const u64 offset = streams[index].buffer != nullptr ? streams[index].offset : 0;
            encoder->setBuffer(buffer, offset, slot);
        }

        encoder->setBuffer(order.buffer, order.offset, CAPTURE_INDEX_BUFFER_INDEX);
        encoder->setBuffer(captured.buffer, captured.offset, CAPTURE_OUTPUT_BUFFER_INDEX);

        // Each stream's size, which the kernel's fetch bounds-checks against.
        // MUST be bound: unbound, spvStreamLimits reads zero, every fetch
        // decides it is out of range and yields zero, and the kernel silently
        // captures (0,0,0,1) for every vertex.
        std::array<u32, CAPTURE_DRAW_BASE_SLOT + 2> limits{};
        for (size_t index = 0; index < slot_count; ++index) {
            const u32 slot = BindingMap::VERTEX_BUFFER_BASE + static_cast<u32>(index);
            if (slot >= limits.size()) {
                break;
            }
            // Zero for a slot the bind loop refused, or the kernel fetches
            // vertices out of whatever that argument-table slot still holds.
            // Argument tables are scheduler-wide and persist between dispatches,
            // so the stale address always looks plausible instead of faulting.
            if (IsCaptureReservedBufferSlot(slot)) {
                limits[index] = 0;
                // Only worth reporting when the guest actually feeds this slot.
                const bool stream_is_live = (enabled_vertex_slots & (1u << index)) != 0 &&
                                            streams[index].buffer != nullptr &&
                                            streams[index].size != 0;
                if (stream_is_live) {
                    static std::atomic<u32> collisions{0};
                    const u32 n = collisions.fetch_add(1, std::memory_order_relaxed) + 1;
                    if (n <= 4 || n % 1000 == 0) {
                        LOG_WARNING(Render_Metal,
                                    "Vertex stream {} is bound ({} bytes) but wants buffer slot "
                                    "{}, which the capture and geometry kernels reserve ({} so "
                                    "far): its attributes come out as zeroes",
                                    index, streams[index].size, slot, n);
                    }
                }
                continue;
            }
            limits[index] = streams[index].buffer != nullptr ? streams[index].size : 0u;
        }
        limits[CAPTURE_DRAW_BASE_SLOT] = draw_base[0];
        limits[CAPTURE_DRAW_BASE_SLOT + 1] = draw_base[1];
        encoder->setBytes(limits.data(), limits.size() * sizeof(u32),
                          CAPTURE_LIMITS_BUFFER_INDEX);

        // dispatchThreads, not dispatchThreadgroups: the kernel indexes its
        // output by `y * grid.x + x`, so the grid has to be the exact counts.
        const u32 width =
            std::min<u32>(static_cast<u32>(state->maxTotalThreadsPerThreadgroup()), 32);
        encoder->dispatchThreads(MTL::Size::Make(captured_vertices, num_instances, 1),
                                 MTL::Size::Make(std::max(width, 1u), 1, 1));
    });

    const StageResources& geometry_stage = stages[static_cast<size_t>(PipelineStage::Geometry)];
    const u32 thread_count = static_cast<u32>(total_primitives);
    m_scheduler.Record([pipeline_ptr, captured, emitted, geometry_stage,
                        thread_count](CommandContext& context) {
        pipeline_ptr->WaitBuilt();
        MTL::ComputePipelineState* const state = pipeline_ptr->GeometryHandle();
        if (state == nullptr) {
            return;
        }
        ComputeEncoder encoder = context.RequestCompute();
        if (encoder == nullptr) {
            return;
        }
        encoder->setComputePipelineState(state);

        if (geometry_stage.argument_buffer.buffer != nullptr) {
            encoder->setBuffer(geometry_stage.argument_buffer.buffer,
                               geometry_stage.argument_buffer.offset,
                               BindingMap::ARGUMENT_BUFFER_INDEX);
        }
        encoder->setBuffer(captured.buffer, captured.offset, GEOMETRY_INPUT_BUFFER_INDEX);
        encoder->setBuffer(emitted.buffer, emitted.offset, GEOMETRY_OUTPUT_BUFFER_INDEX);

        const u32 width = std::min<u32>(static_cast<u32>(state->maxTotalThreadsPerThreadgroup()),
                                        Shader::Backend::SPIRV::GEOMETRY_AS_COMPUTE_LOCAL_SIZE);
        encoder->dispatchThreads(MTL::Size::Make(thread_count, 1, 1),
                                 MTL::Size::Make(std::max(width, 1u), 1, 1));
    });

    out.emitted = emitted.buffer;
    out.emitted_offset = emitted.offset;
    out.emitted_vertices = static_cast<u32>(emitted_vertices);

    return true;
}

bool RasterizerMetal::RecordXfbCapture(
    GraphicsPipeline& pipeline, const GeometryEmulation& xfb,
    Tegra::Engines::Maxwell3D::Regs::PrimitiveTopology topology, bool is_indexed, u32 count,
    u32 first_index, u32 base_vertex, u32 num_instances,
    const std::array<StageResources, NUM_PIPELINE_STAGES>& stages,
    std::span<const BufferBinding> vertex_bindings, u32 enabled_vertex_slots) {
    m_scheduler.RequestOutsideRenderPassOperationContext();

    using Topology = Tegra::Engines::Maxwell3D::Regs::PrimitiveTopology;
    using IndexFormat = Tegra::Engines::Maxwell3D::Regs::IndexFormat;

    const auto refuse = [&](const char* why) {
        static std::atomic<u64> refused{0};
        const u64 n = refused.fetch_add(1, std::memory_order_relaxed) + 1;
        if (n <= 8 || n % 10000 == 0) {
            LOG_WARNING(Render_Metal, "XFB capture refused ({}), {} so far", why, n);
        }
        return false;
    };

    if (count == 0) {
        return refuse("empty draw");
    }
    if (num_instances > 1) {
        return refuse("instanced capture");
    }

    const IndexBufferBinding& index_binding = m_buffer_cache_runtime.GetIndexBinding();
    Topology effective_topology = topology;
    IndexFormat index_format = IndexFormat::UnsignedInt;
    MTL::Buffer* index_source = nullptr;
    u64 index_source_offset = 0;
    bool source_indexed = false;

    if (index_binding.converted) {
        if (ConvertedPrimitive(topology) != MTL::PrimitiveTypeTriangle) {
            return refuse("rewritten topology is not a triangle list");
        }
        effective_topology = Topology::Triangles;
        source_indexed = true;
        index_source = index_binding.buffer;
        index_source_offset = index_binding.offset;
    } else if (is_indexed) {
        if (!index_binding.valid || index_binding.buffer == nullptr) {
            return refuse("indexed draw with no index binding");
        }
        const u32 index_size = index_binding.type == MTL::IndexTypeUInt32 ? 4 : 2;
        index_format = index_binding.type == MTL::IndexTypeUInt32 ? IndexFormat::UnsignedInt
                                                                  : IndexFormat::UnsignedShort;
        source_indexed = true;
        index_source = index_binding.buffer;
        index_source_offset = index_binding.offset + u64{first_index} * index_size;
    }

    const u32 verts_per_prim = IndexPass::VerticesPerPrimitive(effective_topology);
    if (verts_per_prim == 0) {
        return refuse("topology has no primitive expansion");
    }
    const u32 primitives = IndexPass::PrimitiveCount(effective_topology, count);
    if (primitives == 0) {
        return refuse("no complete primitives");
    }
    const u32 captured_vertices = primitives * verts_per_prim;

    constexpr u64 FLOAT4 = 16;
    constexpr u64 MAX_XFB_CAPTURE_BYTES = 64ull * 1024 * 1024;
    const u64 captured_bytes = u64{captured_vertices} * xfb.input_stride * FLOAT4;
    if (captured_bytes == 0 || captured_bytes > MAX_XFB_CAPTURE_BYTES) {
        return refuse("capture size out of range");
    }

    ConvertedIndices order = m_buffer_cache_runtime.GetIndexPass().ExpandPrimitives(
        effective_topology, index_format, source_indexed, count, base_vertex, index_source,
        static_cast<u32>(index_source_offset));
    if (!order.valid || order.buffer == nullptr || order.num_indices != captured_vertices) {
        // buffer checked explicitly: a non-indexed expansion can report valid
        // with nothing behind it, and setBuffer(nullptr) crashes in the driver.
        return refuse("could not flatten the primitives");
    }

    // Never a shared growing buffer: growth reallocates and invalidates both
    // earlier segments and in-flight lambdas' pointers.
    MTL::Buffer* const capture =
        m_transform_feedback.AcquireCaptureSegment(static_cast<u32>(captured_bytes));
    if (capture == nullptr) {
        return refuse("no capture buffer");
    }

    GraphicsPipeline* const pipeline_ptr = &pipeline;
    MTL::Buffer* const null_buffer = m_buffer_cache_runtime.GetNullBuffer();
    const StageResources& vertex_stage = stages[static_cast<size_t>(PipelineStage::Vertex)];
    boost::container::small_vector<BufferBinding, 8> streams;
    for (const BufferBinding& binding : vertex_bindings) {
        streams.push_back(binding);
    }
    // The lambda holds a raw pointer past the pool's own lifetime.
    capture->retain();
    const std::array<u32, 2> draw_base = m_capture_draw_base;
    m_scheduler.Record([pipeline_ptr, capture, order, captured_vertices, enabled_vertex_slots,
                        null_buffer, vertex_stage, draw_base,
                        streams = std::move(streams)](CommandContext& context) {
        const auto release_capture = [capture]() { capture->release(); };
        const struct Releaser {
            const decltype(release_capture)& fn;
            ~Releaser() {
                fn();
            }
        } releaser{release_capture};
        pipeline_ptr->WaitBuilt();
        MTL::ComputePipelineState* const state = pipeline_ptr->CaptureHandle();
        if (state == nullptr) {
            DROP_DRAW("xfb capture: no capture pipeline");
            return;
        }
        ComputeEncoder encoder = context.RequestCompute();
        if (encoder == nullptr) {
            DROP_DRAW("xfb capture: no compute encoder");
            return;
        }
        encoder->setComputePipelineState(state);

        if (vertex_stage.argument_buffer.buffer != nullptr) {
            encoder->setBuffer(vertex_stage.argument_buffer.buffer,
                               vertex_stage.argument_buffer.offset,
                               BindingMap::ARGUMENT_BUFFER_INDEX);
        }

        constexpr size_t MAX_VERTEX_SLOTS =
            BindingMap::MAX_BUFFER_SLOTS - BindingMap::VERTEX_BUFFER_BASE;
        const size_t slot_count = std::min<size_t>(streams.size(), MAX_VERTEX_SLOTS);
        for (size_t index = 0; index < slot_count; ++index) {
            if ((enabled_vertex_slots & (1u << index)) == 0) {
                continue;
            }
            const u32 slot = BindingMap::VERTEX_BUFFER_BASE + static_cast<u32>(index);
            if (IsCaptureReservedBufferSlot(slot)) {
                continue;
            }
            MTL::Buffer* const buffer =
                streams[index].buffer != nullptr ? streams[index].buffer : null_buffer;
            if (buffer == nullptr) {
                continue;
            }
            const u64 offset = streams[index].buffer != nullptr ? streams[index].offset : 0;
            encoder->setBuffer(buffer, offset, slot);
        }

        encoder->setBuffer(order.buffer, order.offset, CAPTURE_INDEX_BUFFER_INDEX);
        encoder->setBuffer(capture, 0, CAPTURE_OUTPUT_BUFFER_INDEX);

        // Without the stream sizes a guest-supplied index addresses raw memory
        // unchecked and eventually faults the GPU, killing the queue.
        std::array<u32, CAPTURE_DRAW_BASE_SLOT + 2> limits{};
        for (size_t index = 0; index < slot_count; ++index) {
            const u32 slot = BindingMap::VERTEX_BUFFER_BASE + static_cast<u32>(index);
            if (slot >= limits.size()) {
                break;
            }
            if (IsCaptureReservedBufferSlot(slot)) {
                limits[index] = 0;
                const bool stream_is_live = (enabled_vertex_slots & (1u << index)) != 0 &&
                                            streams[index].buffer != nullptr &&
                                            streams[index].size != 0;
                if (stream_is_live) {
                    static std::atomic<u32> collisions{0};
                    const u32 n = collisions.fetch_add(1, std::memory_order_relaxed) + 1;
                    if (n <= 4 || n % 1000 == 0) {
                        LOG_WARNING(Render_Metal,
                                    "Vertex stream {} is bound ({} bytes) but wants buffer slot "
                                    "{}, which the capture and geometry kernels reserve ({} so "
                                    "far): its attributes come out as zeroes",
                                    index, streams[index].size, slot, n);
                    }
                }
                continue;
            }
            limits[index] = streams[index].buffer != nullptr ? streams[index].size : 0u;
        }
        limits[CAPTURE_DRAW_BASE_SLOT] = draw_base[0];
        limits[CAPTURE_DRAW_BASE_SLOT + 1] = draw_base[1];
        encoder->setBytes(limits.data(), limits.size() * sizeof(u32),
                          CAPTURE_LIMITS_BUFFER_INDEX);

        const u32 width =
            std::min<u32>(static_cast<u32>(state->maxTotalThreadsPerThreadgroup()), 32);
        encoder->dispatchThreads(MTL::Size::Make(captured_vertices, 1, 1),
                                 MTL::Size::Make(std::max(width, 1u), 1, 1));
    });

    // Counted for Query: the guest differences these around each capture.
    for (u32 stream = 0; stream < 4; ++stream) {
        const auto& control = maxwell3d->regs.transform_feedback.controls[stream];
        if (control.varying_count == 0 && control.stride == 0) {
            continue;
        }
        m_xfb_primitives[stream] += primitives;
        m_xfb_bytes[stream] += u64{captured_vertices} * control.stride;
    }

    // Into the cache buffers VideoCommon bound this draw: frame end is too late
    // for a draw consuming the capture later in the same frame.
    const auto runtime_bindings = m_buffer_cache_runtime.GetTransformFeedbackBindings();
    boost::container::small_vector<TransformFeedbackBinding, 4> gather_bindings;
    for (const BufferBinding& binding : runtime_bindings) {
        gather_bindings.push_back(TransformFeedbackBinding{
            .buffer = binding.buffer,
            .offset = binding.offset,
            .size = binding.size,
        });
    }
    if (gather_bindings.empty() || gather_bindings[0].buffer == nullptr) {
        static std::atomic<u32> reported{0};
        if (reported.fetch_add(1, std::memory_order_relaxed) < 8) {
            const auto& tfb = maxwell3d->regs.transform_feedback;
            LOG_WARNING(Render_Metal,
                        "XFB gather missing binding 0: enable={} size={} addr={:#x} "
                        "varying_count={} control_stride={} runtime_bindings={} b0={} off={} sz={}",
                        tfb.buffers[0].enable, tfb.buffers[0].size, tfb.buffers[0].Address(),
                        tfb.controls[0].varying_count, tfb.controls[0].stride,
                        runtime_bindings.size(),
                        runtime_bindings.empty()
                            ? nullptr
                            : static_cast<const void*>(runtime_bindings[0].buffer),
                        runtime_bindings.empty() ? 0u : runtime_bindings[0].offset,
                        runtime_bindings.empty() ? 0u : runtime_bindings[0].size);
        }
    }
    m_transform_feedback.Gather(capture, xfb.input_stride * static_cast<u32>(FLOAT4),
                                captured_vertices,
                                std::span(xfb.xfb_varyings.data(), xfb.xfb_varying_count),
                                gather_bindings, xfb.xfb_slot_of_generic, xfb.xfb_position_slot);

    // The CPU-side copy of the same gather, deferred to TickFrame: the guest's
    // own CPU reads see guest RAM and nothing else puts the result there.
    {
        const auto& tfb_regs = maxwell3d->regs.transform_feedback;
        const auto slot_offset_of_word = [&](u32 word) -> s32 {
            constexpr u32 POSITION_WORD = 28;
            constexpr u32 GENERIC_BASE_WORD = 32;
            if (word >= POSITION_WORD && word < POSITION_WORD + 4) {
                return xfb.xfb_position_slot < 0
                           ? -1
                           : static_cast<s32>(xfb.xfb_position_slot * 16 +
                                              (word - POSITION_WORD) * 4);
            }
            if (word < GENERIC_BASE_WORD) {
                return -1;
            }
            const u32 generic = (word - GENERIC_BASE_WORD) / 4;
            const u32 component = (word - GENERIC_BASE_WORD) % 4;
            if (generic >= xfb.xfb_slot_of_generic.size() ||
                xfb.xfb_slot_of_generic[generic] < 0) {
                return -1;
            }
            return static_cast<s32>(xfb.xfb_slot_of_generic[generic] * 16 + component * 4);
        };

        PendingXfbGather pending{};
        pending.capture_stride = xfb.input_stride * static_cast<u32>(FLOAT4);
        pending.vertices = captured_vertices;

        for (u32 buffer_index = 0; buffer_index < 4; ++buffer_index) {
            PendingXfbGather::Destination destination{};
            for (u32 word = 0; word < xfb.xfb_varyings.size(); ++word) {
                const Shader::TransformFeedbackVarying& varying = xfb.xfb_varyings[word];
                if (varying.components == 0 || varying.buffer != buffer_index) {
                    continue;
                }
                const s32 src = slot_offset_of_word(word);
                if (src < 0) {
                    continue;
                }
                destination.out_stride = varying.stride;
                destination.copies.push_back(XfbCopy{
                    .src_offset = static_cast<u32>(src),
                    .dst_offset = varying.offset,
                    .components = varying.components,
                });
            }
            if (destination.copies.empty() || destination.out_stride == 0) {
                continue;
            }
            destination.guest_address =
                tfb_regs.buffers[buffer_index].Address() +
                static_cast<u64>(tfb_regs.buffers[buffer_index].start_offset);
            if (destination.guest_address == 0) {
                continue;
            }
            const u64 declared = static_cast<u64>(tfb_regs.buffers[buffer_index].size);
            const u64 start = static_cast<u64>(tfb_regs.buffers[buffer_index].start_offset);
            destination.guest_size = declared > start ? declared - start : 0;
            if (destination.guest_size == 0) {
                continue;
            }
            pending.destinations.push_back(std::move(destination));
        }
        if (pending.destinations.empty()) {
            m_transform_feedback.ReleaseCaptureSegment(capture);
        } else {
            pending.capture = capture;
            pending.tick = m_scheduler.GetMasterSemaphore().CurrentTick();
            m_pending_xfb.push_back(std::move(pending));
        }
    }

    m_recorded_gpu_work += 4;

    {
        static std::atomic<u64> captures{0};
        const u64 n = captures.fetch_add(1, std::memory_order_relaxed) + 1;
        if (n <= 16 || n % 1000 == 0) {
            LOG_INFO(Render_Metal, "XFB capture {}: {} vertices, {} KiB", n, captured_vertices,
                     captured_bytes / 1024);
        }
    }
    return true;
}

// Handles come from the launch descriptor's constant buffers instead of
// Maxwell's shader stages, and header indexing from linked_tsc. Otherwise the
// same walk in declaration order, which is what SPIRV-Cross numbers by.
void RasterizerMetal::ConfigureComputeResources(
    const ComputePipeline& pipeline, StagingBufferRef& out_argument_buffer) {
    const Shader::Info& info = pipeline.GetInfo();
    const auto& qmd = kepler_compute->launch_description;
    const auto& cbufs = qmd.const_buffer_config;
    const bool via_header_index = qmd.linked_tsc != 0;

    boost::container::small_vector<VideoCommon::ImageViewInOut, 32> views;
    boost::container::small_vector<VideoCommon::SamplerId, 32> samplers;

    std::scoped_lock lock{m_texture_cache.mutex, m_buffer_cache.mutex};

    m_texture_cache.SynchronizeDescriptors(true);
    m_buffer_cache.SetComputeUniformBufferState(info.constant_buffer_mask,
                                                &pipeline.UniformBufferSizes());

    m_buffer_cache.UnbindComputeStorageBuffers();
    u32 ssbo_index = 0;
    for (const auto& desc : info.storage_buffers_descriptors) {
        m_buffer_cache.BindComputeStorageBuffer(ssbo_index, desc.cbuf_index, desc.cbuf_offset,
                                                desc.is_written);
        ++ssbo_index;
    }

    // A descriptor can name a constant buffer the launch descriptor never
    // enabled, whose address walks into unmapped guest memory.
    const auto cbuf_enabled = [&](u32 index) {
        return ((qmd.const_buffer_enable_mask.Value() >> index) & 1) != 0;
    };
    const auto read_handle = [&](const auto& desc, u32 element) {
        const u32 index_offset = element << desc.size_shift;
        const u32 offset = desc.cbuf_offset + index_offset;
        if (!cbuf_enabled(desc.cbuf_index)) {
            return Tegra::Texture::TexturePair(0u, via_header_index);
        }
        const GPUVAddr addr = cbufs[desc.cbuf_index].Address() + offset;
        using Desc = std::decay_t<decltype(desc)>;
        if constexpr (std::is_same_v<Desc, Shader::TextureDescriptor> ||
                      std::is_same_v<Desc, Shader::TextureBufferDescriptor>) {
            if (desc.has_secondary && cbuf_enabled(desc.secondary_cbuf_index)) {
                const u32 second_offset = desc.secondary_cbuf_offset + index_offset;
                const GPUVAddr separate_addr =
                    cbufs[desc.secondary_cbuf_index].Address() + second_offset;
                const u32 lhs_raw = gpu_memory->Read<u32>(addr) << desc.shift_left;
                const u32 rhs_raw =
                    gpu_memory->Read<u32>(separate_addr) << desc.secondary_shift_left;
                return Tegra::Texture::TexturePair(lhs_raw | rhs_raw, via_header_index);
            }
        }
        return Tegra::Texture::TexturePair(gpu_memory->Read<u32>(addr),
                                           via_header_index);
    };
    const auto add_views = [&](const auto& descriptors) {
        for (const auto& desc : descriptors) {
            for (u32 element = 0; element < desc.count; ++element) {
                views.push_back({read_handle(desc, element).first});
            }
        }
    };

    add_views(info.texture_buffer_descriptors);
    add_views(info.image_buffer_descriptors);
    for (const auto& desc : info.texture_descriptors) {
        for (u32 element = 0; element < desc.count; ++element) {
            const auto handle = read_handle(desc, element);
            views.push_back({handle.first});
            samplers.push_back(m_texture_cache.GetSamplerId(handle.second, true));
        }
    }
    add_views(info.image_descriptors);

    m_texture_cache.FillImageViews(std::span(views.data(), views.size()), true, true);

    // Texel buffers are linear textures the *buffer* cache builds. The texture
    // cache resolved them only to recover each one's address and size.
    {
        m_buffer_cache.UnbindComputeTextureBuffers();
        size_t texel_view_it = 0;
        size_t index = 0;
        const auto bind = [&](u32 count, bool is_image, bool is_written,
                              Shader::ImageFormat declared) {
            for (u32 element = 0; element < count; ++element) {
                const VideoCommon::ImageViewId id = views[texel_view_it++].id;
                if (id) {
                    ImageView& view = m_texture_cache.GetImageView(id);
                    m_buffer_cache.BindComputeTextureBuffer(index, view.gpu_addr,
                                                            view.BufferSize(),
                                                            TexelBufferFormat(view.format, is_image,
                                                                              declared),
                                                            is_written, is_image);
                } else {
                    // Unreachable today, FillImageViews always assigns an id.
                    static std::atomic<u32> reported{0};
                    if (reported++ < 4) {
                        LOG_ERROR(Render_Metal,
                                  "Texel buffer slot {} has no view; later bindings will shift",
                                  index);
                    }
                }
                ++index;
            }
        };
        for (const auto& desc : info.texture_buffer_descriptors) {
            bind(desc.count, false, false, Shader::ImageFormat::Typeless);
        }
        for (const auto& desc : info.image_buffer_descriptors) {
            bind(desc.count, true, desc.is_written, desc.format);
        }
    }

    m_buffer_cache.UpdateComputeBuffers();

    m_argument_buffer.Reset();
    m_buffer_cache_runtime.ClearPendingBindings();
    m_buffer_cache.BindHostComputeBuffers();
    // Same over-read protection the graphics path has. Metal has no
    // robustBufferAccess to bound it.
    const auto& compute_sizes = pipeline.UniformBufferSizes();
    u32 compute_cbuf = 0;
    for (const BufferBinding& binding : m_buffer_cache_runtime.GetPendingBindings()) {
        BufferBinding bound = binding;
        if (!binding.is_written && compute_cbuf < compute_sizes.size()) {
            m_buffer_cache_runtime.ExtendUniformBinding(bound, compute_sizes[compute_cbuf]);
            ++compute_cbuf;
        }
        if (bound.is_written) {
            m_argument_buffer.AddStorageBuffer(bound.buffer, bound.offset, bound.size, true);
        } else {
            m_argument_buffer.AddUniformBuffer(bound.buffer, bound.offset, bound.size);
        }
    }

    const auto texture_of = [&](size_t view_index, Shader::TextureType type) -> MTL::Texture* {
        const VideoCommon::ImageViewId id = views[view_index].id;
        if (!id) {
            return nullptr;
        }
        return m_texture_cache.GetImageView(id).Handle(type);
    };

    const std::span<MTL::Texture* const> texel_views = m_buffer_cache_runtime.GetTexelViews();
    size_t texel_it = 0;
    const auto next_texel = [&]() -> MTL::Texture* {
        return texel_it < texel_views.size() ? texel_views[texel_it++] : nullptr;
    };

    size_t view_it = 0;
    size_t sampler_it = 0;
    for (const auto& desc : info.texture_buffer_descriptors) {
        for (u32 element = 0; element < desc.count; ++element) {
            ++view_it;
            m_argument_buffer.AddImage(next_texel(), false);
        }
    }
    for (const auto& desc : info.image_buffer_descriptors) {
        for (u32 element = 0; element < desc.count; ++element) {
            ++view_it;
            m_argument_buffer.AddImage(next_texel(), desc.is_written);
        }
    }
    for (const auto& desc : info.texture_descriptors) {
        for (u32 element = 0; element < desc.count; ++element) {
            MTL::Texture* texture = texture_of(view_it++, desc.type);
            const VideoCommon::SamplerId sampler_id = samplers[sampler_it++];
            const VideoCommon::ImageViewId view_id = views[view_it - 1].id;
            MTL::SamplerState* sampler = nullptr;
            if (sampler_id) {
                const Sampler& state = m_texture_cache.GetSampler(sampler_id);
                sampler = view_id ? SamplerFor(state, m_texture_cache.GetImageView(view_id))
                                  : state.Handle();
            }
            m_argument_buffer.AddTexture(texture, sampler);
        }
    }
    for (const auto& desc : info.image_descriptors) {
        for (u32 element = 0; element < desc.count; ++element) {
            const VideoCommon::ImageViewId id = views[view_it++].id;
            MTL::Texture* image = nullptr;
            if (id) {
                ImageView& view = m_texture_cache.GetImageView(id);
                if (desc.is_written) {
                    m_texture_cache.MarkModification(view.image_id);
                }
                image = view.StorageView(desc.type, desc.format);
            }
            m_argument_buffer.AddImage(image, desc.is_written);
        }
    }

    out_argument_buffer = m_argument_buffer.Build();
}

void RasterizerMetal::ConfigureGraphicsResources(
    const GraphicsPipeline& pipeline, bool is_indexed,
    std::array<StageResources, NUM_PIPELINE_STAGES>& out) {
    using Maxwell = Tegra::Engines::Maxwell3D::Regs;

    const auto& regs = maxwell3d->regs;
    const bool via_header_index =
        regs.sampler_binding == Maxwell::SamplerBinding::ViaHeaderBinding;

    boost::container::small_vector<VideoCommon::ImageViewInOut, 32> views;
    boost::container::small_vector<VideoCommon::SamplerId, 32> samplers;

    std::scoped_lock lock{m_texture_cache.mutex, m_buffer_cache.mutex};

    m_texture_cache.SynchronizeDescriptors(false);
    m_buffer_cache.SetUniformBuffersState(pipeline.EnabledUniformBufferMasks(),
                                          &pipeline.UniformBufferSizes());

    const auto for_each_stage = [&](auto&& func) {
        for (size_t index = 0; index < NUM_PIPELINE_STAGES; ++index) {
            const auto metal_stage = static_cast<PipelineStage>(index);
            if (const Shader::Info* info = pipeline.StageInfo(metal_stage)) {
                func(index, MAXWELL_STAGE_INDEX[index], *info);
            }
        }
    };

    for_each_stage([&](size_t, size_t stage, const Shader::Info& info) {
        m_buffer_cache.UnbindGraphicsStorageBuffers(stage);
        u32 ssbo_index = 0;
        for (const auto& desc : info.storage_buffers_descriptors) {
            m_buffer_cache.BindGraphicsStorageBuffer(stage, ssbo_index, desc.cbuf_index,
                                                     desc.cbuf_offset, desc.is_written);
            ++ssbo_index;
        }

        const auto& cbufs = maxwell3d->state.shader_stages[stage].const_buffers;
        const auto read_handle = [&](const auto& desc, u32 element) {
            const u32 index_offset = element << desc.size_shift;
            const u32 offset = desc.cbuf_offset + index_offset;
            const GPUVAddr addr = cbufs[desc.cbuf_index].address + offset;
            using Desc = std::decay_t<decltype(desc)>;
            if constexpr (std::is_same_v<Desc, Shader::TextureDescriptor> ||
                          std::is_same_v<Desc, Shader::TextureBufferDescriptor>) {
                if (desc.has_secondary) {
                    const u32 second_offset = desc.secondary_cbuf_offset + index_offset;
                    const GPUVAddr separate_addr =
                        cbufs[desc.secondary_cbuf_index].address + second_offset;
                    const u32 lhs_raw = gpu_memory->Read<u32>(addr) << desc.shift_left;
                    const u32 rhs_raw =
                        gpu_memory->Read<u32>(separate_addr) << desc.secondary_shift_left;
                    return Tegra::Texture::TexturePair(lhs_raw | rhs_raw, via_header_index);
                }
            }
            return Tegra::Texture::TexturePair(gpu_memory->Read<u32>(addr), via_header_index);
        };
        const auto add_views = [&](const auto& descriptors) {
            for (const auto& desc : descriptors) {
                for (u32 element = 0; element < desc.count; ++element) {
                    views.push_back({read_handle(desc, element).first});
                }
            }
        };

        add_views(info.texture_buffer_descriptors);
        add_views(info.image_buffer_descriptors);
        for (const auto& desc : info.texture_descriptors) {
            for (u32 element = 0; element < desc.count; ++element) {
                const auto handle = read_handle(desc, element);
                views.push_back({handle.first});
                samplers.push_back(m_texture_cache.GetSamplerId(handle.second, false));
            }
        }
        add_views(info.image_descriptors);
    });

    m_texture_cache.FillImageViews(std::span(views.data(), views.size()), false, true);

    // A draw sampling a texture that is also an attachment of the open pass must
    // end the pass first: on tile-based hardware the attachment is still in tile
    // memory, so sampling it returns pre-pass contents.
    m_texture_cache.CheckFeedbackLoop(
        std::span<const VideoCommon::ImageViewInOut>(views.data(), views.size()));

    // ...and a stricter test of our own. The shared CheckFeedbackLoop only
    // reports a *depth* loop, fine for an immediate-mode GPU but not for a TBDR.
    // Compared by base image handle, not texture pointer - the attachment and
    // the sampled texture are different views over the same image. Only an
    // attachment this pass has written can differ from memory, so sampling one
    // it has not touched, or another level or layer of it, keeps the pass.
    // Deferred renderers bind their whole G-buffer while lighting from it.
    const u32 written_attachments = m_scheduler.PassWrittenAttachments();
    if (const Framebuffer* const framebuffer =
            written_attachments != 0 ? m_texture_cache.GetFramebuffer() : nullptr) {
        const std::span<MTL::Texture* const> attachments = framebuffer->AttachmentImages();
        const std::span<const VideoCommon::SubresourceRange> ranges =
            framebuffer->AttachmentRanges();
        const std::span<const u32> bits = framebuffer->AttachmentBits();
        const auto overlaps = [](const VideoCommon::SubresourceRange& lhs,
                                 const VideoCommon::SubresourceRange& rhs) {
            return lhs.base.level < rhs.base.level + rhs.extent.levels &&
                   rhs.base.level < lhs.base.level + lhs.extent.levels &&
                   lhs.base.layer < rhs.base.layer + rhs.extent.layers &&
                   rhs.base.layer < lhs.base.layer + lhs.extent.layers;
        };
        bool loops = false;
        for (const VideoCommon::ImageViewInOut& view : views) {
            if (loops) {
                break;
            }
            if (!view.id) {
                continue;
            }
            const ImageView& sampled = m_texture_cache.GetImageView(view.id);
            MTL::Texture* const image = sampled.ImageHandle();
            if (image == nullptr) {
                continue;
            }
            for (size_t index = 0; index < attachments.size(); ++index) {
                if (attachments[index] != image || (written_attachments & bits[index]) == 0) {
                    continue;
                }
                // A volume's slices are not array layers, so only the level is compared.
                const bool is_volume = image->textureType() == MTL::TextureType3D;
                VideoCommon::SubresourceRange attachment_range = ranges[index];
                VideoCommon::SubresourceRange sampled_range = sampled.range;
                if (is_volume) {
                    attachment_range.base.layer = sampled_range.base.layer = 0;
                    attachment_range.extent.layers = sampled_range.extent.layers = 1;
                }
                if (overlaps(attachment_range, sampled_range)) {
                    loops = true;
                    break;
                }
            }
        }
        if (loops) {
            static std::atomic<u64> breaks{0};
            const u64 count = breaks.fetch_add(1, std::memory_order_relaxed) + 1;
            if (count == 1 || count % 10000 == 0) {
                LOG_INFO(Render_Metal,
                         "Broke the render pass {} time(s): a draw samples one of its own "
                         "attachments",
                         count);
            }
            m_scheduler.RequestOutsideRenderPassOperationContext();
        }
    }

    {
        size_t texel_view_it = 0;
        for_each_stage([&](size_t, size_t stage, const Shader::Info& info) {
            m_buffer_cache.UnbindGraphicsTextureBuffers(stage);
            size_t index = 0;
            const auto bind = [&](u32 count, bool is_image, bool is_written,
                                  Shader::ImageFormat declared) {
                for (u32 element = 0; element < count; ++element) {
                    const VideoCommon::ImageViewId id = views[texel_view_it++].id;
                    if (id) {
                        ImageView& view = m_texture_cache.GetImageView(id);
                        m_buffer_cache.BindGraphicsTextureBuffer(
                            stage, index, view.gpu_addr, view.BufferSize(),
                            TexelBufferFormat(view.format, is_image, declared), is_written,
                            is_image);
                        } else {
                        // Unreachable today, see the compute path's copy of this.
                        static std::atomic<u32> reported{0};
                        if (reported++ < 4) {
                            LOG_ERROR(Render_Metal,
                                          "Texel buffer slot {} has no view; later bindings will shift",
                                          index);
                        }
                    }

                    ++index;
                }
            };
            for (const auto& desc : info.texture_buffer_descriptors) {
                bind(desc.count, false, false, Shader::ImageFormat::Typeless);
            }
            for (const auto& desc : info.image_buffer_descriptors) {
                bind(desc.count, true, desc.is_written, desc.format);
            }
            texel_view_it += Shader::NumDescriptors(info.texture_descriptors) +
                             Shader::NumDescriptors(info.image_descriptors);
        });
    }

    // Before the index buffer is bound: Metal's restart index is fixed, and a
    // guest using a different one needs the buffer rewritten.
    m_buffer_cache_runtime.SetPrimitiveRestart(regs.primitive_restart.enabled != 0,
                                               regs.primitive_restart.index);
    m_buffer_cache.UpdateGraphicsBuffers(is_indexed);
    m_buffer_cache.BindHostGeometryBuffers(is_indexed);

    size_t view_it = 0;
    size_t sampler_it = 0;
    for_each_stage([&](size_t metal_index, size_t stage, const Shader::Info& info) {
        m_argument_buffer.Reset();
        m_buffer_cache_runtime.ClearPendingBindings();

        m_buffer_cache.BindHostStageBuffers(stage);
        const std::span<const BufferBinding> pending = m_buffer_cache_runtime.GetPendingBindings();

        // A shader can index a constant buffer past the size the guest bound.
        // Maxwell answers zero there. Metal hands back whatever follows in the
        // cache, i.e. another object's data. ExtendUniformBinding pads it.
        const auto& stage_cbufs = maxwell3d->state.shader_stages[stage].const_buffers;
        const auto& uniform_sizes = pipeline.UniformBufferSizes()[stage];
        const u32 uniform_mask = pipeline.EnabledUniformBufferMasks()[stage];
        u32 next_cbuf = 0;
        size_t uniform_slot = 0;
        const size_t num_uniforms = static_cast<size_t>(std::popcount(uniform_mask));

        for (const BufferBinding& binding : pending) {
            BufferBinding bound = binding;
            if (uniform_slot < num_uniforms) {
                while (next_cbuf < 32 && (uniform_mask & (1u << next_cbuf)) == 0) {
                    ++next_cbuf;
                }
                if (next_cbuf < 32) {
                    const auto& cbuf = stage_cbufs[next_cbuf];
                    if (cbuf.enabled && cbuf.address != 0) {
                        // Padded from the cached buffer, never from guest
                        // memory - that discards what the GPU wrote.
                        m_buffer_cache_runtime.ExtendUniformBinding(bound,
                                                                    uniform_sizes[next_cbuf]);
                    }
                    ++next_cbuf;
                }
                ++uniform_slot;
            }
            if (bound.is_written) {
                m_argument_buffer.AddStorageBuffer(bound.buffer, bound.offset, bound.size, true);
            } else {
                m_argument_buffer.AddUniformBuffer(bound.buffer, bound.offset, bound.size);
            }
        }

        const auto texture_of = [&](size_t view_index,
                                    Shader::TextureType type) -> MTL::Texture* {
            const VideoCommon::ImageViewId id = views[view_index].id;
            if (!id) {
                return nullptr;
            }
            return m_texture_cache.GetImageView(id).Handle(type);
        };

        const std::span<MTL::Texture* const> texel_views =
            m_buffer_cache_runtime.GetTexelViews();
        size_t texel_it = 0;
        const auto next_texel = [&]() -> MTL::Texture* {
            return texel_it < texel_views.size() ? texel_views[texel_it++] : nullptr;
        };
        for (const auto& desc : info.texture_buffer_descriptors) {
            for (u32 element = 0; element < desc.count; ++element) {
                ++view_it;
                m_argument_buffer.AddImage(next_texel(), false);
            }
        }
        for (const auto& desc : info.image_buffer_descriptors) {
            for (u32 element = 0; element < desc.count; ++element) {
                ++view_it;
                m_argument_buffer.AddImage(next_texel(), desc.is_written);
            }
        }
        for (const auto& desc : info.texture_descriptors) {
            for (u32 element = 0; element < desc.count; ++element) {
                MTL::Texture* texture = texture_of(view_it++, desc.type);
                const VideoCommon::SamplerId sampler_id = samplers[sampler_it++];
                const VideoCommon::ImageViewId view_id = views[view_it - 1].id;
                MTL::SamplerState* sampler = nullptr;
                if (sampler_id) {
                    const Sampler& state = m_texture_cache.GetSampler(sampler_id);
                    sampler = view_id ? SamplerFor(state, m_texture_cache.GetImageView(view_id))
                                      : state.Handle();
                }
                // Metal does not reject a cube/non-cube mismatch - the argument
                // buffer just holds a handle - so the sample reads garbage.
                if (texture != nullptr &&
                    (desc.type == Shader::TextureType::ColorCube ||
                     desc.type == Shader::TextureType::ColorArrayCube)) {
                    const MTL::TextureType actual = texture->textureType();
                    if (actual != MTL::TextureTypeCube &&
                        actual != MTL::TextureTypeCubeArray) {
                        static std::atomic<u32> reported{0};
                        if (reported++ < 8) {
                            const ImageView& v = m_texture_cache.GetImageView(views[view_it - 1].id);
                            LOG_WARNING(Render_Metal,
                                        "Cube sampler bound a non-cube texture: type={} {}x{} "
                                        "layers={} mips={} fmt={} | view_type={} view_fmt={} "
                                        "range(layer {}+{}) gpu=0x{:x}",
                                        static_cast<u32>(actual), texture->width(),
                                        texture->height(), texture->arrayLength(),
                                        texture->mipmapLevelCount(),
                                        static_cast<u32>(texture->pixelFormat()),
                                        static_cast<u32>(v.type), v.format, v.range.base.layer,
                                        v.range.extent.layers, v.gpu_addr);
                        }
                    }
                }
                m_argument_buffer.AddTexture(texture, sampler);
            }
        }
        for (const auto& desc : info.image_descriptors) {
            for (u32 element = 0; element < desc.count; ++element) {
                const VideoCommon::ImageViewId id = views[view_it++].id;
                MTL::Texture* image = nullptr;
                if (id) {
                    ImageView& view = m_texture_cache.GetImageView(id);
                    // Or a later sync from guest memory overwrites the result.
                    if (desc.is_written) {
                        m_texture_cache.MarkModification(view.image_id);
                    }
                    image = view.StorageView(desc.type, desc.format);
                }
                m_argument_buffer.AddImage(image, desc.is_written);
            }
        }

        // Nothing in Metal checks that the slots written line up with the ones
        // the shader declares, a mismatch silently samples the wrong resource.
        const u32 expected = pipeline.ExpectedArgumentSlots(static_cast<PipelineStage>(metal_index));
        const u32 written = m_argument_buffer.SlotCount();
        if (expected != written) {
            static std::atomic<u32> warned{0};
            if (warned++ < 8) {
                static constexpr std::array<const char*, NUM_PIPELINE_STAGES> NAMES{
                    "vertex", "fragment", "geometry"};
                LOG_WARNING(Render_Metal,
                            "Argument buffer slot mismatch on {} stage: shader declares {}, "
                            "wrote {}",
                            NAMES[metal_index], expected, written);
            }
        }

        out[metal_index].argument_buffer = m_argument_buffer.Build();
        // A failed Build leaves no buffer, and the draw path only rebinds when
        // there IS one - so the stage would keep the previous draw's.
        if (m_argument_buffer.SlotCount() != 0 &&
            out[metal_index].argument_buffer.buffer == nullptr) {
            out[metal_index].resources_failed = true;
        }
    });
}

void RasterizerMetal::UpdatePipelineState() {
    if (maxwell3d == nullptr) {
        return;
    }
    using Maxwell = Tegra::Engines::Maxwell3D::Regs;

    const auto& regs = maxwell3d->regs;

    FixedPipelineState state{};
    // The draw manager's topology, not regs.draw's, re-classed to triangles
    // when the index pass expands it. Metal validates the pairing: a point-class
    // pipeline used with a non-point primitive is a hard error.
    const auto draw_topology = maxwell3d->draw_manager.draw_state.topology;
    state.topology_class = NeedsIndexConversion(draw_topology)
                               ? MTL::PrimitiveTopologyClassTriangle
                               : TopologyClass(draw_topology);
    // The unclassified topology too: a geometry shader's input topology is
    // derived from it and the class throws away Triangles vs TrianglesAdjacency.
    state.topology = static_cast<u8>(draw_topology);
    // What the guest's shaders read as YDirection. Left off - setting it from
    // the register correlates with flicker, and the viewport code already
    // handles orientation.
    state.y_direction_negate = false;
    // NOT set from the guest register, see MakeRuntimeInfo.
    state.force_early_z = false;
    state.ndc_minus_one_to_one = regs.depth_mode == Maxwell::DepthMode::MinusOneToOne;
    state.xfb_enabled = regs.transform_feedback_enabled != 0;
    // ONLY for a draw that submits patches. These registers are live whether or
    // not a title tessellates (`patch_vertices` moves freely), so recording them
    // always mints a new key, and a recompile, for nothing.
    if (draw_topology == Maxwell::PrimitiveTopology::Patches) {
        state.tessellation_domain = static_cast<u8>(regs.tessellation.params.domain_type.Value());
        state.tessellation_spacing = static_cast<u8>(regs.tessellation.params.spacing.Value());
        state.tessellation_clockwise =
            regs.tessellation.params.output_primitives.Value() ==
                    Maxwell::Tessellation::OutputPrimitives::Triangles_CW
                ? 1u
                : 0u;
        state.patch_control_points = static_cast<u8>(regs.patch_vertices);
    }
    // The recompiler bakes the alpha test into the fragment shader.
    state.alpha_test_func = static_cast<u32>(regs.alpha_test_enabled != 0
                                                 ? regs.alpha_test_func
                                                 : Maxwell::ComparisonOp::Always_GL);
    state.alpha_test_ref = std::bit_cast<u32>(regs.alpha_test_ref);
    state.dual_source_blend = ComputeAttachment0DualSourceBlend(regs) ? 1u : 0u;
    // Unconditional: gating on a point topology would miss a geometry shader
    // emitting points from triangle input, only known after translation.
    state.point_size = std::bit_cast<u32>(regs.point_size);
    state.alpha_to_coverage = regs.anti_alias_alpha_control.alpha_to_coverage != 0;
    state.alpha_to_one = regs.anti_alias_alpha_control.alpha_to_one != 0;
    // Two ways the guest asks for no rasterization: rasterize_enable, and a
    // front-and-back cull, which Metal has no cull mode for.
    state.rasterizer_discard =
        regs.rasterize_enable == 0 ||
        (regs.gl_cull_test_enabled != 0 &&
         regs.gl_cull_face == Tegra::Engines::Maxwell3D::Regs::CullFace::FrontAndBack);

    {
        using ProvokingVertex = Tegra::Engines::Maxwell3D::Regs::ProvokingVertex;
        static std::atomic<u32> reported{0};
        const auto report = [&](const char* what) {
            if ((reported.fetch_or(1u, std::memory_order_relaxed) & 1u) == 0) {
                LOG_WARNING(Render_Metal, "{}", what);
            }
        };
        static std::atomic<bool> said_logic{false};
        static std::atomic<bool> said_provoking{false};
        static std::atomic<bool> said_bounds{false};
        static std::atomic<bool> said_line{false};
        (void)report;
        if (regs.logic_op.enable != 0 && !said_logic.exchange(true)) {
            // Apple GPUs can read the attachment in a fragment shader, so this
            // is emulatable - but only in the shared recompiler.
            LOG_WARNING(Render_Metal, "Logic op {} is ignored; Metal has no equivalent",
                        static_cast<u32>(regs.logic_op.op));
        }
        if (regs.provoking_vertex == ProvokingVertex::Last && !said_provoking.exchange(true)) {
            LOG_WARNING(Render_Metal, "Last-vertex flat shading is ignored; Metal provokes first");
        }
        if (regs.depth_bounds_enable != 0 && !said_bounds.exchange(true)) {
            LOG_WARNING(Render_Metal, "Depth bounds test is ignored; Metal has no equivalent");
        }
        if (regs.line_width_aliased > 1.0f && !said_line.exchange(true)) {
            LOG_WARNING(Render_Metal, "Line width {} is ignored; Metal draws one pixel wide",
                        regs.line_width_aliased);
        }
    }

    // Where the constant attributes will live - a slot no real stream wants.
    // CONSTANT_ATTRIBUTE_BINDING is only the preferred one.
    std::bitset<Maxwell::NumVertexArrays> streams_in_use;
    for (size_t index = 0; index < Maxwell::NumVertexAttributes; ++index) {
        const auto& attrib = regs.vertex_attrib_format[index];
        if (attrib.constant != 0) {
            continue;
        }
        if (VertexFormat(attrib.size, attrib.type) == MTL::VertexFormatInvalid) {
            continue;
        }
        const u32 stream = static_cast<u32>(attrib.buffer);
        if (stream < streams_in_use.size()) {
            streams_in_use.set(stream);
        }
    }
    // Downwards from the preferred slot, since guests use the low ones. Capture
    // slots get skipped as well as live streams: the preferred slot lands on
    // Metal buffer 30, CAPTURE_LIMITS_BUFFER_INDEX, so a draw with constant
    // attributes during a capture put both there and one overwrote the other.
    const auto slot_taken = [&](u32 binding) {
        return streams_in_use.test(binding) ||
               IsCaptureReservedBufferSlot(BindingMap::VERTEX_BUFFER_BASE + binding);
    };
    u32 constant_binding = CONSTANT_ATTRIBUTE_BINDING;
    while (constant_binding > 0 && slot_taken(constant_binding)) {
        --constant_binding;
    }
    if (slot_taken(constant_binding)) {
        // Needs 30 simultaneous streams to reach.
        static std::atomic<u32> reported{0};
        if (reported++ < 4) {
            LOG_WARNING(Render_Metal,
                        "All {} vertex slots are in use; constant attributes must share slot {}",
                        streams_in_use.count(), constant_binding);
        }
    }
    state.constant_binding = static_cast<u8>(constant_binding);

    // The shader declares in_attrN at location N, so every attribute the guest
    // has enabled must appear in the descriptor or Metal rejects the pipeline.
    for (size_t index = 0; index < Maxwell::NumVertexAttributes; ++index) {
        const auto& attrib = regs.vertex_attrib_format[index];

        state.attribute_types[index] = static_cast<u8>(AttributeTypeOf(regs, index));

        // Constant attributes have no vertex stream, but the descriptor cannot
        // omit them, so they get an entry on the zero-filled null buffer.
        const bool is_constant = attrib.constant != 0;
        const MTL::VertexFormat format =
            is_constant ? MTL::VertexFormatFloat4 : VertexFormat(attrib.size, attrib.type);
        if (format == MTL::VertexFormatInvalid) {
            continue;
        }

        const u32 buffer_index =
            is_constant ? constant_binding : static_cast<u32>(attrib.buffer);
        if (buffer_index > BindingMap::MAX_VERTEX_BUFFER_SLOT - BindingMap::VERTEX_BUFFER_BASE) {
            // Dropping loses geometry, but binding out of range aborts.
            static std::atomic<u32> reported{0};
            if (reported++ < 8) {
                LOG_WARNING(Render_Metal,
                            "Vertex stream {} exceeds the Metal slot limit, dropping attribute {}",
                            buffer_index, index);
            }
            continue;
        }

        state.attributes[index].format = format;
        state.attributes[index].offset = is_constant ? 0 : attrib.offset;
        state.attributes[index].buffer_index = buffer_index;
        state.bindings[buffer_index].enabled = true;
        if (is_constant) {
            state.bindings[buffer_index].stride = 0;
            state.uses_constant_attributes = true;
        }
    }

    for (size_t index = 0; index < Maxwell::NumVertexArrays; ++index) {
        const auto& stream = regs.vertex_streams[index];
        if (!state.bindings[index].enabled) {
            continue;
        }
        if (state.uses_constant_attributes && index == constant_binding) {
            // The constant slot keeps its zero stride and no divisor.
            continue;
        }
        state.bindings[index].stride = stream.stride;
        state.bindings[index].instanced =
            regs.vertex_stream_instances.IsInstancingEnabled(static_cast<u32>(index));
        state.bindings[index].divisor = state.bindings[index].instanced ? stream.frequency : 0;
    }

    {
        std::scoped_lock lock{m_texture_cache.mutex};
        if (const Framebuffer* framebuffer = m_texture_cache.GetFramebuffer()) {
            const RenderPassState& pass = framebuffer->GetRenderPassState();
            state.num_color_attachments = pass.num_color;
            state.sample_count = pass.sample_count;
            for (u32 index = 0; index < pass.num_color; ++index) {
                if (pass.color[index].texture == nullptr) {
                    continue;
                }
                state.color_attachments[index].format = pass.color[index].texture->pixelFormat();
            }
        }
    }

    // MSL will not compile a float4 written to an integer attachment, so the
    // output type is decided before translation. Taken from the guest formats,
    // the Metal ones above can be absent for a target nothing has drawn to yet.
    for (size_t index = 0; index < state.color_output_types.size(); ++index) {
        const auto format = regs.rt[index].format;
        if (static_cast<u32>(format) == 0) {
            continue;
        }
        const auto pixel_format = VideoCore::Surface::PixelFormatFromRenderTargetFormat(format);
        if (!VideoCore::Surface::IsPixelFormatInteger(pixel_format)) {
            continue;
        }
        state.color_output_types[index] =
            static_cast<u8>(VideoCore::Surface::IsPixelFormatSignedInteger(pixel_format)
                                ? Shader::AttributeType::SignedInt
                                : Shader::AttributeType::UnsignedInt);
    }

    for (u32 index = 0; index < state.num_color_attachments; ++index) {
        auto& target = state.color_attachments[index];

        target.blend_enable = regs.blend.enable[index] != 0;
        if (target.blend_enable) {
            const auto setup = [&target](const auto& blend) {
                target.src_rgb = BlendFactor(blend.color_source);
                target.dst_rgb = BlendFactor(blend.color_dest);
                target.rgb_op = BlendEquation(blend.color_op);
                target.src_alpha = BlendFactor(blend.alpha_source);
                target.dst_alpha = BlendFactor(blend.alpha_dest);
                target.alpha_op = BlendEquation(blend.alpha_op);
            };
            if (regs.blend_per_target_enabled != 0) {
                setup(regs.blend_per_target[index]);
            } else if (regs.iterated_blend.enable != 0 &&
                       Settings::values.use_squashed_iterated_blend) {
                // Maxwell's programmable per-fragment blend unit, which Metal
                // cannot express. While it is on, regs.blend is stale, so the
                // other backends' fixed squashed equation is used instead.
                target.src_rgb = MTL::BlendFactorOne;
                target.dst_rgb = MTL::BlendFactorOne;
                target.rgb_op = MTL::BlendOperationAdd;
                target.src_alpha = MTL::BlendFactorOneMinusSourceColor;
                target.dst_alpha = MTL::BlendFactorZero;
                target.alpha_op = MTL::BlendOperationAdd;
                target.blend_enable = true;
            } else {
                setup(regs.blend);
            }
        }

        const auto mask = regs.color_mask[regs.color_mask_common != 0 ? 0 : index];
        MTL::ColorWriteMask write_mask = MTL::ColorWriteMaskNone;
        if (mask.R != 0) {
            write_mask |= MTL::ColorWriteMaskRed;
        }
        if (mask.G != 0) {
            write_mask |= MTL::ColorWriteMaskGreen;
        }
        if (mask.B != 0) {
            write_mask |= MTL::ColorWriteMaskBlue;
        }
        if (mask.A != 0) {
            write_mask |= MTL::ColorWriteMaskAlpha;
        }
        target.write_mask = write_mask;
    }

    m_pipeline_cache.SetGraphicsState(state);
}

void RasterizerMetal::DrawTexture() {
    if (maxwell3d == nullptr) {
        return;
    }
    SCOPE_EXIT {
        m_gpu.TickWork();
    };

    std::scoped_lock lock{m_texture_cache.mutex};
    m_texture_cache.SynchronizeDescriptors(false);
    m_texture_cache.UpdateRenderTargets(false);

    const Framebuffer* const framebuffer = m_texture_cache.GetFramebuffer();
    if (framebuffer == nullptr) {
        return;
    }
    const auto& state = maxwell3d->draw_manager.draw_texture_state;
    const ImageView& texture = m_texture_cache.GetImageView(state.src_texture);

    const bool src_rescaling = m_texture_cache.IsRescaling() && texture.IsRescaled();
    const bool dst_rescaling = m_texture_cache.IsRescaling() && framebuffer->IsRescaled();
    const auto scale = [](bool rescaling, auto value) {
        const auto dim = static_cast<s32>(value);
        return rescaling ? Settings::values.resolution_info.ScaleUp(dim) : dim;
    };

    const VideoCommon::Region2D dst_region{
        VideoCommon::Offset2D{scale(dst_rescaling, state.dst_x0),
                              scale(dst_rescaling, state.dst_y0)},
        VideoCommon::Offset2D{scale(dst_rescaling, state.dst_x1),
                              scale(dst_rescaling, state.dst_y1)},
    };
    const VideoCommon::Region2D src_region{
        VideoCommon::Offset2D{scale(src_rescaling, state.src_x0),
                              scale(src_rescaling, state.src_y0)},
        VideoCommon::Offset2D{scale(src_rescaling, state.src_x1),
                              scale(src_rescaling, state.src_y1)},
    };

    MTL::Texture* const dst = framebuffer->GetRenderPassState().color[0].texture;
    MTL::Texture* const src = texture.RenderTarget();
    if (dst == nullptr || src == nullptr) {
        return;
    }
    m_blit_image.BlitColor(dst, src, dst_region, src_region,
                           Tegra::Engines::Fermi2D::Filter::Bilinear);
}

void RasterizerMetal::Clear(u32 layer_count) {
    if (!maxwell3d) {
        return;
    }
    // FlushWork()/++m_draw_counter belong here, a clear-dominated frame
    // otherwise never hits the mid-frame commit cadence and FlushCommands
    // early-returns on m_draw_counter == 0. Left out while we bisect a rendering
    // artifact. Tried putting it back and reverted it, Z-A came back with
    // full-screen vertical striping. Do not restore without a Z-A run.

    m_gpu.TickWork();
    if (gpu_memory) {
        gpu_memory->FlushCaching();
    }
    // The clear is expressed entirely by the load actions UpdateRenderTargets
    // sets. It must clear no more than `clear_surface` names, or a depth-only
    // clear also blanks colour.
    const auto& clear = maxwell3d->regs.clear_surface;
    const u32 color_mask = (clear.R != 0 ? MTL::ColorWriteMaskRed : 0u) |
                           (clear.G != 0 ? MTL::ColorWriteMaskGreen : 0u) |
                           (clear.B != 0 ? MTL::ColorWriteMaskBlue : 0u) |
                           (clear.A != 0 ? MTL::ColorWriteMaskAlpha : 0u);
    const ClearAspects aspects{
        .color = color_mask != 0,
        .depth = clear.Z != 0,
        .stencil = clear.S != 0,
        .color_target = clear.RT.Value(),
        .layer = clear.layer.Value(),
        .layer_count = layer_count,
        .color_mask = color_mask,
        .stencil_mask = maxwell3d->regs.stencil_front_mask & 0xffu,
    };
    if (!aspects.color && !aspects.depth && !aspects.stencil) {
        return;
    }

    UpdateRenderTargets(true, aspects);
}

void RasterizerMetal::DispatchCompute() {
    // Drains the guest's pending sync requests, see Clear().
    m_gpu.TickWork();
    if (gpu_memory) {
        gpu_memory->FlushCaching();
    }
    // See the note in Clear(): the same FlushWork()/++m_draw_counter pair is
    // out here for the same bisect, and for the same Z-A striping.

    ComputePipeline* const pipeline = m_pipeline_cache.CurrentComputePipeline();
    if (pipeline == nullptr) {
        static std::atomic<u32> skipped{0};
        const u32 n = ++skipped;
        if (n == 1 || n % 10000 == 0) {
            LOG_WARNING(Render_Metal, "Compute dispatch skipped, no pipeline ({} so far)", n);
        }
        return;
    }
    if (kepler_compute == nullptr) {
        return;
    }

    const auto& qmd = kepler_compute->launch_description;

    StagingBufferRef argument_buffer{};
    ConfigureComputeResources(*pipeline, argument_buffer);

    const u32 max_threads = pipeline->GetMaxTotalThreadsPerThreadgroup();
    MTL::Size threadgroup = pipeline->GetThreadgroupSize();
    const u32 declared = static_cast<u32>(threadgroup.width * threadgroup.height *
                                          threadgroup.depth);
    if (declared > max_threads || declared == 0) {
        LOG_WARNING(Render_Metal, "Compute kernel wants {} threads per group, max is {}", declared,
                    max_threads);
        return;
    }

    MTL::ComputePipelineState* state = pipeline->Handle();

    // A GPU-driven dispatch reads its grid from a buffer the GPU itself wrote,
    // the QMD's grid_dim fields are stale for one. Off by default: every
    // dispatch ends the render pass, a full tile store and reload on Apple GPUs.
    const std::optional<GPUVAddr> indirect =
        Settings::values.metal_indirect_compute.GetValue()
            ? kepler_compute->GetIndirectComputeAddress()
            : std::nullopt;
    if (indirect) {
        static constexpr auto sync_info = VideoCommon::ObtainBufferSynchronize::FullSynchronize;
        static constexpr auto post_op = VideoCommon::ObtainBufferOperation::DiscardWrite;
        static constexpr u32 DISPATCH_ARGS_SIZE = 12; // three u32 group counts
        std::scoped_lock lock{m_buffer_cache.mutex};
        const auto [indirect_buffer, indirect_offset] =
            m_buffer_cache.ObtainBuffer(*indirect, DISPATCH_ARGS_SIZE, sync_info, post_op);
        if (indirect_buffer == nullptr) {
            return;
        }
        MTL::Buffer* const handle = indirect_buffer->Handle();
        m_scheduler.RequestOutsideRenderPassOperationContext();
        m_scheduler.Record([state, threadgroup, handle, indirect_offset, argument_buffer](CommandContext& context) {
            ComputeEncoder encoder = context.RequestCompute();
            if (encoder == nullptr) {
                return;
            }
            encoder->setComputePipelineState(state);
            if (argument_buffer.buffer != nullptr) {
                encoder->setBuffer(argument_buffer.buffer, argument_buffer.offset,
                                   BindingMap::ARGUMENT_BUFFER_INDEX);
            }
            encoder->dispatchThreadgroups(handle, indirect_offset, threadgroup);
        });
        return;
    }

    const MTL::Size grid =
        MTL::Size::Make(qmd.grid_dim_x, qmd.grid_dim_y, qmd.grid_dim_z);
    if (grid.width == 0 || grid.height == 0 || grid.depth == 0) {
        return;
    }

    m_scheduler.RequestOutsideRenderPassOperationContext();

    m_scheduler.Record([state, grid, threadgroup, argument_buffer](CommandContext& context) {
        ComputeEncoder encoder = context.RequestCompute();
        if (encoder == nullptr) {
            return;
        }

        encoder->setComputePipelineState(state);
        if (argument_buffer.buffer != nullptr) {
            encoder->setBuffer(argument_buffer.buffer, argument_buffer.offset,
                               BindingMap::ARGUMENT_BUFFER_INDEX);
        }

        encoder->dispatchThreadgroups(grid, threadgroup);
    });
}

void RasterizerMetal::ResetCounter(VideoCommon::QueryType type) {
    switch (type) {
    case VideoCommon::QueryType::ZPassPixelCount64:
        // The sum restarts from whatever slot the next counting pass claims.
        m_visibility_first_slot = m_visibility_slot;
        break;
    case VideoCommon::QueryType::StreamingByteCount:
        // These accumulate forever, as the hardware counters do, and guests
        // difference a begin/end pair out of them.
        m_xfb_bytes.fill(0);
        break;
    case VideoCommon::QueryType::StreamingPrimitivesSucceeded:
    case VideoCommon::QueryType::StreamingPrimitivesNeeded:
        // Both, or Needed - Succeeded goes non-zero, which guests read as
        // capture overflow.
        m_xfb_primitives.fill(0);
        break;
    default:
        break;
    }
}

/// One u64 count per counting render pass. A frame with more counting passes
/// than this stops counting rather than growing without bound.
static constexpr u32 VISIBILITY_SLOTS = 4096;

void RasterizerMetal::EnsureVisibilityBuffer() {
    if (m_visibility_buffer) {
        return;
    }
    m_visibility_buffer = Adopt(m_device.GetDevice()->newBuffer(
        VISIBILITY_SLOTS * sizeof(u64), MTL::ResourceStorageModeShared));
    if (m_visibility_buffer) {
        m_visibility_buffer->setLabel(ToNSString("eden occlusion counts"));
        std::memset(m_visibility_buffer->contents(), 0, VISIBILITY_SLOTS * sizeof(u64));
        // Nothing is implicitly resident on Metal 4, and this is created here
        // rather than through MemoryAllocator, which handles residency itself.
        m_device.AddResidentAllocation(m_visibility_buffer.get());
        m_device.CommitResidency();
    }
}

void RasterizerMetal::BeginPassVisibility(RenderPassState& pass) {
    if (!m_zpass_enabled) {
        m_visibility_pass_slot = NO_VISIBILITY_SLOT;
        return;
    }
    EnsureVisibilityBuffer();
    if (!m_visibility_buffer || m_visibility_slot >= VISIBILITY_SLOTS) {
        m_visibility_pass_slot = NO_VISIBILITY_SLOT;
        return;
    }
    m_visibility_pass_slot = m_visibility_slot++;
    // Metal only writes the slot if a draw counts into it, so a pass that
    // draws nothing would otherwise contribute a stale total.
    static_cast<u64*>(m_visibility_buffer->contents())[m_visibility_pass_slot] = 0;
    pass.visibility_buffer = m_visibility_buffer.get();
}

u64 RasterizerMetal::ReadVisibilityCount() {
    if (!m_visibility_buffer) {
        return 0;
    }
    m_scheduler.Finish();
    const u64* const slots = static_cast<const u64*>(m_visibility_buffer->contents());
    u64 total = 0;
    for (u32 slot = m_visibility_first_slot; slot < m_visibility_slot; ++slot) {
        total += slots[slot];
    }
    return total;
}

void RasterizerMetal::Query(GPUVAddr gpu_addr, VideoCommon::QueryType type,
                            VideoCommon::QueryPropertiesFlags flags, u32 payload, u32 subreport) {
    if (!gpu_memory) {
        return;
    }
    DrainPendingXfb(XfbDrain::ForGuestRead);
    // An answer must always be written or the guest deadlocks on the semaphore.
    // With no query cache it cannot be truthful, so it errs large: zero makes a
    // title conclude an object is hidden and stop submitting it.
    u64 answer;
    switch (type) {
    case VideoCommon::QueryType::Payload:
        answer = static_cast<u64>(payload);
        break;
    case VideoCommon::QueryType::StreamingPrimitivesNeededMinusSucceeded:
    case VideoCommon::QueryType::TotalStreamingPrimitivesNeededMinusSucceeded:
        // The XFB overflow check, and zero is the truthful answer. The generic
        // max() below reads as "everything overflowed", after which the guest
        // stops issuing capture batches entirely.
        answer = 0;
        break;
    case VideoCommon::QueryType::StreamingPrimitivesSucceeded:
    case VideoCommon::QueryType::StreamingPrimitivesNeeded:
        answer = m_xfb_primitives[subreport & 3];
        break;
    case VideoCommon::QueryType::StreamingByteCount:
        answer = m_xfb_bytes[subreport & 3];
        break;
    case VideoCommon::QueryType::ZPassPixelCount64:
        // NOT answered from the visibility buffer, though it is filled and
        // ReadVisibilityCount works: reading it means Scheduler::Finish, a full
        // CPU-GPU stall once per query. Answering needs a deferred readback.
        [[fallthrough]];
    default:
        answer = std::numeric_limits<u32>::max();
        break;
    }

    if (True(flags & VideoCommon::QueryPropertiesFlags::HasTimeout)) {
        const u64 ticks = m_gpu.GetTicks();
        gpu_memory->Write<u64>(gpu_addr + 8, ticks);
        gpu_memory->Write<u64>(gpu_addr, answer);
    } else {
        gpu_memory->Write<u32>(gpu_addr, static_cast<u32>(answer));
    }
}

void RasterizerMetal::BindGraphicsUniformBuffer(size_t stage, u32 index, GPUVAddr gpu_addr,
                                                u32 size) {
    std::scoped_lock lock{m_buffer_cache.mutex};
    m_buffer_cache.BindGraphicsUniformBuffer(stage, index, gpu_addr, size);
}

void RasterizerMetal::DisableGraphicsUniformBuffer(size_t stage, u32 index) {
    m_buffer_cache.DisableGraphicsUniformBuffer(stage, index);
}

void RasterizerMetal::FlushAll() {}

void RasterizerMetal::FlushRegion(DAddr addr, u64 size, CacheType which) {
    if (addr == 0 || size == 0) {
        return;
    }
    if (True(which & CacheType::TextureCache)) {
        std::scoped_lock lock{m_texture_cache.mutex};
        m_texture_cache.DownloadMemory(addr, size);
    }
    if (True(which & CacheType::BufferCache)) {
        std::scoped_lock lock{m_buffer_cache.mutex};
        m_buffer_cache.DownloadMemory(addr, size);
    }
}

bool RasterizerMetal::MustFlushRegion(DAddr addr, u64 size, CacheType which) {
    if (True(which & CacheType::BufferCache)) {
        std::scoped_lock lock{m_buffer_cache.mutex};
        if (m_buffer_cache.IsRegionGpuModified(addr, size)) {
            return true;
        }
    }
    // Below High accuracy the texture cache is not consulted at all, matching
    // Vulkan. Answering yes makes the engine read a render target back through
    // guest memory, a full drain plus a download.
    if (!Settings::IsGPULevelHigh()) {
        return false;
    }
    if (True(which & CacheType::TextureCache)) {
        std::scoped_lock lock{m_texture_cache.mutex};
        return m_texture_cache.IsRegionGpuModified(addr, size);
    }
    return false;
}

VideoCore::RasterizerDownloadArea RasterizerMetal::GetFlushArea(DAddr addr, u64 size) {
    {
        std::scoped_lock lock{m_texture_cache.mutex};
        auto area = m_texture_cache.GetFlushArea(addr, size);
        if (area) {
            return *area;
        }
    }
    {
        std::scoped_lock lock{m_buffer_cache.mutex};
        auto area = m_buffer_cache.GetFlushArea(addr, size);
        if (area) {
            return *area;
        }
    }
    return VideoCore::RasterizerDownloadArea{
        .start_address = Common::AlignDown(addr, Core::DEVICE_PAGESIZE),
        .end_address = Common::AlignUp(addr + size, Core::DEVICE_PAGESIZE),
        .preemtive = true,
    };
}

void RasterizerMetal::InvalidateRegion(DAddr addr, u64 size, CacheType which) {
    if (addr == 0 || size == 0) {
        return;
    }
    if (True(which & CacheType::TextureCache)) {
        std::scoped_lock lock{m_texture_cache.mutex};
        m_texture_cache.WriteMemory(addr, size);
    }
    if (True(which & CacheType::BufferCache)) {
        std::scoped_lock lock{m_buffer_cache.mutex};
        m_buffer_cache.WriteMemory(addr, size);
    }
    if (True(which & CacheType::ShaderCache)) {
        m_pipeline_cache.InvalidateRegion(addr, size);
    }
}

void RasterizerMetal::InnerInvalidation(
    std::span<const std::pair<DAddr, std::size_t>> sequences) {
    // Overridden purely for the LOCKING: the base class takes a pair of
    // cache-mutex round-trips per page rather than per batch.
    if (Settings::values.skip_cpu_inner_invalidation.GetValue()) {
        return;
    }
    {
        std::scoped_lock lock{m_texture_cache.mutex};
        for (const auto& [addr, size] : sequences) {
            if (addr == 0 || size == 0) {
                continue;
            }
            m_texture_cache.WriteMemory(addr, size);
        }
    }
    {
        std::scoped_lock lock{m_buffer_cache.mutex};
        for (const auto& [addr, size] : sequences) {
            if (addr == 0 || size == 0) {
                continue;
            }
            m_buffer_cache.WriteMemory(addr, size);
        }
    }
    for (const auto& [addr, size] : sequences) {
        if (addr == 0 || size == 0) {
            continue;
        }
        m_pipeline_cache.InvalidateRegion(addr, size);
    }
}

void RasterizerMetal::OnCacheInvalidation(PAddr addr, u64 size) {
    if (addr == 0 || size == 0) {
        return;
    }
    {
        std::scoped_lock lock{m_texture_cache.mutex};
        m_texture_cache.WriteMemory(addr, size);
    }
    {
        std::scoped_lock lock{m_buffer_cache.mutex};
        m_buffer_cache.WriteMemory(addr, size);
    }
    m_pipeline_cache.InvalidateRegion(addr, size);
}

bool RasterizerMetal::OnCPUWrite(PAddr addr, u64 size) {
    if (addr == 0 || size == 0) {
        return false;
    }
    m_pipeline_cache.OnCacheInvalidation(addr, size);
    {
        std::scoped_lock lock{m_buffer_cache.mutex};
        if (m_buffer_cache.OnCPUWrite(addr, size)) {
            return true;
        }
    }
    {
        std::scoped_lock lock{m_texture_cache.mutex};
        m_texture_cache.WriteMemory(addr, size);
    }
    return false;
}

void RasterizerMetal::InvalidateGPUCache() {
    m_gpu.InvalidateGPUCache();
}

void RasterizerMetal::UnmapMemory(DAddr addr, u64 size) {
    {
        std::scoped_lock lock{m_texture_cache.mutex};
        m_texture_cache.UnmapMemory(addr, size);
    }
    {
        std::scoped_lock lock{m_buffer_cache.mutex};
        m_buffer_cache.WriteMemory(addr, size);
    }
    m_pipeline_cache.OnCacheInvalidation(addr, size);
}

void RasterizerMetal::ModifyGPUMemory(size_t as_id, GPUVAddr addr, u64 size) {
    std::scoped_lock lock{m_texture_cache.mutex};
    m_texture_cache.UnmapGPUMemory(as_id, addr, size);
}

// These four must go through FenceManager rather than flushing inline. Flushing
// on every guest fence signal removes all CPU/GPU overlap and slows the guest by
// roughly two orders of magnitude.
void RasterizerMetal::SignalFence(std::function<void()>&& func) {
    DrainPendingXfb(XfbDrain::ForGuestRead);
    m_fence_manager.SignalFence(std::move(func));
}

void RasterizerMetal::SyncOperation(std::function<void()>&& func) {
    DrainPendingXfb(XfbDrain::ForGuestRead);
    m_fence_manager.SyncOperation(std::move(func));
}

void RasterizerMetal::SignalSyncPoint(u32 value) {
    DrainPendingXfb(XfbDrain::ForGuestRead);
    m_fence_manager.SignalSyncPoint(value);
}

void RasterizerMetal::SignalReference() {
    m_fence_manager.SignalReference();
}

void RasterizerMetal::ReleaseFences(bool force) {
    m_fence_manager.WaitPendingFences(force);
}

void RasterizerMetal::FlushAndInvalidateRegion(DAddr addr, u64 size, CacheType which) {
    if (Settings::IsGPULevelHigh()) {
        FlushRegion(addr, size, which);
    }
    InvalidateRegion(addr, size, which);
}

void RasterizerMetal::WaitForIdle() {
    // A GPU *ordering* request, not a request to stall the CPU. The guest issues
    // it thousands of times a second. Ending the encoder is the ordering: Metal
    // 4 tracks no hazards, and the next encoder to open emits a queue-scope
    // barrier that waits for every prior stage. Copies and dispatches end the
    // pass anyway, so inside one only the draws' storage writes need it.
    m_scheduler.RequestDeferredFragmentBarrier();
    m_fence_manager.SignalOrdering();
}

void RasterizerMetal::FragmentBarrier() {
    // NOT a no-op. Everything the shader touches goes through an argument buffer
    // Metal cannot see through, so one draw's write is unordered against a later
    // draw's read in the same encoder.
    //
    // Ending the pass, not memoryBarrierWithScope: the scoped barrier is refused
    // on this hardware ("afterStages can not contain MTLRenderStageTile or
    // MTLRenderStageFragment on this device").
    m_scheduler.RequestDeferredFragmentBarrier();
}

void RasterizerMetal::TiledCacheBarrier() {
    m_scheduler.RequestDeferredFragmentBarrier();
}

void RasterizerMetal::FlushCommands() {
    if (m_draw_counter == 0) {
        return;
    }
    m_draw_counter = 0;
    m_scheduler.FlushAtPassBoundary();
}

void RasterizerMetal::DrainPendingXfb(XfbDrain drain) {
    if (m_pending_xfb.empty() || gpu_memory == nullptr) {
        return;
    }
    // Waiting on the captures instead of Finish()ing the queue: Finish flushes
    // everything recorded and blocks on the newest tick, which in Legends Z-A
    // meant ~170 full pipeline drains a frame and left the GPU idle half the
    // time.
    const bool wait = XFB_WRITEBACK_ENABLED && drain != XfbDrain::WhenComplete;
    if (wait) {
        u64 latest = 0;
        for (const PendingXfbGather& pending : m_pending_xfb) {
            latest = std::max(latest, pending.tick);
        }
        m_scheduler.Wait(latest);
    }

    const MasterSemaphore& semaphore = m_scheduler.GetMasterSemaphore();
    boost::container::small_vector<const PendingXfbGather*, 32> consumed;
    for (const PendingXfbGather& pending : m_pending_xfb) {
        // Recycling a capture segment the GPU is still writing into corrupts
        // the next capture that draws it.
        if (!wait && !semaphore.IsFree(pending.tick)) {
            continue;
        }
        const u8* const captured = static_cast<const u8*>(pending.capture->contents());
        if (captured == nullptr) {
            m_transform_feedback.ReleaseCaptureSegment(pending.capture);
            continue;
        }

        // Do NOT add a value check over these floats. Screening for non-finite
        // or absurd values looked great, 97% of captures came back "dirty", one
        // component per vertex, and it was all bogus. The flagged component is a
        // 10/10/10 packed normal the shader builds with as_type<float>, whose
        // bits are *meant* to read as 9.6e37 or NaN. A capture buffer holds
        // packed integers as often as geometry.
        for (const PendingXfbGather::Destination& destination : pending.destinations) {
            if (!XFB_WRITEBACK_ENABLED) {
                break;
            }
            // Clamped to what the guest declared. The full computed span spills
            // into the next mesh's vertices.
            const u64 wanted = u64{pending.vertices} * destination.out_stride;
            const u32 total_bytes = static_cast<u32>(std::min(wanted, destination.guest_size));
            if (total_bytes == 0) {
                continue;
            }
            // Read-modify-write, so gaps the varyings miss survive.
            boost::container::small_vector<u8, 4096> scratch(total_bytes);
            gpu_memory->ReadBlockUnsafe(destination.guest_address, scratch.data(), total_bytes);
            for (u32 vertex = 0; vertex < pending.vertices; ++vertex) {
                const u8* const src_vertex = captured + u64{vertex} * pending.capture_stride;
                u8* const dst_vertex = scratch.data() + u64{vertex} * destination.out_stride;
                for (const XfbCopy& copy : destination.copies) {
                    std::memcpy(dst_vertex + copy.dst_offset, src_vertex + copy.src_offset,
                                copy.components * sizeof(float));
                }
            }
            // WriteBlockUnsafe on purpose, the same bytes already reached the
            // cache buffer through the GPU gather, so no invalidation is wanted.
            gpu_memory->WriteBlockUnsafe(destination.guest_address, scratch.data(), total_bytes);
        }
        m_transform_feedback.ReleaseCaptureSegment(pending.capture);
        consumed.push_back(&pending);
    }

    if (wait) {
        m_pending_xfb.clear();
    } else {
        std::erase_if(m_pending_xfb, [&consumed](const PendingXfbGather& pending) {
            return std::find(consumed.begin(), consumed.end(), &pending) != consumed.end();
        });
    }
}

void RasterizerMetal::FlushWork() {
    // Bounded by encoded work, not draw count: one draw can carry a capture
    // kernel, an index expansion and a gather per destination buffer.
    //
    // The bound adapts. A submission the GPU watchdog kills fails with
    // MTL4CommandQueueErrorTimeout and is *discarded*, so the frame is torn as
    // well as late. Pokemon Legends Z-A lost 18 submissions in a session at 512,
    // each running 8-50 ms, Odyssey never trips it.
    static constexpr u32 CHECK_MASK = 7;
    static constexpr u32 MIN_WORK_TO_COMMIT = 64;

    // The per-submission pass ceiling lives in Scheduler::RequestRenderPass:
    // this runs per draw, so enforcing it here would split passes mid-flight.
    if ((++m_flush_work_counter & CHECK_MASK) != CHECK_MASK) {
        return;
    }
    static constexpr u32 MAX_WORK_TO_COMMIT = 512;
    if (const u32 timeouts = m_scheduler.GetMasterSemaphore().TimeoutCount();
        timeouts != m_seen_timeouts) {
        m_seen_timeouts = timeouts;
        m_healthy_flushes = 0;
        const u32 reduced = std::max(m_work_to_commit / 2, MIN_WORK_TO_COMMIT);
        if (reduced != m_work_to_commit) {
            m_work_to_commit = reduced;
            LOG_WARNING(Render_Metal,
                        "GPU watchdog killed a submission; committing every {} work units now",
                        m_work_to_commit);
        }
    } else if (m_work_to_commit < MAX_WORK_TO_COMMIT &&
               ++m_healthy_flushes >= HEALTHY_FLUSHES_TO_GROW) {
        // And back up again once the timeouts stop. Halving was one-way, so a
        // burst of kills ratcheted the bound to its floor and left it there for
        // the rest of the run: 256, then 128, then 64, which is 950 submissions
        // a second of pure overhead at 75% GPU occupancy with no frames to show
        // for it.
        m_healthy_flushes = 0;
        m_work_to_commit = std::min(m_work_to_commit * 2, MAX_WORK_TO_COMMIT);
        LOG_INFO(Render_Metal, "No submission killed for a while; back to {} work units",
                 m_work_to_commit);
    }
    // A submission since the last check, at a pass boundary we asked for, starts the count over.
    if (const u64 tick = m_scheduler.CurrentTick(); tick != m_flush_work_tick) {
        m_flush_work_tick = tick;
        m_flush_work_counter = 0;
        m_recorded_gpu_work = 0;
    }
    const u32 work = m_flush_work_counter + m_recorded_gpu_work;
    if (work < m_work_to_commit) {
        m_scheduler.DispatchWork();
        return;
    }
    // Waits for the open pass to end, unless a single pass carries enough to risk the watchdog.
    if (work < m_work_to_commit * 4) {
        m_scheduler.FlushAtPassBoundary();
        m_scheduler.DispatchWork();
        return;
    }
    m_scheduler.Flush();
    m_flush_work_counter = 0;
    m_recorded_gpu_work = 0;
}

void RasterizerMetal::TickFrame() {
    // Throttling on purpose: the wait keeps the CPU from running so far ahead
    // that single submissions grow past the GPU watchdog limit. Inert right now,
    // the wait is tied to XFB_WRITEBACK_ENABLED, so FlushWork's adaptive bound
    // is what holds submissions down.
    DrainPendingXfb(XfbDrain::Throttle);
    // After the drain, so segments it just released are stamped too. Frame end
    // is the first point at which every submission referencing one is known.
    m_transform_feedback.StampReleasedSegments();
    m_draw_counter = 0;
    m_flush_work_counter = 0;
    // Or a long session exhausts the occlusion ring and stops counting.
    if (m_visibility_first_slot == m_visibility_slot) {
        m_visibility_first_slot = 0;
        m_visibility_slot = 0;
    }
    m_geometry_scratch_bytes = 0;
    m_fence_manager.TickFrame();
    // Required, or the bucketed staging cache holds its high-water mark for the
    // rest of the session - every converted index buffer takes one.
    m_staging_pool.TickFrame();
    {
        std::scoped_lock lock{m_texture_cache.mutex};
        m_texture_cache.TickFrame();
    }
    {
        std::scoped_lock lock{m_buffer_cache.mutex};
        m_buffer_cache.TickFrame();
    }
}

bool RasterizerMetal::HasDrawTransformFeedback() {
    // Gated on this backend's XFB emulation, not a device feature: with it off
    // nothing fills the capture buffer, so macro.cpp's CPU fallback is better.
    return Settings::values.metal_transform_feedback.GetValue();
}

bool RasterizerMetal::AccelerateConditionalRendering() {
    if (!maxwell3d) {
        return false;
    }
    using RenderEnable = Tegra::Engines::Maxwell3D::Regs::RenderEnable;
    const auto& regs = maxwell3d->regs;

    // MUST always be false: this backend has no host conditional rendering, and
    // Maxwell3D::ProcessQueryCondition reads a yes as "the rasterizer handles
    // this" and sets execute_on unconditionally - running every command the
    // guest predicated off. Clears are among those commands, so a yes wipes
    // frames that were already drawn (this was the ACNH black screen).
    (void)regs;
    return false;
}

bool RasterizerMetal::AccelerateSurfaceCopy(const Tegra::Engines::Fermi2D::Surface& src,
                                            const Tegra::Engines::Fermi2D::Surface& dst,
                                            const Tegra::Engines::Fermi2D::Config& copy_config) {
    std::scoped_lock lock{m_texture_cache.mutex};
    return m_texture_cache.BlitImage(dst, src, copy_config);
}

Tegra::Engines::AccelerateDMAInterface& RasterizerMetal::AccessAccelerateDMA() {
    return m_accelerate_dma;
}

void RasterizerMetal::AccelerateInlineToMemory(GPUVAddr address, size_t copy_size,
                                               std::span<const u8> memory) {
    // The write is the point of this call - invalidating alone leaves the caches
    // re-reading memory nothing ever wrote. Kepler uploads the compute launch
    // descriptor this way, so dropping it zeroes program_start.
    auto cpu_addr = gpu_memory->GpuToCpuAddress(address);
    if (!cpu_addr) {
        gpu_memory->WriteBlock(address, memory.data(), copy_size);
        m_gpu.InvalidateGPUCache();
        return;
    }
    gpu_memory->WriteBlockUnsafe(address, memory.data(), copy_size);
    m_gpu.InvalidateRegion(*cpu_addr, copy_size);

    std::scoped_lock lock{m_buffer_cache.mutex};
    if (!m_buffer_cache.InlineMemory(*cpu_addr, copy_size, memory)) {
        m_gpu.InvalidateRegion(*cpu_addr, copy_size);
    }
}

void RasterizerMetal::LoadDiskResources(u64 title_id, std::stop_token stop_loading,
                                        const VideoCore::DiskResourceLoadCallback& callback) {
    m_pipeline_cache.LoadDiskResources(title_id, stop_loading, callback);
}

MTL::Texture* RasterizerMetal::GetFramebufferTexture(const Tegra::FramebufferConfig& config,
                                                     u32* unscaled_width, u32* unscaled_height) {
    // Already a device address - do not run it through GpuToCpuAddress.
    const DAddr framebuffer_addr = config.address + config.offset;
    if (framebuffer_addr == 0) {
        return nullptr;
    }

    std::scoped_lock lock{m_texture_cache.mutex};
    const auto [image_view, is_scaled] =
        m_texture_cache.TryFindFramebufferImageView(config, framebuffer_addr);
    if (image_view == nullptr) {
        return nullptr;
    }

    // Reported so the caller can normalise the crop, which is in GUEST pixels.
    // The handle is the shader-visible view, not RenderTarget(): the shared
    // cache swizzles w_source to OneFloat for an opaque framebuffer, and
    // RenderTarget() carries no swizzle (Metal will not render into a swizzled
    // view) so it would hand the compositor the guest's real alpha.
    if (unscaled_width != nullptr) {
        *unscaled_width = image_view->size.width;
    }
    if (unscaled_height != nullptr) {
        *unscaled_height = image_view->size.height;
    }
    return image_view->Handle(Shader::TextureType::Color2D);
}

void RasterizerMetal::InitializeChannel(Tegra::Control::ChannelState& channel) {
    // Required: without it the dirty tables stay empty, writing a render-target
    // register never raises Dirty::RenderTargets, and UpdateRenderTargets
    // early-outs after the first call, so only one render target ever exists.
    VideoCommon::Dirty::SetupDirtyFlags(channel.payload->maxwell_3d.dirty.tables);

    CreateChannel(channel);
    {
        std::scoped_lock lock{m_texture_cache.mutex, m_buffer_cache.mutex};
        m_texture_cache.CreateChannel(channel);
        m_buffer_cache.CreateChannel(channel);
    }
    m_pipeline_cache.CreateChannel(channel);
}

void RasterizerMetal::BindChannel(Tegra::Control::ChannelState& channel) {
    BindToChannel(channel.bind_id);
    {
        std::scoped_lock lock{m_texture_cache.mutex, m_buffer_cache.mutex};
        m_texture_cache.BindToChannel(channel.bind_id);
        m_buffer_cache.BindToChannel(channel.bind_id);
    }
    m_pipeline_cache.BindToChannel(channel.bind_id);

    // The shared buffer cache only rebinds streams whose dirty flag is set, and
    // this backend's vertex_bindings array is global rather than per channel, so
    // an unmarked slot keeps the previous channel's buffer.
    if (maxwell3d != nullptr) {
        auto& flags = maxwell3d->dirty.flags;
        flags[VideoCommon::Dirty::VertexBuffers] = true;
        for (u32 index = VideoCommon::Dirty::VertexBuffer0;
             index <= VideoCommon::Dirty::VertexBuffer31; ++index) {
            flags[index] = true;
        }
    }
}

void RasterizerMetal::ReleaseChannel(s32 channel_id) {
    EraseChannel(channel_id);
    {
        std::scoped_lock lock{m_texture_cache.mutex, m_buffer_cache.mutex};
        m_texture_cache.EraseChannel(channel_id);
        m_buffer_cache.EraseChannel(channel_id);
    }
    m_pipeline_cache.EraseChannel(channel_id);
}

} // namespace Metal
