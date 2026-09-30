// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <optional>

#include "common/common_types.h"
#include "video_core/renderer_vulkan/present/frame_gen_pacer.h"
#include "video_core/renderer_vulkan/present/lsfg_chain.h"
#include "video_core/renderer_vulkan/present/lsfg_shaders.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"

namespace Vulkan {

class Device;
class Scheduler;

class FrameGen {
public:
    explicit FrameGen(MemoryAllocator& memory_allocator, Scheduler& scheduler);
    ~FrameGen();

    void Process(const Device& device, VkImageView source, VkExtent2D extent);

    [[nodiscard]] size_t WantedGenerations(size_t capacity);

    [[nodiscard]] size_t GeneratedFrameCount() const;

    [[nodiscard]] const LsfgImage& Generate(const Device& device, size_t generation);

private:
    static constexpr size_t INPUT_SLOTS = 4;

    void CreateInputPass(const Device& device);
    void Rebuild(const Device& device, VkExtent2D extent, f32 flow_scale);
    void DumpDebugImages(u64 count);

    MemoryAllocator& memory_allocator;
    Scheduler& scheduler;

    std::optional<LsfgShaders> shaders;
    std::optional<LsfgChain> chain;
    std::array<LsfgImage, LSFG_MAX_GENERATIONS> outputs;
    vk::DescriptorSetLayout input_set_layout;
    vk::PipelineLayout input_layout;
    vk::DescriptorPool input_pool;
    vk::DescriptorSets input_sets;
    vk::ShaderModule input_vertex_shader;
    vk::ShaderModule input_fragment_shader;
    vk::Pipeline input_pipeline;
    vk::Sampler input_sampler;
    std::array<u64, INPUT_SLOTS> input_ticks{};
    FrameGenPacer pacer;
    FrameGenPlan plan{};
    VkExtent2D built_extent{};
    f32 built_flow_scale{};
    u64 frame_count{};
    u64 last_count{};
    size_t last_generations{};
    u32 warm_streak{};
    bool generated{};
    bool unavailable{};
    bool dumped{};
};

} // namespace Vulkan
