// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

#define ARRAY_TO_SPAN(a) std::span(a, (sizeof(a) / sizeof(a[0])))

vk::Buffer CreateWrappedBuffer(MemoryAllocator& allocator, VkDeviceSize size, MemoryUsage usage);

vk::Image CreateWrappedImage(MemoryAllocator& allocator, VkExtent2D dimensions, VkFormat format);
void TransitionImageLayout(vk::CommandBuffer& cmdbuf, VkImage image, VkImageLayout target_layout,
                           VkImageLayout source_layout = VK_IMAGE_LAYOUT_GENERAL);
void UploadImage(const Device& device, MemoryAllocator& allocator, Scheduler& scheduler,
                 vk::Image& image, VkExtent2D dimensions, VkFormat format,
                 std::span<const u8> initial_contents = {});
void DownloadColorImage(vk::CommandBuffer& cmdbuf, VkImage image, VkBuffer buffer,
                        VkExtent3D extent);
void ClearColorImage(vk::CommandBuffer& cmdbuf, VkImage image);

vk::ImageView CreateWrappedImageView(const Device& device, vk::Image& image, VkFormat format);
vk::Sampler CreateWrappedSampler(const Device& device, VkFilter filter = VK_FILTER_LINEAR);
vk::ShaderModule CreateWrappedShaderModule(const Device& device, std::span<const u32> code);
vk::DescriptorPool CreateWrappedDescriptorPool(const Device& device, size_t max_descriptors,
                                               size_t max_sets,
                                               std::initializer_list<VkDescriptorType> types = {
                                                   VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER});
vk::DescriptorSetLayout CreateWrappedDescriptorSetLayout(
    const Device& device, std::initializer_list<VkDescriptorType> types,
    VkShaderStageFlags stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
vk::DescriptorSetLayout CreateWrappedDescriptorSetLayout(const Device& device,
                                                         std::span<const VkDescriptorType> types,
                                                         VkShaderStageFlags stages);
vk::DescriptorSets CreateWrappedDescriptorSets(vk::DescriptorPool& pool,
                                               vk::Span<VkDescriptorSetLayout> layouts);
vk::PipelineLayout CreateWrappedPipelineLayout(const Device& device,
                                               vk::DescriptorSetLayout& layout);
[[nodiscard]] VkPipelineRenderingCreateInfo ColorRenderingInfo(const VkFormat& format);
vk::Pipeline CreateWrappedPipeline(const Device& device, VkFormat format,
                                   vk::PipelineLayout& layout,
                                   std::tuple<vk::ShaderModule&, vk::ShaderModule&> shaders);
vk::Pipeline CreateWrappedComputePipeline(const Device& device, vk::PipelineLayout& layout,
                                          VkShaderModule shader);
vk::Pipeline CreateWrappedPremultipliedBlendingPipeline(
    const Device& device, VkFormat format, vk::PipelineLayout& layout,
    std::tuple<vk::ShaderModule&, vk::ShaderModule&> shaders);
vk::Pipeline CreateWrappedCoverageBlendingPipeline(
    const Device& device, VkFormat format, vk::PipelineLayout& layout,
    std::tuple<vk::ShaderModule&, vk::ShaderModule&> shaders);
VkWriteDescriptorSet CreateWriteDescriptorSet(std::vector<VkDescriptorImageInfo>& images,
                                              VkSampler sampler, VkImageView view,
                                              VkDescriptorSet set, u32 binding);
vk::Sampler CreateBilinearSampler(const Device& device);
vk::Sampler CreateNearestNeighborSampler(const Device& device);
vk::Sampler CreateCubicSampler(const Device& device, VkCubicFilterWeightsQCOM qcom_weights);

void BeginRendering(vk::CommandBuffer& cmdbuf, VkImageView view, VkExtent2D extent,
                    VkAttachmentLoadOp load_op, const VkClearValue& clear_value = {});

} // namespace Vulkan
