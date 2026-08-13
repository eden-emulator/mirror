// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
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
    LsfgGenerate(const Device& device, MemoryAllocator& memory_allocator,
                 const LsfgShaders& shaders, LsfgResources& resources,
                 vk::DescriptorPool& descriptor_pool, LsfgImagePair& frames, LsfgImage& motion,
                 LsfgImage& detail1, LsfgImage& detail2, VkFormat format);

    void Dispatch(vk::CommandBuffer cmdbuf, u64 frame_count);

    [[nodiscard]] LsfgImage& Output() {
        return out_image;
    }

private:
    LsfgImagePair* frames{};
    LsfgImage* motion{};
    LsfgImage* detail1{};
    LsfgImage* detail2{};

    LsfgPass pass;
    std::array<VkDescriptorSet, 2> descriptor_sets{};
    vk::DescriptorSets owned_sets;

    LsfgImage out_image;
};

} // namespace Vulkan
