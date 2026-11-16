// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <span>
#include <vector>

#include <boost/container/small_vector.hpp>

#include "common/common_types.h"
#include "video_core/control/channel_state_cache.h"
#include "video_core/engines/maxwell_dma.h"
#include "video_core/metal_common/metal_memory_allocator.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/renderer_metal/mtl_argument_buffer.h"
#include "video_core/renderer_metal/mtl_transform_feedback.h"
#include "video_core/renderer_metal/mtl_blit_image.h"
#include "video_core/renderer_metal/mtl_buffer_cache.h"
#include "video_core/renderer_metal/mtl_fence_manager.h"
#include "video_core/renderer_metal/mtl_pipeline_cache.h"
#include "video_core/renderer_metal/mtl_render_pass_state.h"
#include "video_core/renderer_metal/mtl_staging_buffer_pool.h"
#include "video_core/renderer_metal/mtl_texture_cache.h"

namespace Core {
class System;
}

namespace Metal {
class Device;
class Scheduler;

class AccelerateDMA final : public Tegra::Engines::AccelerateDMAInterface {
public:
    explicit AccelerateDMA(BufferCache& buffer_cache, TextureCache& texture_cache,
                           Scheduler& scheduler);

    bool BufferCopy(GPUVAddr start_address, GPUVAddr end_address, u64 amount) override;
    bool BufferClear(GPUVAddr src_address, u64 amount, u32 value) override;
    bool ImageToBuffer(const Tegra::DMA::ImageCopy& copy_info, const Tegra::DMA::ImageOperand& src,
                       const Tegra::DMA::BufferOperand& dst) override;
    bool BufferToImage(const Tegra::DMA::ImageCopy& copy_info, const Tegra::DMA::BufferOperand& src,
                       const Tegra::DMA::ImageOperand& dst) override;

private:
    /// Don't just return false here. The engine's fallback goes through guest
    /// memory, and a render target the GPU never wrote back reads as nothing.
    template <bool IS_IMAGE_UPLOAD>
    bool DmaBufferImageCopy(const Tegra::DMA::ImageCopy& copy_info,
                            const Tegra::DMA::BufferOperand& buffer_operand,
                            const Tegra::DMA::ImageOperand& image_operand);

