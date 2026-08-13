// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>

#include "common/common_types.h"
#include "video_core/renderer_vulkan/present/lsfg_common.h"

namespace Vulkan {

class Device;
class LsfgShaders;
class Scheduler;

constexpr size_t LSFG_ALPHA_STAGES = 4;
constexpr size_t LSFG_ALPHA_HISTORY = 3;
constexpr size_t LSFG_ALPHA_OUTPUTS = 2;

using LsfgAlphaOutputs = std::array<LsfgImage, LSFG_ALPHA_OUTPUTS>;

class LsfgAlpha {
public:
    LsfgAlpha() = default;
    LsfgAlpha(const Device& device, MemoryAllocator& memory_allocator, const LsfgShaders& shaders,
              vk::DescriptorPool& descriptor_pool, vk::Sampler& sampler, const LsfgImage& input);

    void Dispatch(vk::CommandBuffer cmdbuf, u64 frame_count);

    [[nodiscard]] const LsfgAlphaOutputs& GetOutputs(u64 frame_count) const {
        return out_images[frame_count % LSFG_ALPHA_HISTORY];
    }

    [[nodiscard]] const std::array<LsfgAlphaOutputs, LSFG_ALPHA_HISTORY>& GetAllOutputs() const {
        return out_images;
    }

private:
    const LsfgImage* input{};

    std::array<LsfgPass, LSFG_ALPHA_STAGES> passes;
    std::array<VkDescriptorSet, LSFG_ALPHA_STAGES - 1> descriptor_sets{};
    std::array<VkDescriptorSet, LSFG_ALPHA_HISTORY> last_descriptor_sets{};
    vk::DescriptorSets owned_sets;

    LsfgImage temp1;
    LsfgImage temp2;
    std::array<LsfgImage, LSFG_ALPHA_OUTPUTS> temp3;
    std::array<LsfgAlphaOutputs, LSFG_ALPHA_HISTORY> out_images;
};

} // namespace Vulkan
