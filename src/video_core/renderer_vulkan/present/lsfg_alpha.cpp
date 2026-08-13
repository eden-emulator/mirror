// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <vector>

#include "video_core/frame_gen/lossless_dll.h"
#include "video_core/renderer_vulkan/present/lsfg_alpha.h"
#include "video_core/renderer_vulkan/present/lsfg_shaders.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/vulkan_common/vulkan_device.h"

namespace Vulkan {

namespace {

constexpr u32 DISPATCH_TILE_SHIFT = 3;

[[nodiscard]] u32 GroupCount(u32 size) {
    return (size + (1u << DISPATCH_TILE_SHIFT) - 1) >> DISPATCH_TILE_SHIFT;
}

[[nodiscard]] VkExtent2D HalveExtent(VkExtent2D extent) {
    return VkExtent2D{
        .width = (extent.width + 1) >> 1,
        .height = (extent.height + 1) >> 1,
    };
}

} // Anonymous namespace

LsfgAlpha::LsfgAlpha(const Device& device, MemoryAllocator& memory_allocator,
                     const LsfgShaders& shaders, vk::DescriptorPool& descriptor_pool,
                     vk::Sampler& sampler, const LsfgImage& input_)
    : input{&input_} {
    using namespace VideoCore::FrameGen::PerformanceShader;

    passes[0] = LsfgPass(device, shaders, ALPHA[0],
                         {{1, VK_DESCRIPTOR_TYPE_SAMPLER},
                          {1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
                          {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}});
    passes[1] = LsfgPass(device, shaders, ALPHA[1],
                         {{1, VK_DESCRIPTOR_TYPE_SAMPLER},
                          {1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
                          {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}});
    passes[2] = LsfgPass(device, shaders, ALPHA[2],
                         {{1, VK_DESCRIPTOR_TYPE_SAMPLER},
                          {1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
                          {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}});
    passes[3] = LsfgPass(device, shaders, ALPHA[3],
                         {{1, VK_DESCRIPTOR_TYPE_SAMPLER},
                          {2, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
                          {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}});

    const VkExtent2D half_extent = HalveExtent(input->Extent());
    const VkExtent2D quarter_extent = HalveExtent(half_extent);

    temp1 = LsfgImage(device, memory_allocator, half_extent);
    temp2 = LsfgImage(device, memory_allocator, half_extent);
    for (size_t i = 0; i < LSFG_ALPHA_OUTPUTS; ++i) {
        temp3[i] = LsfgImage(device, memory_allocator, quarter_extent);
        for (size_t j = 0; j < LSFG_ALPHA_HISTORY; ++j) {
            out_images[j][i] = LsfgImage(device, memory_allocator, quarter_extent);
        }
    }

    std::vector<VkDescriptorSetLayout> layouts;
    for (size_t i = 0; i < LSFG_ALPHA_STAGES - 1; ++i) {
        layouts.push_back(passes[i].SetLayout());
    }
    for (size_t i = 0; i < LSFG_ALPHA_HISTORY; ++i) {
        layouts.push_back(passes[3].SetLayout());
    }
    owned_sets = CreateWrappedDescriptorSets(descriptor_pool, layouts);

    for (size_t i = 0; i < LSFG_ALPHA_STAGES - 1; ++i) {
        descriptor_sets[i] = owned_sets[i];
    }
    for (size_t i = 0; i < LSFG_ALPHA_HISTORY; ++i) {
        last_descriptor_sets[i] = owned_sets[LSFG_ALPHA_STAGES - 1 + i];
    }

    LsfgDescriptorWriter(descriptor_sets[0])
        .AddSampler(*sampler)
        .AddSampledImage(*input)
        .AddStorageImage(temp1)
        .Build(device);
    LsfgDescriptorWriter(descriptor_sets[1])
        .AddSampler(*sampler)
        .AddSampledImage(temp1)
        .AddStorageImage(temp2)
        .Build(device);
    LsfgDescriptorWriter(descriptor_sets[2])
        .AddSampler(*sampler)
        .AddSampledImage(temp2)
        .AddStorageImages(temp3)
        .Build(device);
    for (size_t i = 0; i < LSFG_ALPHA_HISTORY; ++i) {
        LsfgDescriptorWriter(last_descriptor_sets[i])
            .AddSampler(*sampler)
            .AddSampledImages(temp3)
            .AddStorageImages(out_images[i])
            .Build(device);
    }
}

void LsfgAlpha::Dispatch(vk::CommandBuffer cmdbuf, u64 frame_count) {
    const VkExtent2D half_extent = temp1.Extent();
    u32 groups_x = GroupCount(half_extent.width);
    u32 groups_y = GroupCount(half_extent.height);

    LsfgBarriers(cmdbuf).ReadToWrite(temp1).Build();
    passes[0].Bind(cmdbuf, descriptor_sets[0]);
    cmdbuf.Dispatch(groups_x, groups_y, 1);

    LsfgBarriers(cmdbuf).WriteToRead(temp1).ReadToWrite(temp2).Build();
    passes[1].Bind(cmdbuf, descriptor_sets[1]);
    cmdbuf.Dispatch(groups_x, groups_y, 1);

    const VkExtent2D quarter_extent = temp3[0].Extent();
    groups_x = GroupCount(quarter_extent.width);
    groups_y = GroupCount(quarter_extent.height);

    LsfgBarriers(cmdbuf).WriteToRead(temp2).ReadToWriteAll(temp3).Build();
    passes[2].Bind(cmdbuf, descriptor_sets[2]);
    cmdbuf.Dispatch(groups_x, groups_y, 1);

    const size_t slot = frame_count % LSFG_ALPHA_HISTORY;
    LsfgBarriers(cmdbuf).WriteToReadAll(temp3).ReadToWriteAll(out_images[slot]).Build();
    passes[3].Bind(cmdbuf, last_descriptor_sets[slot]);
    cmdbuf.Dispatch(groups_x, groups_y, 1);
}

} // namespace Vulkan
