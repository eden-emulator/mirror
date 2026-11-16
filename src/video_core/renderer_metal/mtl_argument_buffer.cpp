// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <atomic>
#include <cstring>

#include "common/alignment.h"
#include "common/logging.h"
#include "video_core/metal_common/metal_device.h"
#include "video_core/renderer_metal/mtl_argument_buffer.h"
#include "video_core/renderer_metal/mtl_scheduler.h"

namespace Metal {
ArgumentBufferBuilder::ArgumentBufferBuilder(const Device& device_, Scheduler& scheduler_,
                                             StagingBufferPool& staging_pool_)
    : device{device_}, scheduler{scheduler_}, staging_pool{staging_pool_} {
    // Avoid reallocating on the per-draw hot path.
    slots.reserve(64);
}

ArgumentBufferBuilder::~ArgumentBufferBuilder() = default;

void ArgumentBufferBuilder::Reset() {
    slots.clear();
}

void ArgumentBufferBuilder::PushHandle(u64 value) {
    slots.push_back(value);
}

void ArgumentBufferBuilder::AddUniformBuffer(MTL::Buffer* buffer, u32 offset, u32 size) {
    if (buffer == nullptr) {
        // A null slot must still occupy its index, or every later binding
        // shifts and the shader reads the wrong resource.
        PushHandle(0);
        return;
    }
    PushHandle(buffer->gpuAddress() + offset);
}

void ArgumentBufferBuilder::AddStorageBuffer(MTL::Buffer* buffer, u32 offset, u32 size,
                                             bool is_written) {
    if (buffer == nullptr) {
        PushHandle(0);
        return;
    }
    PushHandle(buffer->gpuAddress() + offset);
}

void ArgumentBufferBuilder::AddTexture(MTL::Texture* texture, MTL::SamplerState* sampler) {
    // A combined image sampler takes two consecutive slots, texture then
    // sampler. See the layout contract in the header.
    if (texture == nullptr) {
        PushHandle(0);
        PushHandle(0);
        return;
    }
    PushHandle(texture->gpuResourceID()._impl);

    PushHandle(sampler != nullptr ? sampler->gpuResourceID()._impl : 0);
    // Sampler states are not MTLResources and need no residency declaration.
}

void ArgumentBufferBuilder::AddImage(MTL::Texture* texture, bool is_written) {
    if (texture == nullptr) {
        PushHandle(0);
        return;
    }
    PushHandle(texture->gpuResourceID()._impl);
}

StagingBufferRef ArgumentBufferBuilder::Build() {
    if (slots.empty()) {
        return StagingBufferRef{};
    }

    // One staging request per stage per draw. Requests this small go through
    // StagingBufferPool's stream buffer bump allocator, so sharing a chunk
    // between the two stages would only save us a pointer bump.
    const size_t size_bytes = slots.size() * sizeof(u64);
    StagingBufferRef ref = staging_pool.Request(size_bytes, MemoryUsage::Upload);
    if (ref.mapped_span.size() < size_bytes) {
        LOG_ERROR(Render_Metal, "Argument buffer staging allocation is too small");
        return StagingBufferRef{};
    }

    std::memcpy(ref.mapped_span.data(), slots.data(), size_bytes);
    MemoryAllocator::FlushRange(ref.buffer, ref.offset, size_bytes);
    return ref;
}

} // namespace Metal
