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

constexpr size_t LSFG_ALPHA_STAGES = 4;

class LsfgAlpha {
public:
    LsfgAlpha() = default;
    LsfgAlpha(const Device& device, MemoryAllocator& memory_allocator, const LsfgShaders& shaders,
              LsfgResources& resources, vk::DescriptorPool& descriptor_pool, LsfgImage& input);

    void Dispatch(vk::CommandBuffer cmdbuf, u64 frame_count);

    [[nodiscard]] LsfgImageHistory& Outputs() {
        return out_images;
    }

private:
    LsfgImage* input{};

    std::array<LsfgPass, LSFG_ALPHA_STAGES> passes;
    std::array<VkDescriptorSet, LSFG_ALPHA_STAGES - 1> descriptor_sets{};
    std::array<VkDescriptorSet, LSFG_HISTORY_SLOTS> last_descriptor_sets{};
    vk::DescriptorSets owned_sets;

    LsfgImage temp1;
    LsfgImage temp2;
    LsfgImagePair temp3;
    LsfgImageHistory out_images;
};

} // namespace Vulkan
