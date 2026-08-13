// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include "video_core/renderer_vulkan/present/lsfg_common.h"
#include "video_core/renderer_vulkan/present/lsfg_shaders.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/vulkan_common/vulkan_device.h"

namespace Vulkan {

namespace {

constexpr u32 DESCRIPTORS_PER_TYPE = 4096;

VkImageMemoryBarrier MakeBarrier(const LsfgImage& image, VkAccessFlags src_access,
                                 VkAccessFlags dst_access) {
    return VkImageMemoryBarrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = src_access,
        .dstAccessMask = dst_access,
        .oldLayout = image.Layout(),
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image.Handle(),
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

LsfgImage::LsfgImage(const Device& device, MemoryAllocator& memory_allocator, VkExtent2D extent_,
                     VkFormat format)
    : extent{std::max(1u, extent_.width), std::max(1u, extent_.height)} {
    image = CreateWrappedImage(memory_allocator, extent, format);
    view = CreateWrappedImageView(device, image, format);
}

LsfgBarriers& LsfgBarriers::Push(LsfgImage& image, VkAccessFlags src_access,
                                 VkAccessFlags dst_access) {
    barriers.push_back(MakeBarrier(image, src_access, dst_access));
    image.SetLayout(VK_IMAGE_LAYOUT_GENERAL);
    return *this;
}

LsfgBarriers& LsfgBarriers::WriteToRead(LsfgImage& image) {
    return Push(image, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
}

LsfgBarriers& LsfgBarriers::ReadToWrite(LsfgImage& image) {
    return Push(image, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT);
}

void LsfgBarriers::Build() {
    if (barriers.empty()) {
        return;
    }
    cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, {}, {}, barriers);
    barriers.clear();
}

LsfgDescriptorWriter& LsfgDescriptorWriter::PushImage(VkDescriptorType type, VkSampler sampler,
                                                      VkImageView view) {
    image_infos.push_back(VkDescriptorImageInfo{
        .sampler = sampler,
        .imageView = view,
        .imageLayout = view == VK_NULL_HANDLE ? VK_IMAGE_LAYOUT_UNDEFINED
                                              : VK_IMAGE_LAYOUT_GENERAL,
    });
    writes.push_back(VkWriteDescriptorSet{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .pNext = nullptr,
        .dstSet = set,
        .dstBinding = binding++,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = type,
        .pImageInfo = &image_infos.back(),
        .pBufferInfo = nullptr,
        .pTexelBufferView = nullptr,
    });
    return *this;
}

LsfgDescriptorWriter& LsfgDescriptorWriter::AddSampler(VkSampler sampler) {
    return PushImage(VK_DESCRIPTOR_TYPE_SAMPLER, sampler, VK_NULL_HANDLE);
}

LsfgDescriptorWriter& LsfgDescriptorWriter::AddSampledImage(const LsfgImage& image) {
    return PushImage(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_NULL_HANDLE, image.View());
}

LsfgDescriptorWriter& LsfgDescriptorWriter::AddStorageImage(const LsfgImage& image) {
    return PushImage(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_NULL_HANDLE, image.View());
}

LsfgDescriptorWriter& LsfgDescriptorWriter::AddUniformBuffer(VkBuffer buffer, VkDeviceSize size) {
    buffer_infos.push_back(VkDescriptorBufferInfo{
        .buffer = buffer,
        .offset = 0,
        .range = size,
    });
    writes.push_back(VkWriteDescriptorSet{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .pNext = nullptr,
        .dstSet = set,
        .dstBinding = binding++,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .pImageInfo = nullptr,
        .pBufferInfo = &buffer_infos.back(),
        .pTexelBufferView = nullptr,
    });
    return *this;
}

void LsfgDescriptorWriter::Build(const Device& device) {
    if (writes.empty()) {
        return;
    }
    device.GetLogical().UpdateDescriptorSets(writes, {});
    writes.clear();
}

LsfgPass::LsfgPass(const Device& device, const LsfgShaders& shaders, u32 shader_id,
                   LsfgBindings bindings) {
    std::vector<VkDescriptorType> types;
    for (const auto& [count, type] : bindings) {
        types.insert(types.end(), count, type);
    }
    descriptor_count = static_cast<u32>(types.size());

    descriptor_set_layout = CreateWrappedDescriptorSetLayout(
        device, std::span<const VkDescriptorType>{types}, VK_SHADER_STAGE_COMPUTE_BIT);
    pipeline_layout = CreateWrappedPipelineLayout(device, descriptor_set_layout);
    pipeline = CreateWrappedComputePipeline(device, pipeline_layout, shaders.Get(shader_id));
}

void LsfgPass::Bind(vk::CommandBuffer cmdbuf, VkDescriptorSet set) const {
    cmdbuf.BindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE, *pipeline);
    cmdbuf.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE, *pipeline_layout, 0, set, {});
}

vk::DescriptorPool CreateLsfgDescriptorPool(const Device& device, u32 max_sets) {
    return CreateWrappedDescriptorPool(
        device, DESCRIPTORS_PER_TYPE, max_sets,
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_SAMPLER,
         VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE});
}

vk::Sampler CreateLsfgSampler(const Device& device, VkSamplerAddressMode address_mode,
                              VkCompareOp compare_op, bool white_border) {
    return device.GetLogical().CreateSampler(VkSamplerCreateInfo{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = address_mode,
        .addressModeV = address_mode,
        .addressModeW = address_mode,
        .mipLodBias = 0.0f,
        .anisotropyEnable = VK_FALSE,
        .maxAnisotropy = 0.0f,
        .compareEnable = VK_FALSE,
        .compareOp = compare_op,
        .minLod = 0.0f,
        .maxLod = VK_LOD_CLAMP_NONE,
        .borderColor = white_border ? VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE
                                    : VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
        .unnormalizedCoordinates = VK_FALSE,
    });
}

} // namespace Vulkan
