// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include "video_core/renderer_vulkan/present/lsfg_chain.h"
#include "video_core/renderer_vulkan/present/lsfg_shaders.h"
#include "video_core/vulkan_common/vulkan_device.h"

namespace Vulkan {

namespace {

constexpr u32 DESCRIPTOR_SET_COUNT = 256;
constexpr size_t FIRST_DELTA_LEVEL = 4;

} // Anonymous namespace

LsfgChain::LsfgChain(const Device& device, MemoryAllocator& memory_allocator,
                     const LsfgShaders& shaders, VkExtent2D extent, VkFormat format,
                     f32 flow_scale)
    : resources{device, memory_allocator, flow_scale},
      descriptor_pool{CreateLsfgDescriptorPool(device, DESCRIPTOR_SET_COUNT)} {
    for (auto& image : frames) {
        image = LsfgImage(device, memory_allocator, extent, format);
    }

    mipmaps = LsfgMipmaps(device, memory_allocator, shaders, resources, descriptor_pool, frames,
                          flow_scale);

    for (size_t i = 0; i < LSFG_MIP_LEVELS; ++i) {
        alpha[i] = LsfgAlpha(device, memory_allocator, shaders, resources, descriptor_pool,
                             mipmaps.Output(i));
    }

    beta = LsfgBeta(device, memory_allocator, shaders, resources, descriptor_pool,
                    alpha[0].Outputs());

    for (size_t i = 0; i < LSFG_MIP_LEVELS; ++i) {
        const size_t level = LSFG_MIP_LEVELS - 1 - i;
        gamma[i] = LsfgGamma(device, memory_allocator, shaders, resources, descriptor_pool,
                             alpha[level].Outputs(),
                             beta.Output(std::min(level, LSFG_BETA_OUTPUTS - 1)),
                             i == 0 ? nullptr : &gamma[i - 1].Output());

        if (i < FIRST_DELTA_LEVEL) {
            continue;
        }

        const size_t index = i - FIRST_DELTA_LEVEL;
        delta[index] = LsfgDelta(
            device, memory_allocator, shaders, resources, descriptor_pool, alpha[level].Outputs(),
            beta.Output(level), i == FIRST_DELTA_LEVEL ? nullptr : &gamma[i - 1].Output(),
            i == FIRST_DELTA_LEVEL ? nullptr : &delta[index - 1].Output1(),
            i == FIRST_DELTA_LEVEL ? nullptr : &delta[index - 1].Output2());
    }

    generate = LsfgGenerate(device, memory_allocator, shaders, resources, descriptor_pool, frames,
                            gamma[LSFG_MIP_LEVELS - 1].Output(),
                            delta[LSFG_DELTA_INSTANCES - 1].Output1(),
                            delta[LSFG_DELTA_INSTANCES - 1].Output2(), format);
}

void LsfgChain::Dispatch(vk::CommandBuffer cmdbuf, u64 frame_count) {
    mipmaps.Dispatch(cmdbuf, frame_count);

    for (size_t i = 0; i < LSFG_MIP_LEVELS; ++i) {
        alpha[LSFG_MIP_LEVELS - 1 - i].Dispatch(cmdbuf, frame_count);
    }

    beta.Dispatch(cmdbuf, frame_count);

    for (size_t i = 0; i < LSFG_MIP_LEVELS; ++i) {
        gamma[i].Dispatch(cmdbuf, frame_count);
        if (i >= FIRST_DELTA_LEVEL) {
            delta[i - FIRST_DELTA_LEVEL].Dispatch(cmdbuf, frame_count);
        }
    }

    generate.Dispatch(cmdbuf, frame_count);
}

} // namespace Vulkan
