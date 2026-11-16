// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>

#include "common/common_funcs.h"
#include "common/common_types.h"
#include "common/thread_worker.h"
#include "shader_recompiler/runtime_info.h"
#include "shader_recompiler/shader_info.h"
#include "video_core/buffer_cache/buffer_cache_base.h"
#include "video_core/metal_common/metal_wrapper.h"
#include "video_core/renderer_metal/fixed_pipeline_state.h"
#include "video_core/renderer_metal/mtl_shader_util.h"

namespace VideoCore {
class ShaderNotify;
}

namespace Metal {

class Device;
class Scheduler;

/// Index into the per-stage arrays. Geometry never contributes a function to
/// the render pipeline (it drives a compute dispatch), but it still needs its
/// constant buffers, textures and argument buffers resolved per-stage.
enum class PipelineStage : size_t {
    Vertex = 0,
    Fragment = 1,
    Geometry = 2,
    Count = 3,
};

constexpr size_t NUM_PIPELINE_STAGES = static_cast<size_t>(PipelineStage::Count);

/// Stages the render pipeline itself is built from.
constexpr size_t NUM_RENDER_PIPELINE_STAGES = 2;

/// Maxwell program index backing each Metal stage. The buffer cache is indexed
/// by Maxwell stage (VertexB 0, Geometry 3, Fragment 4) and not by PipelineStage.
/// The wrong index silently feeds a shader another stage's constant buffers.
constexpr std::array<size_t, NUM_PIPELINE_STAGES> MAXWELL_STAGE_INDEX{0, 4, 3};

// Everything a geometry-emulated pipeline needs beyond an ordinary one. The
// guest vertex program becomes a compute kernel writing its outputs to a buffer,
// the geometry program becomes a second kernel expanding those into triangles,
// and the render pipeline's vertex function is a generated passthrough reading
// the result. Buffer layouts are fixed by Shader::Backend::SPIRV's
// geometry-as-compute ABI.
struct GeometryEmulation {
    /// The guest vertex program compiled with capture_output_to_buffer.
    MslShader capture;
    /// The geometry program, already lowered to GLCompute by the recompiler.
    MslShader geometry;

    /// float4s per captured vertex: one per stored generic location, then
    /// gl_Position. Must agree with EmitContext::gs_input_stride.
    u32 input_stride{};
    /// Vertices one input primitive is assembled from.
    u32 input_vertices{};
    /// float4s per emitted vertex, i.e. GEOMETRY_AS_COMPUTE_OUTPUT_STRIDE.
    u32 output_stride{};
    /// Vertices one thread may emit, i.e. GeometryAsComputeOutputCapacity().
    u32 output_capacity{};

    /// Argument-buffer slots the geometry kernel declares, for the same drift
    /// check the render stages get. (The vertex stage's count is the capture
    /// kernel's, since the passthrough consumes nothing.)
    u32 geometry_argument_slots{};

    /// Transform feedback capture riding the capture-kernel machinery. When set,
    /// `geometry` is empty and the guest vertex program still runs as the
    /// pipeline's real vertex function. The capture kernel just also writes the
    /// vertex stream out to the guest's feedback buffers.
    bool xfb_only{};
    /// Capture-buffer float4 slot per generic location, -1 when the vertex
    /// program does not store it. SPIRV-Cross packs stored generics in
    /// ascending location order, then gl_Position last.
    std::array<s8, 32> xfb_slot_of_generic{};
    s8 xfb_position_slot{-1};
    /// The guest's capture table, indexed by attribute word.
    std::array<Shader::TransformFeedbackVarying, 256> xfb_varyings{};
    u32 xfb_varying_count{};
};

/// A compiled MTLRenderPipelineState, built asynchronously, plus the emulated
/// geometry/transform-feedback kernels that go with it. Depth/stencil state is
/// kept out of here on purpose, it lives in DepthStencilCache below so a changed
/// depth comparison rebuilds only that and not the shader pipeline.
class GraphicsPipeline : Common::NonCopyable, Common::NonMoveable {
public:
    /// @param worker_thread Pool the MSL compile runs on. nullptr builds
    ///        synchronously (blocking preload path only).
    /// @param geometry Non-null only when the geometry stage is emulated.
    explicit GraphicsPipeline(const Device& device, Scheduler& scheduler,
                              Common::StatefulThreadWorker<>* worker_thread,
                              const FixedPipelineState& state,
                              std::array<const Shader::Info*, NUM_PIPELINE_STAGES> infos,
                              std::array<const MslShader*, NUM_PIPELINE_STAGES> shaders,
                              VideoCore::ShaderNotify* shader_notify = nullptr,
                              std::unique_ptr<GeometryEmulation> geometry = nullptr);
    ~GraphicsPipeline();

    /// Whether this pipeline is worth binding. Optimistic: an in-flight build
    /// reports valid, only a finished-and-failed one is invalid. The draw path
    /// re-checks Handle() after WaitBuilt().
    [[nodiscard]] bool IsValid() const noexcept {
        return !build_failed.load(std::memory_order::acquire);
    }

    [[nodiscard]] bool IsBuilt() const noexcept {
        return is_built.load(std::memory_order::acquire);
    }

    /// Blocks until the asynchronous build finishes. Call from the scheduler's
    /// worker thread, never from the thread recording draws.
    void WaitBuilt() const;

    /// Null until the build finishes, and stays null if it failed.
    [[nodiscard]] MTL::RenderPipelineState* Handle() const noexcept {
        return pipeline_state.get();
    }

    [[nodiscard]] const FixedPipelineState& State() const noexcept {
        return key;
    }

