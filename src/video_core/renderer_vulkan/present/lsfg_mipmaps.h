// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>

#include "common/common_types.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

class Device;
class LsfgShaders;
class Scheduler;

constexpr size_t LSFG_MIP_LEVELS = 7;

class LsfgMipmaps {
public:
    explicit LsfgMipmaps(const Device& device, MemoryAllocator& memory_allocator,
                         const LsfgShaders& shaders, VkExtent2D input_extent, f32 flow_scale);

    void Dispatch(const Device& device, Scheduler& scheduler, VkImageView current_view,
                  u64 frame_count);

    [[nodiscard]] VkImageView GetLevelView(size_t level) const {
        return *image_views[level];
    }

    [[nodiscard]] VkImage GetLevelImage(size_t level) const {
        return *images[level];
    }

    [[nodiscard]] VkExtent2D GetLevelExtent(size_t level) const;

private:
    void CreateImages(const Device& device);
    void CreateUniformBuffer(f32 flow_scale);

    MemoryAllocator& memory_allocator;
    VkExtent2D flow_extent{};

    vk::Buffer uniform_buffer;
    vk::Sampler sampler;
    vk::DescriptorPool descriptor_pool;
    vk::DescriptorSetLayout descriptor_set_layout;
    vk::DescriptorSets descriptor_sets;
    vk::PipelineLayout pipeline_layout;
    vk::Pipeline pipeline;

    std::array<vk::Image, LSFG_MIP_LEVELS> images;
    std::array<vk::ImageView, LSFG_MIP_LEVELS> image_views;
};

} // namespace Vulkan
