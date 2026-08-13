// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>

#include "common/common_types.h"
#include "video_core/renderer_vulkan/present/lsfg_chain.h"
#include "video_core/renderer_vulkan/present/lsfg_shaders.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"

namespace Vulkan {

class Device;
class Scheduler;
struct Frame;

class FrameGen {
public:
    explicit FrameGen(MemoryAllocator& memory_allocator, Scheduler& scheduler);
    ~FrameGen();

    void Process(const Device& device, Frame* frame, VkFormat format);

    [[nodiscard]] size_t GeneratedFrameCount() const;

    void CopyToFrame(Frame* destination, size_t generation);

private:
    void Rebuild(const Device& device, VkExtent2D extent, VkFormat format);
    void DumpDebugImages(u64 count);

    MemoryAllocator& memory_allocator;
    Scheduler& scheduler;

    std::optional<LsfgShaders> shaders;
    std::optional<LsfgChain> chain;
    VkExtent2D built_extent{};
    VkFormat built_format{VK_FORMAT_UNDEFINED};
    f32 built_flow_scale{};
    bool built_hdr{};
    size_t built_generations{};
    u64 frame_count{};
    bool unavailable{};
    bool dumped{};
};

} // namespace Vulkan
