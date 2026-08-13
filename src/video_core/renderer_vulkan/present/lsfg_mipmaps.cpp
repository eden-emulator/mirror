// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstring>

#include "video_core/frame_gen/lossless_dll.h"
#include "video_core/renderer_vulkan/present/lsfg_mipmaps.h"
#include "video_core/renderer_vulkan/present/lsfg_shaders.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/vulkan_common/vulkan_device.h"

namespace Vulkan {

namespace {

constexpr VkFormat FLOW_FORMAT = VK_FORMAT_R8_UNORM;
constexpr u32 DISPATCH_TILE_SHIFT = 6;
constexpr size_t DESCRIPTOR_SET_COUNT = 2;

struct LsfgConstants {
    std::array<u32, 2> input_offset;
    u32 first_iter;
    u32 first_iter_s;
    u32 advanced_color_kind;
    u32 hdr_support;
    f32 resolution_inv_scale;
    f32 timestamp;
    f32 ui_threshold;
    std::array<u32, 3> padding;
};
static_assert(sizeof(LsfgConstants) == 48);

constexpr std::array<VkDescriptorType, 3 + LSFG_MIP_LEVELS> MIPMAPS_BINDINGS{
    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_SAMPLER,
    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
    VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
    VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
    VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
};

vk::Sampler CreateFlowSampler(const Device& device) {
    return device.GetLogical().CreateSampler(VkSamplerCreateInfo{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        .mipLodBias = 0.0f,
        .anisotropyEnable = VK_FALSE,
        .maxAnisotropy = 0.0f,
        .compareEnable = VK_FALSE,
        .compareOp = VK_COMPARE_OP_NEVER,
        .minLod = 0.0f,
        .maxLod = VK_LOD_CLAMP_NONE,
        .borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
        .unnormalizedCoordinates = VK_FALSE,
    });
}

} // Anonymous namespace

LsfgMipmaps::LsfgMipmaps(const Device& device, MemoryAllocator& memory_allocator_,
                         const LsfgShaders& shaders, VkExtent2D input_extent, f32 flow_scale)
    : memory_allocator{memory_allocator_} {
    flow_extent = VkExtent2D{
        .width = std::max(1u, static_cast<u32>(static_cast<f32>(input_extent.width) * flow_scale)),
        .height = std::max(1u, static_cast<u32>(static_cast<f32>(input_extent.height) * flow_scale)),
    };

    CreateImages(device);
    CreateUniformBuffer(flow_scale);

    sampler = CreateFlowSampler(device);
    descriptor_pool = CreateWrappedDescriptorPool(
        device, DESCRIPTOR_SET_COUNT * MIPMAPS_BINDINGS.size(), DESCRIPTOR_SET_COUNT,
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_SAMPLER,
         VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE});
    descriptor_set_layout = CreateWrappedDescriptorSetLayout(
        device, std::span<const VkDescriptorType>{MIPMAPS_BINDINGS}, VK_SHADER_STAGE_COMPUTE_BIT);

    const std::vector<VkDescriptorSetLayout> layouts(DESCRIPTOR_SET_COUNT,
                                                     *descriptor_set_layout);
    descriptor_sets = CreateWrappedDescriptorSets(descriptor_pool, layouts);

    pipeline_layout = CreateWrappedPipelineLayout(device, descriptor_set_layout);
    pipeline = CreateWrappedComputePipeline(
        device, pipeline_layout, shaders.Get(VideoCore::FrameGen::PerformanceShader::MIPMAPS));
}

VkExtent2D LsfgMipmaps::GetLevelExtent(size_t level) const {
    return VkExtent2D{
        .width = std::max(1u, flow_extent.width >> level),
        .height = std::max(1u, flow_extent.height >> level),
    };
}

void LsfgMipmaps::CreateImages(const Device& device) {
    for (size_t i = 0; i < LSFG_MIP_LEVELS; ++i) {
        images[i] = CreateWrappedImage(memory_allocator, GetLevelExtent(i), FLOW_FORMAT);
        image_views[i] = CreateWrappedImageView(device, images[i], FLOW_FORMAT);
    }
}

