// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
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

    void Process(const Device& device, Frame* frame, VkFormat format, VkExtent2D guest_extent,
                 bool generate);

    [[nodiscard]] size_t WantedGenerations();

    [[nodiscard]] size_t GeneratedFrameCount() const;

    void GenerateInto(const Device& device, Frame* destination, size_t generation);

private:
    using Clock = std::chrono::steady_clock;

    void Rebuild(const Device& device, VkExtent2D extent, VkFormat format, f32 flow_scale);
    void UpdateBaseRate();
    void UpdateGenerationCount();
    [[nodiscard]] size_t DesiredGenerations() const;
    void DumpDebugImages(u64 count);

    MemoryAllocator& memory_allocator;
    Scheduler& scheduler;

    std::optional<LsfgShaders> shaders;
    std::optional<LsfgChain> chain;
    VkExtent2D peak_guest_extent{};
    VkExtent2D built_extent{};
    VkFormat built_format{VK_FORMAT_UNDEFINED};
    f32 built_flow_scale{};
    u64 frame_count{};
    u64 last_count{};
    size_t last_generations{};

    Clock::time_point last_process_time{};
    f32 smoothed_base_rate{};
    size_t stable_generations{};
    size_t pending_generations{};
    u32 pending_generation_votes{};
    bool generated{};
    bool unavailable{};
    bool dumped{};
};

} // namespace Vulkan
