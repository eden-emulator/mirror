// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <format>
#include <cstdlib>
#include <algorithm>
#include <ranges>
#include <array>
#include <atomic>
#include <mutex>
#include <set>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_set>

#include "common/alignment.h"
#include "common/logging.h"
#include "video_core/metal_common/metal_device.h"
#include "video_core/renderer_metal/maxwell_to_mtl.h"
#include "video_core/shader_notify.h"
#include "video_core/renderer_metal/mtl_graphics_pipeline.h"
#include "video_core/renderer_metal/mtl_scheduler.h"

namespace Metal {

namespace {

constexpr const char* StageName(PipelineStage stage) {
    switch (stage) {
    case PipelineStage::Vertex:
        return "vertex";
    case PipelineStage::Fragment:
        return "fragment";
    case PipelineStage::Geometry:
        return "geometry";
    default:
        return "unknown";
    }
}

/// Metal exposes 31 vertex attribute slots (0..30). Indexing past the end
/// aborts the process rather than reporting a validation error, so bound it.
constexpr size_t MAX_ATTRIBUTE_SLOTS = 31;

} // Anonymous namespace

GraphicsPipeline::GraphicsPipeline(
    const Device& device_, Scheduler& scheduler_, Common::StatefulThreadWorker<>* worker_thread,
    const FixedPipelineState& state,
    std::array<const Shader::Info*, NUM_PIPELINE_STAGES> infos,
    std::array<const MslShader*, NUM_PIPELINE_STAGES> shaders,
    VideoCore::ShaderNotify* shader_notify_, std::unique_ptr<GeometryEmulation> geometry_)
    : device{device_}, scheduler{scheduler_}, shader_notify{shader_notify_},
      key{state}, geometry{std::move(geometry_)} {
    if (shader_notify_ != nullptr) {
        shader_notify_->MarkShaderBuilding();
    }
    // Copied: the caller's Shader::Info and MslShader are locals that die well
    // before an async build runs.
    std::array<MslShader, NUM_PIPELINE_STAGES> owned_shaders{};
    std::array<bool, NUM_PIPELINE_STAGES> present{};
    for (size_t index = 0; index < NUM_PIPELINE_STAGES; ++index) {
        if (infos[index] != nullptr) {
            const Shader::Info& info = *infos[index];
            stage_infos[index] = info;
            const size_t maxwell_stage = MAXWELL_STAGE_INDEX[index];
            enabled_uniform_buffer_masks[maxwell_stage] = info.constant_buffer_mask;
            std::ranges::copy(info.constant_buffer_used_sizes,
                              uniform_buffer_sizes[maxwell_stage].begin());
        }
        if (shaders[index] != nullptr) {
            owned_shaders[index] = *shaders[index];
            expected_argument_slots[index] = shaders[index]->argument_buffer_slots;
            present[index] = true;
        }
    }

    if (geometry) {
        // The capture kernel consumes the vertex argument buffer, so it owns the
        // slot count.
        expected_argument_slots[static_cast<size_t>(PipelineStage::Vertex)] =
            geometry->capture.argument_buffer_slots;
        expected_argument_slots[static_cast<size_t>(PipelineStage::Geometry)] =
            geometry->geometry_argument_slots;
    }

    if (worker_thread == nullptr) {
        Build(std::move(owned_shaders), present);
        return;
    }
    worker_thread->QueueWork(
        [this, shaders = std::move(owned_shaders), present]() mutable {
            Build(std::move(shaders), present);
        });
}

bool GraphicsPipeline::StoresColor(const FixedPipelineState& key,
                                   const Shader::Info* fragment_info, u32 index) {
    if (fragment_info == nullptr || index >= fragment_info->stores_frag_color.size()) {
        return false;
    }
    // Dual-source blending feeds both outputs into attachment 0.
    if (key.dual_source_blend != 0 && index == 0) {
        return fragment_info->stores_frag_color[0] || fragment_info->stores_frag_color[1];
    }
    return fragment_info->stores_frag_color[index];
}

bool GraphicsPipeline::HasStorageWrites() const noexcept {
    for (const std::optional<Shader::Info>& info : stage_infos) {
        if (!info) {
            continue;
        }
        if (info->stores_global_memory ||
            std::ranges::any_of(info->storage_buffers_descriptors,
                                [](const auto& desc) { return desc.is_written; }) ||
            std::ranges::any_of(info->image_descriptors,
                                [](const auto& desc) { return desc.is_written; }) ||
            std::ranges::any_of(info->image_buffer_descriptors,
                                [](const auto& desc) { return desc.is_written; })) {
            return true;
        }
    }
    return false;
}

GraphicsPipeline::~GraphicsPipeline() {
    // The worker holds a bare `this`, so it must outlive a queued build.
    WaitBuilt();
}

void GraphicsPipeline::SignalBuilt() {
    {
        std::scoped_lock lock{build_mutex};
        is_built.store(true, std::memory_order::release);
        build_condvar.notify_all();
    }
    // Exchanged, not tested, so a second SignalBuilt cannot double-decrement.
    if (VideoCore::ShaderNotify* const notify = shader_notify.exchange(nullptr)) {
        notify->MarkShaderComplete();
    }
}

void GraphicsPipeline::WaitBuilt() const {
    if (is_built.load(std::memory_order::acquire)) {
        return;
    }
    std::unique_lock lock{build_mutex};
    build_condvar.wait(lock, [this] { return is_built.load(std::memory_order::acquire); });
}

Owned<MTL::VertexDescriptor> GraphicsPipeline::BuildVertexDescriptor() const {
    Owned<MTL::VertexDescriptor> descriptor = Adopt(MTL::VertexDescriptor::alloc()->init());

    // Smallest stride each binding must declare to span its attributes. A
    // too-small stride is not a validation error, just silently wrong bytes.
    std::array<u32, MAX_VERTEX_BUFFERS> min_strides{};

    auto* attributes = descriptor->attributes();
    const size_t max_attributes =
        std::min<size_t>(key.attributes.size(), MAX_ATTRIBUTE_SLOTS);
    for (size_t index = 0; index < max_attributes; ++index) {
        const FixedPipelineState::VertexAttribute& attribute = key.attributes[index];
        if (attribute.format == MTL::VertexFormatInvalid) {
            continue;
        }
        auto* target = attributes->object(index);
        target->setFormat(attribute.format);
        target->setOffset(attribute.offset);
        target->setBufferIndex(BindingMap::VERTEX_BUFFER_BASE + attribute.buffer_index);

        if (attribute.buffer_index < min_strides.size()) {
            u32& min_stride = min_strides[attribute.buffer_index];
            min_stride = std::max(min_stride, attribute.offset + VertexFormatSize(attribute.format));
        }
    }

    auto* layouts = descriptor->layouts();
    const size_t max_bindings =
        std::min<size_t>(key.bindings.size(),
                         BindingMap::MAX_BUFFER_SLOTS - BindingMap::VERTEX_BUFFER_BASE);
    for (size_t index = 0; index < max_bindings; ++index) {
        const FixedPipelineState::VertexBinding& binding = key.bindings[index];
        if (!binding.enabled) {
            continue;
        }
        auto* layout = layouts->object(BindingMap::VERTEX_BUFFER_BASE + index);
        const u32 required = min_strides[index];
        const u32 min_stride = Common::AlignUp(std::max(required, 4u), 4u);

        if (binding.stride == 0) {
            // Metal rejects stride 0, so express it as a constant step instead.
            layout->setStride(min_stride);
            layout->setStepFunction(MTL::VertexStepFunctionConstant);
            layout->setStepRate(0);
            continue;
        }

        if (binding.stride < required) {
            // Honouring it reads past each vertex, widening it changes spacing.
            static std::atomic<u32> reported{0};
            if (reported++ < 8) {
                LOG_WARNING(Render_Metal,
                            "Vertex stream {} stride {} is below the {} bytes its attributes read",
                            index, binding.stride, required);
            }
        }

        layout->setStride(binding.stride);
        // Only a NON-ZERO divisor selects per-instance stepping. Mapping
        // `instanced && divisor == 0` to VertexStepFunctionConstant was tried and
        // is wrong: `IsInstancingEnabled` is set for streams that step per vertex,
        // so Constant made every vertex of those meshes read element 0,
        // collapsing them to a point. Do not revisit without clearing the shader
        // cache, a stale pipeline made it look like it worked.
        if (binding.divisor == 0) {
            layout->setStepFunction(MTL::VertexStepFunctionPerVertex);
            layout->setStepRate(1);
        } else {
            layout->setStepFunction(MTL::VertexStepFunctionPerInstance);
            layout->setStepRate(binding.divisor);
        }
    }

    return descriptor;
}

Owned<MTL::StageInputOutputDescriptor> GraphicsPipeline::BuildStageInputDescriptor() const {
    Owned<MTL::StageInputOutputDescriptor> descriptor =
        Adopt(MTL::StageInputOutputDescriptor::alloc()->init());

    std::array<u32, MAX_VERTEX_BUFFERS> min_strides{};

    auto* attributes = descriptor->attributes();
    const size_t max_attributes = std::min<size_t>(key.attributes.size(), MAX_ATTRIBUTE_SLOTS);
    for (size_t index = 0; index < max_attributes; ++index) {
        const FixedPipelineState::VertexAttribute& attribute = key.attributes[index];
        if (attribute.format == MTL::VertexFormatInvalid) {
            continue;
        }
        const u32 slot = BindingMap::VERTEX_BUFFER_BASE + attribute.buffer_index;
        if (IsCaptureReservedBufferSlot(slot)) {
            // A dropped attribute then reads undefined data, not zero.
            static std::atomic<u32> reported{0};
            if (reported++ < 8) {
                LOG_WARNING(Render_Metal,
                            "Capture kernel drops attribute {}: stream slot {} is one of its own "
                            "buffers",
                            index, slot);
            }
            continue;
        }
        auto* target = attributes->object(index);
        // MTLAttributeFormat mirrors MTLVertexFormat value for value.
        target->setFormat(static_cast<MTL::AttributeFormat>(attribute.format));
        target->setOffset(attribute.offset);
        target->setBufferIndex(slot);

        if (attribute.buffer_index < min_strides.size()) {
            u32& min_stride = min_strides[attribute.buffer_index];
            min_stride =
                std::max(min_stride, attribute.offset + VertexFormatSize(attribute.format));
        }
    }

    auto* layouts = descriptor->layouts();
    const size_t max_bindings = std::min<size_t>(
        key.bindings.size(), BindingMap::MAX_BUFFER_SLOTS - BindingMap::VERTEX_BUFFER_BASE);
    for (size_t index = 0; index < max_bindings; ++index) {
        const FixedPipelineState::VertexBinding& binding = key.bindings[index];
        const u32 slot = BindingMap::VERTEX_BUFFER_BASE + static_cast<u32>(index);
        if (!binding.enabled || IsCaptureReservedBufferSlot(slot)) {
            continue;
        }
        auto* layout = layouts->object(slot);
        const u32 min_stride = Common::AlignUp(std::max(min_strides[index], 4u), 4u);

        if (binding.stride == 0) {
            layout->setStride(min_stride);
            layout->setStepFunction(MTL::StepFunctionConstant);
            layout->setStepRate(0);
            continue;
        }
        layout->setStride(binding.stride);
        if (binding.divisor == 0) {
            // Always indexed: the capture runs off a generated index buffer.
            layout->setStepFunction(MTL::StepFunctionThreadPositionInGridXIndexed);
            layout->setStepRate(1);
        } else {
            // Instances are the dispatch's Y axis, matching the capture grid.
            layout->setStepFunction(MTL::StepFunctionThreadPositionInGridY);
            layout->setStepRate(binding.divisor);
        }
    }

    descriptor->setIndexType(MTL::IndexTypeUInt32);
    descriptor->setIndexBufferIndex(CAPTURE_INDEX_BUFFER_INDEX);
    return descriptor;
}

bool GraphicsPipeline::BuildGeometryPipelines() {
    capture_library = BuildLibrary(device, geometry->capture);
    if (!capture_library) {
        LOG_ERROR(Render_Metal, "Failed to build the vertex capture kernel library");
        return false;
    }
    capture_function = GetFunction(capture_library.get(), geometry->capture.entry_point);
    if (!capture_function) {
        return false;
    }

    // A transform-feedback-only capture has no geometry kernel.
    if (!geometry->xfb_only) {
        geometry_library = BuildLibrary(device, geometry->geometry);
        if (!geometry_library) {
            LOG_ERROR(Render_Metal, "Failed to build the geometry kernel library");
            return false;
        }
        geometry_function = GetFunction(geometry_library.get(), geometry->geometry.entry_point);
        if (!geometry_function) {
            return false;
        }
    }

    // MTL4::ComputePipelineDescriptor has no stage-input descriptor, so the
    // capture kernel fetches its attributes itself. See InjectManualVertexFetch.
    capture_pipeline = BuildComputePipeline(device, capture_library.get(),
                                            geometry->capture.entry_point,
                                            "eden vertex capture kernel");
    if (!capture_pipeline) {
        return false;
    }

    if (!geometry->xfb_only) {
        geometry_pipeline = BuildComputePipeline(device, geometry_library.get(),
                                                 geometry->geometry.entry_point,
                                                 "eden geometry kernel");
        if (!geometry_pipeline) {
            return false;
        }
    }
    return true;
}

void GraphicsPipeline::Build(std::array<MslShader, NUM_PIPELINE_STAGES> shaders,
                             std::array<bool, NUM_PIPELINE_STAGES> present) {
    ScopedAutoreleasePool pool;

    // Every exit path has to end the build, or a draw in WaitBuilt() hangs.
    const auto fail = [this] {
        build_failed.store(true, std::memory_order::release);
        SignalBuilt();
    };

    for (size_t index = 0; index < NUM_PIPELINE_STAGES; ++index) {
        if (!present[index]) {
            continue;
        }
        const MslShader& shader = shaders[index];

        libraries[index] = BuildLibrary(device, shader);
        if (!libraries[index]) {
            LOG_ERROR(Render_Metal, "Failed to build the {} library",
                      StageName(static_cast<PipelineStage>(index)));
            fail();
            return;
        }
        functions[index] = GetFunction(libraries[index].get(), shader.entry_point);
        if (!functions[index]) {
            fail();
            return;
        }
    }

    constexpr size_t VERTEX = static_cast<size_t>(PipelineStage::Vertex);
    constexpr size_t FRAGMENT = static_cast<size_t>(PipelineStage::Fragment);

    if (!functions[VERTEX]) {
        LOG_ERROR(Render_Metal, "Graphics pipeline has no vertex function");
        fail();
        return;
    }

    if (geometry && !BuildGeometryPipelines()) {
        fail();
        return;
    }

    // Metal refuses an ordinary vertex function without a vertex descriptor.
    const bool passthrough_vertex = geometry && !geometry->xfb_only;
    Owned<MTL::VertexDescriptor> vertex_descriptor;
    if (!passthrough_vertex) {
        vertex_descriptor = BuildVertexDescriptor();
    }

    // Rasterizer discard is never baked in: Metal only allows it when the vertex
    // function returns void, so the rasterizer skips such draws instead.
    const std::optional<Shader::Info>& fragment_info = stage_infos[FRAGMENT];

    NS::Error* error = nullptr;
    {
        MTL4::RenderPipelineDescriptor* mtl4_descriptor =
            MTL4::RenderPipelineDescriptor::alloc()->init();

        mtl4_descriptor->setVertexFunctionDescriptor(
            MakeFunctionDescriptor(libraries[VERTEX].get(), shaders[VERTEX].entry_point));
        if (libraries[FRAGMENT]) {
            mtl4_descriptor->setFragmentFunctionDescriptor(
                MakeFunctionDescriptor(libraries[FRAGMENT].get(), shaders[FRAGMENT].entry_point));
        }
        if (!passthrough_vertex) {
            mtl4_descriptor->setVertexDescriptor(vertex_descriptor.get());
        }
        mtl4_descriptor->setRasterSampleCount(key.sample_count);
        mtl4_descriptor->setAlphaToCoverageState(key.alpha_to_coverage
                                                     ? MTL4::AlphaToCoverageStateEnabled
                                                     : MTL4::AlphaToCoverageStateDisabled);
        mtl4_descriptor->setAlphaToOneState(key.alpha_to_one ? MTL4::AlphaToOneStateEnabled
                                                             : MTL4::AlphaToOneStateDisabled);
        mtl4_descriptor->setInputPrimitiveTopology(
            passthrough_vertex ? MTL::PrimitiveTopologyClassTriangle : key.topology_class);

        // Metal requires the fragment function to write every attachment whose
        // write mask is enabled, and a guest depth-only pass compiles to a void
        // fragment shader, whose fragments then vanish entirely. Per attachment, an
        // output the shader never stores is masked too, see StoresColor.
        const bool writes_any_color =
            fragment_info.has_value() &&
            std::ranges::any_of(fragment_info->stores_frag_color,
                                [](bool stores) { return stores; });
        for (u32 index = 0; index < key.num_color_attachments; ++index) {
            const FixedPipelineState::ColorAttachment& attachment = key.color_attachments[index];
            if (attachment.format == MTL::PixelFormatInvalid) {
                continue;
            }
            MTL4::RenderPipelineColorAttachmentDescriptor* target =
                MTL4::RenderPipelineColorAttachmentDescriptor::alloc()->init();
            target->setPixelFormat(attachment.format);
            if (!writes_any_color && attachment.write_mask != MTL::ColorWriteMaskNone) {
                static std::atomic<u32> reported{0};
                if (reported++ < 12) {
                    LOG_WARNING(Render_Metal,
                                "Masking off every colour attachment (first is {}, fmt {}): the "
                                "fragment shader writes no colour output at all (has_info={})",
                                index, static_cast<u32>(attachment.format),
                                fragment_info.has_value());
                }
            }
            const bool stores =
                StoresColor(key, fragment_info.has_value() ? &*fragment_info : nullptr, index);
            target->setWriteMask(stores ? attachment.write_mask : MTL::ColorWriteMaskNone);
            target->setBlendingState(attachment.blend_enable ? MTL4::BlendStateEnabled
                                                             : MTL4::BlendStateDisabled);
            if (attachment.blend_enable) {
                target->setSourceRGBBlendFactor(attachment.src_rgb);
                target->setDestinationRGBBlendFactor(attachment.dst_rgb);
                target->setRgbBlendOperation(attachment.rgb_op);
                target->setSourceAlphaBlendFactor(attachment.src_alpha);
                target->setDestinationAlphaBlendFactor(attachment.dst_alpha);
                target->setAlphaBlendOperation(attachment.alpha_op);
            }
            mtl4_descriptor->colorAttachments()->setObject(target, index);
            target->release();
        }
        // Depth/stencil formats are absent on purpose, MTL4 has no setter for
        // them and the render pass establishes the pairing instead.
        pipeline_state = Adopt(device.GetMTL4Compiler()->newRenderPipelineState(
            mtl4_descriptor, device.GetMTL4LookupOptions(), &error));
        mtl4_descriptor->release();
    }

    if (!pipeline_state) {
        // Metal's message names a format but not which attachment carried it.
        std::string attachments;
        for (u32 index = 0; index < key.num_color_attachments; ++index) {
            attachments += std::format(
                " color{}={}", index, static_cast<u32>(key.color_attachments[index].format));
        }
        LOG_ERROR(Render_Metal, "Failed to build a render pipeline state: {} (samples={}{})",
                  DescribeError(error), key.sample_count, attachments);
        fail();
        return;
    }
    SignalBuilt();
}

DepthStencilCache::DepthStencilCache(const Device& device_) : device{device_} {}

DepthStencilCache::~DepthStencilCache() = default;

MTL::DepthStencilState* DepthStencilCache::Get(const DepthStencilState& state) {
    const auto it = cache.find(state);
    if (it != cache.end()) {
        return it->second.get();
    }

    ScopedAutoreleasePool pool;

    MTL::DepthStencilDescriptor* descriptor = MTL::DepthStencilDescriptor::alloc()->init();
    descriptor->setDepthCompareFunction(state.depth_compare);
    descriptor->setDepthWriteEnabled(state.depth_write);

    if (state.stencil_enable) {
        const auto make_face = [](const DepthStencilState::StencilFace& face) {
            MTL::StencilDescriptor* stencil = MTL::StencilDescriptor::alloc()->init();
            stencil->setStencilCompareFunction(face.compare);
            stencil->setStencilFailureOperation(face.fail);
            stencil->setDepthFailureOperation(face.depth_fail);
            stencil->setDepthStencilPassOperation(face.pass);
            stencil->setReadMask(face.read_mask);
            stencil->setWriteMask(face.write_mask);
            return stencil;
        };

        MTL::StencilDescriptor* front = make_face(state.front);
        MTL::StencilDescriptor* back = make_face(state.back);
        descriptor->setFrontFaceStencil(front);
        descriptor->setBackFaceStencil(back);
        front->release();
        back->release();
    }

    Owned<MTL::DepthStencilState> result =
        Adopt(device.GetDevice()->newDepthStencilState(descriptor));
    descriptor->release();

    if (!result) {
        LOG_ERROR(Render_Metal, "Failed to create a depth/stencil state");
        return nullptr;
    }

    return cache.emplace(state, std::move(result)).first->second.get();
}

} // namespace Metal
