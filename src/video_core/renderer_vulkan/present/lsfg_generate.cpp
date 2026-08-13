// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2025 lsfg-vk
// SPDX-License-Identifier: GPL-3.0-or-later

#include <vector>

#include "video_core/frame_gen/lossless_dll.h"
#include "video_core/renderer_vulkan/present/lsfg_generate.h"
#include "video_core/renderer_vulkan/present/lsfg_shaders.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/vulkan_common/vulkan_device.h"

namespace Vulkan {

namespace {

constexpr u32 DISPATCH_TILE_SHIFT = 4;

[[nodiscard]] u32 GroupCount(u32 size) {
    return (size + (1u << DISPATCH_TILE_SHIFT) - 1) >> DISPATCH_TILE_SHIFT;
}

} // Anonymous namespace

LsfgGenerate::LsfgGenerate(const Device& device, MemoryAllocator& memory_allocator,
                           const LsfgShaders& shaders, LsfgResources& resources,
                           vk::DescriptorPool& descriptor_pool, LsfgImagePair& frames_,
                           LsfgImage& motion_, LsfgImage& detail1_, LsfgImage& detail2_,
                           VkFormat format, size_t generation_count)
    : frames{&frames_}, motion{&motion_}, detail1{&detail1_}, detail2{&detail2_} {
    using namespace VideoCore::FrameGen::PerformanceShader;

    pass = LsfgPass(device, shaders, GENERATE,
                    {{1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER},
                     {2, VK_DESCRIPTOR_TYPE_SAMPLER},
                     {5, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
                     {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}});

    generations.resize(generation_count);

    const std::vector<VkDescriptorSetLayout> layouts(
        generation_count * generations[0].descriptor_sets.size(), pass.SetLayout());
    owned_sets = CreateWrappedDescriptorSets(descriptor_pool, layouts);

    const VkSampler sampler = resources.GetSampler();
    const VkSampler edge_sampler =
        resources.GetSampler(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_COMPARE_OP_ALWAYS, false);

    size_t next = 0;
    for (size_t generation = 0; generation < generation_count; ++generation) {
        Generation& target = generations[generation];
        target.out_image = LsfgImage(device, memory_allocator, (*frames)[0].Extent(), format);

        const VkBuffer buffer =
            resources.GetBuffer(LsfgTimestamp(generation, generation_count));

        for (size_t i = 0; i < target.descriptor_sets.size(); ++i) {
            target.descriptor_sets[i] = owned_sets[next++];
            LsfgDescriptorWriter(target.descriptor_sets[i])
                .AddUniformBuffer(buffer, LsfgResources::BufferSize())
                .AddSampler(sampler)
                .AddSampler(edge_sampler)
                .AddSampledImage((*frames)[1 - i])
                .AddSampledImage((*frames)[i])
                .AddSampledImage(*motion)
                .AddSampledImage(*detail1)
                .AddSampledImage(*detail2)
                .AddStorageImage(target.out_image)
                .Build(device);
        }
    }
}

void LsfgGenerate::Dispatch(vk::CommandBuffer cmdbuf, u64 frame_count, size_t generation) {
    Generation& target = generations[generation];
    const VkExtent2D extent = target.out_image.Extent();

    LsfgBarriers(cmdbuf)
        .WriteToReadAll(*frames)
        .WriteToRead(*motion)
        .WriteToRead(*detail1)
        .WriteToRead(*detail2)
        .ReadToWrite(target.out_image)
        .Build();

    pass.Bind(cmdbuf, target.descriptor_sets[frame_count % target.descriptor_sets.size()]);
    cmdbuf.Dispatch(GroupCount(extent.width), GroupCount(extent.height), 1);
}

} // namespace Vulkan