    /// Whether the fragment program writes colour output @p index. Maxwell leaves a target
    /// the shader does not write untouched, but the MSL output struct declares and
    /// zero-fills every output, so such an attachment has to be masked off.
    [[nodiscard]] static bool StoresColor(const FixedPipelineState& key,
                                          const Shader::Info* fragment_info, u32 index);

    /// Whether any stage writes memory other than its attachments: storage buffers, storage
    /// images or global memory. Only such writes need a guest barrier to end the pass.
    [[nodiscard]] bool HasStorageWrites() const noexcept;

    [[nodiscard]] const Shader::Info* StageInfo(PipelineStage stage) const noexcept {
        const auto& info = stage_infos[static_cast<size_t>(stage)];
        return info ? &*info : nullptr;
    }

    /// Constant-buffer enable masks, indexed by *Maxwell* stage, for
    /// BufferCache::SetUniformBuffersState.
    [[nodiscard]] const std::array<u32, VideoCommon::NUM_STAGES>&
    EnabledUniformBufferMasks() const noexcept {
        return enabled_uniform_buffer_masks;
    }

    [[nodiscard]] const VideoCommon::UniformBufferSizes& UniformBufferSizes() const noexcept {
        return uniform_buffer_sizes;
    }

    /// Argument-buffer slots SPIRV-Cross emitted for @p stage. Zero when the
    /// stage is absent or binds nothing.
    [[nodiscard]] u32 ExpectedArgumentSlots(PipelineStage stage) const noexcept {
        return expected_argument_slots[static_cast<size_t>(stage)];
    }

    /// Whether the guest disabled rasterization, or asked to cull both faces
    /// (no such Metal cull mode). Either way the draw skips rasterization.
    [[nodiscard]] bool IsRasterizerDiscard() const noexcept {
        return key.rasterizer_discard;
    }

    /// Non-null when this draw runs through geometry emulation. Readable
    /// before the build finishes; only the pipeline states need WaitBuilt().
    [[nodiscard]] const GeometryEmulation* GeometryParams() const noexcept {
        return geometry.get();
    }

    /// Whether the guest declared a geometry stage this pipeline runs without,
    /// so the draw path can report it per draw rather than per compile.
    void SetDroppedGeometry() noexcept {
        dropped_geometry = true;
    }
    [[nodiscard]] bool DroppedGeometry() const noexcept {
        return dropped_geometry;
    }

    /// The guest vertex program as a capture kernel. Null until the build
    /// finishes, and stays null if it failed.
    [[nodiscard]] MTL::ComputePipelineState* CaptureHandle() const noexcept {
        return capture_pipeline.get();
    }

    /// The lowered geometry program. Same lifetime rules as CaptureHandle().
    [[nodiscard]] MTL::ComputePipelineState* GeometryHandle() const noexcept {
        return geometry_pipeline.get();
    }

private:
    void Build(std::array<MslShader, NUM_PIPELINE_STAGES> shaders,
               std::array<bool, NUM_PIPELINE_STAGES> present);
    /// Builds the compute pipelines an emulated geometry stage needs. Failing
    /// either fails the whole pipeline.
    [[nodiscard]] bool BuildGeometryPipelines();
    [[nodiscard]] Owned<MTL::VertexDescriptor> BuildVertexDescriptor() const;
    /// The capture kernel's equivalent of the vertex descriptor. Mirrors
    /// MTLVertexDescriptor except step functions run against the dispatch grid.
    /// Unused right now, the capture kernel fetches its attributes manually
    /// (InjectManualVertexFetch) instead of taking them through stage_in.
    [[nodiscard]] Owned<MTL::StageInputOutputDescriptor> BuildStageInputDescriptor() const;
    void SignalBuilt();

    const Device& device;
    Scheduler& scheduler;
    /// Non-null while this pipeline still owes a MarkShaderComplete. Cleared by
    /// SignalBuilt so the pairing happens exactly once however Build ended.
    std::atomic<VideoCore::ShaderNotify*> shader_notify{};
    FixedPipelineState key;

    // Held by value: the caller's Shader::Info lives in a local that dies when
    // CreateGraphicsPipeline returns, so pointers into it would dangle.
    std::array<std::optional<Shader::Info>, NUM_PIPELINE_STAGES> stage_infos{};
    std::array<u32, NUM_PIPELINE_STAGES> expected_argument_slots{};
    std::array<u32, VideoCommon::NUM_STAGES> enabled_uniform_buffer_masks{};
    VideoCommon::UniformBufferSizes uniform_buffer_sizes{};
    std::array<Owned<MTL::Library>, NUM_PIPELINE_STAGES> libraries{};
    std::array<Owned<MTL::Function>, NUM_PIPELINE_STAGES> functions{};

    Owned<MTL::RenderPipelineState> pipeline_state;

    /// Null unless this pipeline emulates a geometry stage.
    std::unique_ptr<GeometryEmulation> geometry;
    bool dropped_geometry{};
    Owned<MTL::Library> capture_library;
    Owned<MTL::Function> capture_function;
    Owned<MTL::ComputePipelineState> capture_pipeline;
    Owned<MTL::Library> geometry_library;
    Owned<MTL::Function> geometry_function;
    Owned<MTL::ComputePipelineState> geometry_pipeline;

    mutable std::mutex build_mutex;
    mutable std::condition_variable build_condvar;
    std::atomic_bool is_built{false};
    std::atomic_bool build_failed{false};
};

/// Builds (and owns) MTLDepthStencilState objects, keyed by their description.
class DepthStencilCache : Common::NonCopyable, Common::NonMoveable {
public:
    explicit DepthStencilCache(const Device& device);
    ~DepthStencilCache();

    [[nodiscard]] MTL::DepthStencilState* Get(const DepthStencilState& state);

private:
    const Device& device;
    std::unordered_map<DepthStencilState, Owned<MTL::DepthStencilState>> cache;
};

} // namespace Metal
