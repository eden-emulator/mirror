// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>

#include "common/common_types.h"
#include "video_core/renderer_vulkan/present/lsfg_mipmaps.h"
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

    void Process(const Device& device, Frame* frame);

private:
    void Rebuild(const Device& device, VkExtent2D extent);
    void DumpFlowPyramid(const Device& device);

    MemoryAllocator& memory_allocator;
    Scheduler& scheduler;

    std::optional<LsfgShaders> shaders;
    std::optional<LsfgMipmaps> mipmaps;
    VkExtent2D built_extent{};
    u64 frame_count{};
    bool unavailable{};
    bool dumped{};
};

} // namespace Vulkan
