// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cstring>

#include "common/logging.h"
#include <boost/container/small_vector.hpp>

#include "common/div_ceil.h"
#include "video_core/metal_common/metal_device.h"
#include "video_core/renderer_metal/mtl_scheduler.h"
#include "video_core/renderer_metal/mtl_shader_util.h"
#include "video_core/renderer_metal/mtl_transform_feedback.h"

namespace Metal {

namespace {

constexpr u32 SOURCE_INDEX = 0;
constexpr u32 DESTINATION_INDEX = 1;
constexpr u32 PARAMS_INDEX = 2;
constexpr u32 THREADGROUP_SIZE = 64;

/// One varying to move, resolved to plain byte arithmetic.
struct GatherEntry {
    u32 src_offset;  ///< Byte offset within a captured vertex.
    u32 dst_offset;
    u32 dst_stride;
    u32 components;
};

struct Params {
    u32 capture_stride;
    u32 num_vertices;
    u32 num_entries;
    /// Bytes addressable in the destination and the capture, from guest state.
    u32 dst_limit;
    u32 src_limit;
    u32 padding;
};

/// Gathers one varying per thread-y, one vertex per thread-x.
constexpr const char* GATHER_MSL = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct GatherEntry {
    uint src_offset;
    uint dst_offset;
    uint dst_stride;
    uint components;
};

struct Params {
    uint capture_stride;
    uint num_vertices;
    uint num_entries;
    uint dst_limit;
    uint src_limit;
    uint padding;
};

kernel void gather_varyings(device const float* capture [[buffer(0)]],
                            device float* destination [[buffer(1)]],
                            constant Params& p [[buffer(2)]],
                            constant GatherEntry* entries [[buffer(3)]],
                            uint2 id [[thread_position_in_grid]]) {
    if (id.x >= p.num_vertices || id.y >= p.num_entries) {
        return;
    }
    const GatherEntry entry = entries[id.y];
    const uint src = (id.x * p.capture_stride + entry.src_offset) / 4u;
    const uint dst = (id.x * entry.dst_stride + entry.dst_offset) / 4u;
    // Both ends are guest-described, so both are bounds checked: an overrun
    // corrupts whatever buffer follows and eventually faults the GPU.
    for (uint c = 0u; c < entry.components; ++c) {
        if ((dst + c + 1u) * 4u > p.dst_limit || (src + c + 1u) * 4u > p.src_limit) {
            break;
        }
        destination[dst + c] = capture[src + c];
    }
}
)MSL";

/// Attribute word of gl_Position, and of the first generic output.
constexpr u32 POSITION_WORD = 28;
constexpr u32 GENERIC_BASE_WORD = 32;

} // Anonymous namespace

TransformFeedback::TransformFeedback(const Device& device_, Scheduler& scheduler_,
                                     MemoryAllocator& memory_allocator_)
    : device{device_}, scheduler{scheduler_}, memory_allocator{memory_allocator_} {
    ScopedAutoreleasePool pool;

    NS::Error* error = nullptr;
    MTL::CompileOptions* options = MTL::CompileOptions::alloc()->init();
    library = Adopt(device.GetDevice()->newLibrary(ToNSString(GATHER_MSL), options, &error));
    options->release();
    if (!library) {
        LOG_ERROR(Render_Metal, "Failed to compile the transform feedback gather: {}",
                  DescribeError(error));
        return;
    }
    library->setLabel(ToNSString("eden transform feedback"));

    gather_pipeline = BuildComputePipeline(device, library.get(), "gather_varyings",
                                           "eden transform feedback gather");
}

TransformFeedback::~TransformFeedback() = default;

Owned<MTL::Buffer> TransformFeedback::CreateCaptureSegment(u32 bytes) {
    if (bytes == 0) {
        return {};
    }
    Owned<MTL::Buffer> buffer = memory_allocator.CreateBuffer(bytes, MemoryUsage::DeviceLocal);
    if (buffer) {
        buffer->setLabel(ToNSString("eden xfb capture segment"));
    } else {
        LOG_WARNING(Render_Metal, "Could not allocate a {} byte capture segment", bytes);
    }
    return buffer;
}

MTL::Buffer* TransformFeedback::AcquireCaptureSegment(u32 bytes) {
    if (bytes == 0) {
        return nullptr;
    }
    // Bucketed, so captures that vary by a few vertices reuse one segment.
    u32 capacity = 4096;
    while (capacity < bytes) {
        capacity *= 2;
    }

    const MasterSemaphore& semaphore = scheduler.GetMasterSemaphore();
    for (PooledSegment& segment : segment_pool) {
        if (segment.held || segment.capacity < capacity ||
            !semaphore.IsFree(segment.free_after)) {
            continue;
        }
        segment.held = true;
        // A capture only writes the varying slots its shader outputs, and a stale
        // float in a slot the gather consumes is a vertex flung to infinity. On
        // the CPU because a GPU fill was left undone by encoder ordering.
        if (void* const contents = segment.buffer->contents()) {
            std::memset(contents, 0, segment.capacity);
        }
        return segment.buffer.get();
    }

    Owned<MTL::Buffer> buffer =
        memory_allocator.CreateBuffer(capacity, MemoryUsage::DeviceLocal);
    if (!buffer) {
        LOG_WARNING(Render_Metal, "Could not allocate a {} byte capture segment", capacity);
        return nullptr;
    }
    buffer->setLabel(ToNSString("eden xfb capture segment"));

    MTL::Buffer* const handle = buffer.get();
    segment_pool.push_back(PooledSegment{
        .buffer = std::move(buffer),
        .capacity = capacity,
        .held = true,
    });
    if (segment_pool.size() % 64 == 0) {
        LOG_INFO(Render_Metal, "Transform feedback capture pool grew to {} segments",
                 segment_pool.size());
    }
    return handle;
}