void LsfgMipmaps::CreateUniformBuffer(f32 flow_scale) {
    uniform_buffer = CreateWrappedBuffer(memory_allocator, sizeof(LsfgConstants),
                                         MemoryUsage::Upload);

    const LsfgConstants constants{
        .input_offset = {0, 0},
        .first_iter = 0,
        .first_iter_s = 0,
        .advanced_color_kind = 0,
        .hdr_support = 0,
        .resolution_inv_scale = 1.0f / flow_scale,
        .timestamp = 0.0f,
        .ui_threshold = 0.5f,
        .padding = {0, 0, 0},
    };

    const std::span<u8> mapped = uniform_buffer.Mapped();
    std::memcpy(mapped.data(), &constants, sizeof(constants));
    uniform_buffer.Flush();
}

void LsfgMipmaps::Dispatch(const Device& device, Scheduler& scheduler, VkImage current_image,
                           VkImageView current_view, u64 frame_count) {
    const size_t set_index = frame_count % DESCRIPTOR_SET_COUNT;
    const VkDescriptorSet set = descriptor_sets[set_index];

    const VkDescriptorBufferInfo buffer_info{
        .buffer = *uniform_buffer,
        .offset = 0,
        .range = sizeof(LsfgConstants),
    };
    const VkDescriptorImageInfo sampler_info{
        .sampler = *sampler,
        .imageView = VK_NULL_HANDLE,
        .imageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    const VkDescriptorImageInfo sampled_info{
        .sampler = VK_NULL_HANDLE,
        .imageView = current_view,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
    };
    std::array<VkDescriptorImageInfo, LSFG_MIP_LEVELS> storage_infos{};
    for (size_t i = 0; i < LSFG_MIP_LEVELS; ++i) {
        storage_infos[i] = VkDescriptorImageInfo{
            .sampler = VK_NULL_HANDLE,
            .imageView = *image_views[i],
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        };
    }

    std::vector<VkWriteDescriptorSet> writes;
    writes.reserve(MIPMAPS_BINDINGS.size());
    const auto push = [&](u32 binding, VkDescriptorType type,
                          const VkDescriptorImageInfo* image_info,
                          const VkDescriptorBufferInfo* buf_info) {
        writes.push_back(VkWriteDescriptorSet{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = nullptr,
            .dstSet = set,
            .dstBinding = binding,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = type,
            .pImageInfo = image_info,
            .pBufferInfo = buf_info,
            .pTexelBufferView = nullptr,
        });
    };

    push(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &buffer_info);
    push(1, VK_DESCRIPTOR_TYPE_SAMPLER, &sampler_info, nullptr);
    push(2, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &sampled_info, nullptr);
    for (u32 i = 0; i < LSFG_MIP_LEVELS; ++i) {
        push(3 + i, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &storage_infos[i], nullptr);
    }

    device.GetLogical().UpdateDescriptorSets(writes, {});

    const u32 groups_x = (flow_extent.width + (1u << DISPATCH_TILE_SHIFT) - 1) >>
                         DISPATCH_TILE_SHIFT;
    const u32 groups_y = (flow_extent.height + (1u << DISPATCH_TILE_SHIFT) - 1) >>
                         DISPATCH_TILE_SHIFT;

    std::array<VkImage, LSFG_MIP_LEVELS> raw_images{};
    for (size_t i = 0; i < LSFG_MIP_LEVELS; ++i) {
        raw_images[i] = *images[i];
    }

    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([raw_images, current_image, set, groups_x, groups_y,
                      layout = *pipeline_layout,
                      compute_pipeline = *pipeline](vk::CommandBuffer cmdbuf) {
        const VkImageMemoryBarrier input_barrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = current_image,
            .subresourceRange{
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };
        cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, {}, {}, input_barrier);

        for (const VkImage image : raw_images) {
            TransitionImageLayout(cmdbuf, image, VK_IMAGE_LAYOUT_GENERAL,
                                  VK_IMAGE_LAYOUT_UNDEFINED);
        }

        cmdbuf.BindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE, compute_pipeline);
        cmdbuf.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, set, {});
        cmdbuf.Dispatch(groups_x, groups_y, 1);
    });
}

} // namespace Vulkan