    BufferCache& buffer_cache;
    TextureCache& texture_cache;
    Scheduler& scheduler;
};

/// Metal rasterizer. A draw whose pipeline fails to build is skipped rather
/// than issued with wrong state.
class RasterizerMetal final : public VideoCore::RasterizerInterface,
                              protected VideoCommon::ChannelSetupCaches<VideoCommon::ChannelInfo> {
public:
    explicit RasterizerMetal(Tegra::GPU& gpu,
                             Tegra::MaxwellDeviceMemoryManager& device_memory,
                             const Device& device, MemoryAllocator& memory_allocator,
                             StagingBufferPool& staging_pool, Scheduler& scheduler);
    ~RasterizerMetal() override;

    void Draw(bool is_indexed, u32 instance_count) override;
    void DrawIndirect() override;
    void DrawTexture() override;
    void Clear(u32 layer_count) override;
    void DispatchCompute() override;
    void ResetCounter(VideoCommon::QueryType type) override;
    void Query(GPUVAddr gpu_addr, VideoCommon::QueryType type,
               VideoCommon::QueryPropertiesFlags flags, u32 payload, u32 subreport) override;
    void BindGraphicsUniformBuffer(size_t stage, u32 index, GPUVAddr gpu_addr, u32 size) override;
    void DisableGraphicsUniformBuffer(size_t stage, u32 index) override;
    void FlushAll() override;
    void FlushRegion(DAddr addr, u64 size,
                     VideoCommon::CacheType which = VideoCommon::CacheType::All) override;
    bool MustFlushRegion(DAddr addr, u64 size,
                         VideoCommon::CacheType which = VideoCommon::CacheType::All) override;
    void InvalidateRegion(DAddr addr, u64 size,
                          VideoCommon::CacheType which = VideoCommon::CacheType::All) override;
    void InnerInvalidation(std::span<const std::pair<DAddr, std::size_t>> sequences) override;
    void OnCacheInvalidation(PAddr addr, u64 size) override;
    bool OnCPUWrite(PAddr addr, u64 size) override;
    VideoCore::RasterizerDownloadArea GetFlushArea(DAddr addr, u64 size) override;
    void InvalidateGPUCache() override;
    void UnmapMemory(DAddr addr, u64 size) override;
    void ModifyGPUMemory(size_t as_id, GPUVAddr addr, u64 size) override;
    void SignalFence(std::function<void()>&& func) override;
    void SyncOperation(std::function<void()>&& func) override;
    void SignalSyncPoint(u32 value) override;
    void SignalReference() override;
    void ReleaseFences(bool force) override;
    void FlushAndInvalidateRegion(
        DAddr addr, u64 size, VideoCommon::CacheType which = VideoCommon::CacheType::All) override;
    void WaitForIdle() override;
    void FragmentBarrier() override;
    void TiledCacheBarrier() override;
    void FlushCommands() override;
    void TickFrame() override;

    bool AccelerateConditionalRendering() override;
    bool HasDrawTransformFeedback() override;
    bool AccelerateSurfaceCopy(const Tegra::Engines::Fermi2D::Surface& src,
                               const Tegra::Engines::Fermi2D::Surface& dst,
                               const Tegra::Engines::Fermi2D::Config& copy_config) override;
    Tegra::Engines::AccelerateDMAInterface& AccessAccelerateDMA() override;
    void AccelerateInlineToMemory(GPUVAddr address, size_t copy_size,
                                  std::span<const u8> memory) override;
    void LoadDiskResources(u64 title_id, std::stop_token stop_loading,
                           const VideoCore::DiskResourceLoadCallback& callback) override;
    void InitializeChannel(Tegra::Control::ChannelState& channel) override;
    void BindChannel(Tegra::Control::ChannelState& channel) override;
    void ReleaseChannel(s32 channel_id) override;

    /// Hands recorded work to the worker and commits mid-frame. Without this a
    /// guest thread waiting on a fence waits out the whole frame's command list.
    void FlushWork();

    /// Null when @p config has not been rendered yet. Optionally reports the
    /// view's UNSCALED guest extent, what a guest-space crop is normalised to.
    [[nodiscard]] MTL::Texture* GetFramebufferTexture(const Tegra::FramebufferConfig& config,
                                                      u32* unscaled_width = nullptr,
                                                      u32* unscaled_height = nullptr);

private:
    /// Metal overwrites the pass's 8-byte visibility slot instead of
    /// accumulating, so a counter spanning passes needs a fresh slot each pass.
    void EnsureVisibilityBuffer();
    void BeginPassVisibility(RenderPassState& pass);
    /// Sums the slots written since the last reset. Drains the GPU first.
    [[nodiscard]] u64 ReadVisibilityCount();

    Owned<MTL::Buffer> m_visibility_buffer;
    u32 m_visibility_slot{};
    u32 m_visibility_first_slot{};
    static constexpr u32 NO_VISIBILITY_SLOT = ~0u;
    u32 m_visibility_pass_slot{NO_VISIBILITY_SLOT};
    bool m_zpass_enabled{};

    /// Distinct from m_draw_counter: resetting that one here would make
    /// FlushCommands drop a buffer that does have work in it.
    u32 m_flush_work_counter{};
    /// Work units per submission, halved whenever the GPU watchdog kills one.
    u32 m_work_to_commit{512};
    u32 m_seen_timeouts{};
    u32 m_healthy_flushes{};
    /// Roughly two seconds of checks, so a title that can't sustain the larger
    /// bound settles instead of oscillating.
    static constexpr u32 HEALTHY_FLUSHES_TO_GROW = 512;
    /// Encoded GPU work beyond the draws themselves (capture kernels, index
    /// expansions, gathers), so the cadence follows the command buffer.
    u32 m_recorded_gpu_work{};
    /// Submission the FlushWork counters belong to.
    u64 m_flush_work_tick{};

    /// Clearing more than the guest asked for destroys contents it expected to keep.
    struct ClearAspects {
        bool color{};
        bool depth{};
        bool stencil{};
        u32 color_target{};
        /// A clear is a load action, which would cover every layer of the pass.
        u32 layer{};
        u32 layer_count{1};
        /// A load action has no channel mask, so a partial clear must be drawn.
        u32 color_mask{0xf};
        /// The guest's stencil write mask, for the same reason.
        u32 stencil_mask{0xff};
    };

    struct IndirectDraw {
        MTL::Buffer* buffer{};
        u32 offset{};
        u32 stride{};
        /// Entry count resolved on the GPU. See IndexPass::PatchIndirectCount.
        bool include_count{};
        /// The "indirect buffer" is a transform-feedback BYTE count, not draw
        /// arguments. See IndexPass::MakeByteCountDrawArgs.
        bool is_byte_count{};
        u32 count{};
    };

    /// @p indirect is null for an ordinary draw.
    void DrawImpl(bool is_indexed, u32 instance_count, const IndirectDraw* indirect);

    /// False when there is nothing to draw into.
    bool UpdateRenderTargets(bool is_clear, const ClearAspects& aspects);

    void UpdatePipelineState();

    struct StageResources {
        StagingBufferRef argument_buffer{};
        /// The draw must be dropped: the encoder still holds the previous
        /// draw's buffer, so rendering anyway samples another object's data.
        bool resources_failed{};
    };

    /// Writes one argument buffer PER STAGE - never one shared: SPIRV-Cross
    /// numbers `[[id(N)]]` from zero independently for each stage.
    void ConfigureGraphicsResources(const GraphicsPipeline& pipeline, bool is_indexed,
                                    std::array<StageResources, NUM_PIPELINE_STAGES>& out);

    void ConfigureComputeResources(const ComputePipeline& pipeline,
                                   StagingBufferRef& out_argument_buffer);

    struct GeometryDispatch {
        /// Expanded vertices, GEOMETRY_AS_COMPUTE_OUTPUT_STRIDE float4s each.
        MTL::Buffer* emitted{};
        u64 emitted_offset{};
        /// primitives * capacity, including slots no thread filled. Those keep
        /// the kernel's w=0 position and the clipper drops them, so no counter.
        u32 emitted_vertices{};
    };

    /// MUST run before the pass opens, both dispatches need a compute encoder
    /// which closes the render one and drops the draw. False means drop it.
    [[nodiscard]] bool RecordGeometryDispatch(
        GraphicsPipeline& pipeline, const GeometryEmulation& geometry,
        Tegra::Engines::Maxwell3D::Regs::PrimitiveTopology topology, bool is_indexed, u32 count,
        u32 first_index, u32 base_vertex, u32 num_instances,
        const std::array<StageResources, NUM_PIPELINE_STAGES>& stages,
        std::span<const BufferBinding> vertex_bindings, u32 enabled_vertex_slots,
        GeometryDispatch& out);

    Tegra::GPU& m_gpu;
    Tegra::MaxwellDeviceMemoryManager& m_device_memory;
    const Device& m_device;
    Scheduler& m_scheduler;
    StagingBufferPool& m_staging_pool;

    /// Declared before the caches, which hold a reference to it.
    BlitImageHelper m_blit_image;

    TextureCacheRuntime m_texture_cache_runtime;
    TextureCache m_texture_cache;

    BufferCacheRuntime m_buffer_cache_runtime;
    BufferCache m_buffer_cache;

    PipelineCache m_pipeline_cache;
    /// Gathers the captured varyings into the guest's feedback buffers. The
    /// draw still renders after.
    bool RecordXfbCapture(GraphicsPipeline& pipeline, const GeometryEmulation& xfb,
                          Tegra::Engines::Maxwell3D::Regs::PrimitiveTopology topology,
                          bool is_indexed, u32 count, u32 first_index, u32 base_vertex,
                          u32 num_instances,
                          const std::array<StageResources, NUM_PIPELINE_STAGES>& stages,
                          std::span<const BufferBinding> vertex_bindings,
                          u32 enabled_vertex_slots);

    ArgumentBufferBuilder m_argument_buffer;
    TransformFeedback m_transform_feedback;

    /// Cumulative like the hardware counters, per stream since the guest asks
    /// each of the four separately (subreport picks it).
    std::array<u64, 4> m_xfb_primitives{};
    /// Base vertex and base instance of the draw being recorded.
    std::array<u32, 2> m_capture_draw_base{};
    std::array<u64, 4> m_xfb_bytes{};

    /// One captured varying to move to guest memory, in bytes.
    struct XfbCopy {
        u32 src_offset;
        u32 dst_offset;
        u32 components;
    };
    /// A capture recorded this frame, also written to guest RAM at TickFrame.
    struct PendingXfbGather {
        /// Borrowed from TransformFeedback's segment pool, not owned, so a
        /// capture never pins a fresh allocation in the residency set.
        MTL::Buffer* capture{};
        u64 tick{};
        u32 capture_stride;
        u32 vertices;
        struct Destination {
            u64 guest_address;
            /// Bytes the guest made available at guest_address. The write-back
            /// is clamped to it, an overrun lands in the next mesh.
            u64 guest_size;
            u32 out_stride;
            boost::container::small_vector<XfbCopy, 16> copies;
        };
        boost::container::small_vector<Destination, 4> destinations;
    };
    std::vector<PendingXfbGather> m_pending_xfb;

    enum class XfbDrain {
        /// Retire what the GPU has already finished. Never blocks.
        WhenComplete,
        /// Wait, then scatter into guest RAM. For anything that makes a capture
        /// visible to the guest.
        ForGuestRead,
        /// As ForGuestRead, at frame end. TickFrame relies on the wait to throttle.
        Throttle,
    };

    /// The guest-RAM gather this exists for is off (see XFB_WRITEBACK_ENABLED), so
    /// fences, queries and sync points no longer wait; that cost ~170 pipeline
    /// drains a frame.
    void DrainPendingXfb(XfbDrain drain);

    NullQueryCache m_query_cache;
    FenceManager m_fence_manager;

    /// So FlushCommands() can avoid committing an empty command buffer.
    u32 m_draw_counter{};

    /// Both allocations scale with the draw, so one bad mesh can ask for
    /// gigabytes. Over the limit we refuse it, there is no smaller version.
    static constexpr u64 MAX_GEOMETRY_SCRATCH_PER_DRAW = 64ULL * 1024 * 1024;
    static constexpr u64 MAX_GEOMETRY_SCRATCH_PER_FRAME = 256ULL * 1024 * 1024;
    u64 m_geometry_scratch_bytes{};

    /// Opened just before the draw, so uploads can't close the encoder first.
    RenderPassState m_render_pass{};
    bool m_has_render_pass{};

    AccelerateDMA m_accelerate_dma;
};

} // namespace Metal
