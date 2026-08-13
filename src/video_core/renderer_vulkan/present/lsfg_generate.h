// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2025 lsfg-vk
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <vector>

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
                 LsfgImage& detail1, LsfgImage& detail2, VkFormat format,
                 size_t generation_count);

    void Dispatch(vk::CommandBuffer cmdbuf, u64 frame_count, size_t generation);

    [[nodiscard]] LsfgImage& Output(size_t generation) {
        return generations[generation].out_image;
    }

private:
    struct Generation {
        std::array<VkDescriptorSet, 2> descriptor_sets{};
        LsfgImage out_image;
    };

    LsfgImagePair* frames{};
    LsfgImage* motion{};
    LsfgImage* detail1{};
    LsfgImage* detail2{};

    LsfgPass pass;
    std::vector<Generation> generations;
    vk::DescriptorSets owned_sets;
};

} // namespace Vulkan