void TransformFeedback::ReleaseCaptureSegment(MTL::Buffer* segment) {
    if (segment == nullptr) {
        return;
    }
    for (PooledSegment& pooled : segment_pool) {
        if (pooled.buffer.get() == segment) {
            // NOT stamped with a tick here, on purpose. Captured vertices are
            // read by draws recorded after the capture, so the current tick is no
            // upper bound on the work referencing this segment and neither is a
            // fixed margin on it. Reuse it too early and a later draw reads a
            // segment the GPU is overwriting: a mesh exploding across the screen.
            pooled.held = false;
            pooled.free_after = UNSTAMPED;
            return;
        }
    }
}

void TransformFeedback::StampReleasedSegments() {
    // Nothing more will be recorded against these, so the next tick bounds them.
    const u64 tick = scheduler.GetMasterSemaphore().CurrentTick();
    for (PooledSegment& pooled : segment_pool) {
        if (!pooled.held && pooled.free_after == UNSTAMPED) {
            pooled.free_after = tick;
        }
    }
}

MTL::Buffer* TransformFeedback::CaptureBuffer(u32 bytes) {
    if (bytes == 0) {
        return nullptr;
    }
    if (capture_buffer && capture_capacity >= bytes) {
        return capture_buffer.get();
    }
    const u32 capacity = std::max(bytes, capture_capacity * 2);
    capture_buffer = memory_allocator.CreateBuffer(capacity, MemoryUsage::DeviceLocal);
    if (!capture_buffer) {
        LOG_WARNING(Render_Metal, "Could not allocate a {} byte transform feedback capture",
                    capacity);
        capture_capacity = 0;
        return nullptr;
    }
    capture_buffer->setLabel(ToNSString("eden xfb capture"));
    capture_capacity = capacity;
    return capture_buffer.get();
}

void TransformFeedback::Gather(MTL::Buffer* capture, u32 capture_stride, u32 num_vertices,
                               std::span<const Shader::TransformFeedbackVarying> varyings,
                               std::span<const TransformFeedbackBinding> bindings,
                               std::span<const s8> slot_of_generic, s32 position_slot) {
    if (!gather_pipeline || capture == nullptr || num_vertices == 0 || capture_stride == 0) {
        return;
    }

    // Byte offset of attribute word @p word inside a captured vertex, or -1.
    // SPIRV-Cross compacts stored generics and puts position last.
    const auto capture_offset_of = [&](u32 word) -> s32 {
        if (word >= POSITION_WORD && word < POSITION_WORD + 4) {
            return position_slot < 0
                       ? -1
                       : static_cast<s32>(position_slot * 16 + (word - POSITION_WORD) * 4);
        }
        if (word < GENERIC_BASE_WORD) {
            return -1;
        }
        const u32 generic = (word - GENERIC_BASE_WORD) / 4;
        const u32 component = (word - GENERIC_BASE_WORD) % 4;
        if (generic >= slot_of_generic.size() || slot_of_generic[generic] < 0) {
            return -1;
        }
        return static_cast<s32>(slot_of_generic[generic] * 16 + component * 4);
    };

    // The guest describes its capture as a sparse table indexed by attribute
    // word. One dispatch per destination buffer, of which there can be several.
    MTL::ComputePipelineState* const state = gather_pipeline.get();
    const u32 groups = Common::DivCeil(num_vertices, THREADGROUP_SIZE);

    for (size_t buffer_index = 0; buffer_index < bindings.size(); ++buffer_index) {
        if (bindings[buffer_index].buffer == nullptr) {
            continue;
        }
        boost::container::small_vector<GatherEntry, 32> entries;
        for (u32 word = 0; word < varyings.size(); ++word) {
            const Shader::TransformFeedbackVarying& varying = varyings[word];
            if (varying.components == 0 || varying.buffer != buffer_index) {
                continue;
            }
            const s32 src = capture_offset_of(word);
            if (src < 0) {
                continue;
            }
            entries.push_back(GatherEntry{
                .src_offset = static_cast<u32>(src),
                .dst_offset = varying.offset,
                .dst_stride = varying.stride,
                .components = varying.components,
            });
        }
        if (entries.empty()) {
            continue;
        }

        const Params params{
            .capture_stride = capture_stride,
            .num_vertices = num_vertices,
            .num_entries = static_cast<u32>(entries.size()),
            .dst_limit = bindings[buffer_index].size,
            .src_limit = capture_stride * num_vertices,
            .padding = 0,
        };
        MTL::Buffer* const destination = bindings[buffer_index].buffer;
        const u32 destination_offset = bindings[buffer_index].offset;

        // A later draw can regrow the capture buffer and free the old allocation
        // before the worker runs this lambda.
        capture->retain();
        destination->retain();
        scheduler.Record([state, capture, destination, destination_offset, params, groups,
                          entries = std::move(entries)](CommandContext& context) {
            ComputeEncoder encoder = context.RequestCompute();
            if (encoder == nullptr) {
                capture->release();
                destination->release();
                return;
            }
            encoder->setComputePipelineState(state);
            encoder->setBuffer(capture, 0, SOURCE_INDEX);
            encoder->setBuffer(destination, destination_offset, DESTINATION_INDEX);
            encoder->setBytes(&params, sizeof(params), PARAMS_INDEX);
            encoder->setBytes(entries.data(), entries.size() * sizeof(GatherEntry), 3);
            encoder->dispatchThreadgroups(MTL::Size::Make(groups, params.num_entries, 1),
                                          MTL::Size::Make(THREADGROUP_SIZE, 1, 1));
            capture->release();
            destination->release();
        });
    }
}

} // namespace Metal
