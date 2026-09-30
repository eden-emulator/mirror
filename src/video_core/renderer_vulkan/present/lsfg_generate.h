// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2025 lsfg-vk
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>

#include "common/common_types.h"
#include "video_core/renderer_vulkan/present/lsfg_common.h"

namespace Vulkan {

class Device;
class LsfgShaders;

class LsfgGenerate {
public:
    LsfgGenerate() = default;
    LsfgGenerate(const Device& device, const LsfgShaders& shaders, LsfgResources& resources,
                 vk::DescriptorPool& descriptor_pool, LsfgImagePair& frames, LsfgImage& motion,
                 LsfgImage& detail1, LsfgImage& detail2);

    void SetTarget(const Device& device, size_t slot, VkImageView view);

    void Dispatch(vk::CommandBuffer cmdbuf, u64 frame_count, size_t slot, VkImage image,
                  VkExtent2D extent);

private:
    struct Generation {
        std::array<VkDescriptorSet, 2> descriptor_sets{};
        VkImageView view{};
        VkBuffer buffer{};
    };

    LsfgImagePair* frames{};
    LsfgImage* motion{};
    LsfgImage* detail1{};
    LsfgImage* detail2{};
    VkSampler sampler{};
    VkSampler edge_sampler{};

    LsfgPass pass;
    std::array<Generation, LSFG_GENERATION_SLOTS> generations{};
    vk::DescriptorSets owned_sets;
};

} // namespace Vulkan
