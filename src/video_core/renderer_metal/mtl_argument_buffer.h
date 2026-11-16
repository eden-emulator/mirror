// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <span>
#include <vector>

#include "common/common_funcs.h"
#include "common/common_types.h"
#include "video_core/metal_common/metal_wrapper.h"
#include "video_core/renderer_metal/mtl_encoder.h"
#include "video_core/renderer_metal/mtl_shader_util.h"
#include "video_core/renderer_metal/mtl_staging_buffer_pool.h"

namespace Metal {
class Device;
class Scheduler;

// Builds the per-draw argument buffer, replacing vk_update_descriptor and
// vk_descriptor_pool. A Tier 2 argument buffer is just an MTLBuffer of 64-bit
// handles we write ourselves, no MTLArgumentEncoder involved.
//
// Slot order here has to match what SPIRV-Cross assigned. A mismatch doesn't
// fail validation, it silently samples the wrong resource. Nothing pins the
// layout, so it's convention: the SPIR-V emitter walks one dense binding
// counter in the order uniform buffers, storage buffers, texture buffers,
// image buffers, textures, images (see spirv_emit_context.cpp) and we append
// in that same order. force_active_argument_buffer_resources keeps SPIRV-Cross
// from dropping resources it thinks are unused, and the rasterizer checks the
// slot count against ExpectedArgumentSlots. A permutation of the same count
// gets past both.
//
// Residency is not handled here. Metal 4 has no per-encoder residency and no
// automatic hazard tracking, so every allocation is declared once in the
// device's MTLResidencySet (see metal_device.cpp).
class ArgumentBufferBuilder : Common::NonCopyable, Common::NonMoveable {
public:
    explicit ArgumentBufferBuilder(const Device& device, Scheduler& scheduler,
                                   StagingBufferPool& staging_pool);
    ~ArgumentBufferBuilder();

    /// Discards anything accumulated and starts a new draw's bindings.
    void Reset();

    void AddUniformBuffer(MTL::Buffer* buffer, u32 offset, u32 size);
    void AddStorageBuffer(MTL::Buffer* buffer, u32 offset, u32 size, bool is_written);
    void AddTexture(MTL::Texture* texture, MTL::SamplerState* sampler);
    void AddImage(MTL::Texture* texture, bool is_written);

    /// Finalises the argument buffer. Returns a staging ref to bind at
    /// BindingMap::ARGUMENT_BUFFER_INDEX, or a null buffer if nothing was bound.
    [[nodiscard]] StagingBufferRef Build();

    [[nodiscard]] bool IsEmpty() const noexcept {
        return slots.empty();
    }

    /// Slots written so far. Checked against what SPIRV-Cross reported for the
    /// stage, so a layout disagreement at least shows up as a log line.
    [[nodiscard]] u32 SlotCount() const noexcept {
        return static_cast<u32>(slots.size());
    }

private:
    /// One argument buffer slot. Buffers are referenced by GPU address rather
    /// than resource ID, which is how MSL declares them inside an argument
    /// buffer struct (`device T*`).
    void PushHandle(u64 value);

    const Device& device;
    Scheduler& scheduler;
    StagingBufferPool& staging_pool;

    /// Unused: Build() requests each argument buffer from the staging pool
    /// directly rather than sub-allocating from a shared chunk.
    static constexpr size_t ARENA_SIZE = 256 * 1024;
    static constexpr size_t ARENA_ALIGNMENT = 256;

    struct Arena {
        MTL::Buffer* buffer{};
        std::span<u8> span;
        u32 base{};
        u32 used{};
        u64 tick{};
    };
    Arena arena;
    BindingMap binding_map{};

    std::vector<u64> slots;
};

} // namespace Metal
