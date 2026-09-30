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

VkImageMemoryBarrier2 MakeTargetBarrier(VkImage image, VkPipelineStageFlags2 src_stage,
                                        VkAccessFlags2 src_access, VkPipelineStageFlags2 dst_stage,
                                        VkAccessFlags2 dst_access, VkImageLayout old_layout) {
    return VkImageMemoryBarrier2{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .pNext = nullptr,
        .srcStageMask = src_stage,
        .srcAccessMask = src_access,
        .dstStageMask = dst_stage,
        .dstAccessMask = dst_access,
        .oldLayout = old_layout,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange{
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
}

} // Anonymous namespace

LsfgGenerate::LsfgGenerate(const Device& device, const LsfgShaders& shaders,
                           LsfgResources& resources, vk::DescriptorPool& descriptor_pool,
                           LsfgImagePair& frames_, LsfgImage& motion_, LsfgImage& detail1_,
                           LsfgImage& detail2_)
    : frames{&frames_}, motion{&motion_}, detail1{&detail1_}, detail2{&detail2_} {
    using namespace VideoCore::FrameGen::PerformanceShader;

    pass = LsfgPass(device, shaders, GENERATE,
                    {{1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER},
                     {2, VK_DESCRIPTOR_TYPE_SAMPLER},
                     {5, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
                     {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}});

    sampler = resources.GetSampler();
    edge_sampler =
        resources.GetSampler(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_COMPARE_OP_ALWAYS, false);

    const std::vector<VkDescriptorSetLayout> layouts(LSFG_GENERATION_SLOTS * 2, pass.SetLayout());
    owned_sets = CreateWrappedDescriptorSets(descriptor_pool, layouts);

    size_t next = 0;
    for (size_t slot = 0; slot < LSFG_GENERATION_SLOTS; ++slot) {
        Generation& entry = generations[slot];
        entry.buffer = resources.GetBuffer(LsfgSlotTimestamp(slot));
        for (auto& set : entry.descriptor_sets) {
            set = owned_sets[next++];
        }
    }
}

void LsfgGenerate::SetTarget(const Device& device, size_t slot, VkImageView view) {
    Generation& entry = generations[slot];
    if (entry.view == view) {
        return;
    }
    entry.view = view;

    for (size_t i = 0; i < entry.descriptor_sets.size(); ++i) {
        LsfgDescriptorWriter(entry.descriptor_sets[i])
            .AddUniformBuffer(entry.buffer, LsfgResources::BufferSize())
            .AddSampler(sampler)
            .AddSampler(edge_sampler)
            .AddSampledImage((*frames)[1 - i])
            .AddSampledImage((*frames)[i])
            .AddSampledImage(*motion)
            .AddSampledImage(*detail1)
            .AddSampledImage(*detail2)
            .AddStorageView(view)
            .Build(device);
    }
}

void LsfgGenerate::Dispatch(vk::CommandBuffer cmdbuf, u64 frame_count, size_t slot,
                            VkImage image, VkExtent2D extent) {
    const Generation& entry = generations[slot];

    LsfgBarriers(cmdbuf)
        .WriteToReadAll(*frames)
        .WriteToRead(*motion)
        .WriteToRead(*detail1)
        .WriteToRead(*detail2)
        .DiscardToWrite(image)
        .Build();

    pass.Bind(cmdbuf, entry.descriptor_sets[frame_count % entry.descriptor_sets.size()]);
    cmdbuf.Dispatch(GroupCount(extent.width), GroupCount(extent.height), 1);

    const std::array after{MakeTargetBarrier(image, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                             VK_ACCESS_2_SHADER_WRITE_BIT,
                                             vk::PIPELINE_STAGE_IMAGE_USERS,
                                             vk::ACCESS_IMAGE_USERS, VK_IMAGE_LAYOUT_GENERAL)};
    cmdbuf.PipelineBarrier(0, {}, {}, after);
}

} // namespace Vulkan
