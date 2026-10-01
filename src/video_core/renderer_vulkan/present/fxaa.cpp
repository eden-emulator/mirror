// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/common_types.h"

#include "video_core/host_shaders/fxaa_frag_spv.h"
#include "video_core/host_shaders/fxaa_vert_spv.h"
#include "video_core/renderer_vulkan/present/fxaa.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/vulkan_common/vulkan_device.h"

namespace Vulkan {

FXAA::FXAA(const Device& device, MemoryAllocator& allocator, size_t image_count, VkExtent2D extent)
    : m_extent(extent)
    , m_image_count(u32(image_count))
{
    CreateImages(device, allocator);
    CreateSampler(device);
    CreateShaders(device);
    CreateDescriptorPool(device);
    CreateDescriptorSetLayouts(device);
    CreateDescriptorSets(device);
    CreatePipelineLayouts(device);
    CreatePipelines(device);
}

FXAA::~FXAA() = default;

void FXAA::CreateImages(const Device& device, MemoryAllocator& allocator) {
    m_image = CreateWrappedImage(allocator, m_extent, VK_FORMAT_R16G16B16A16_SFLOAT);
    m_image_view = CreateWrappedImageView(device, m_image, VK_FORMAT_R16G16B16A16_SFLOAT);
}

void FXAA::CreateSampler(const Device& device) {
    m_sampler = CreateWrappedSampler(device);
}

void FXAA::CreateShaders(const Device& device) {
    m_vertex_shader = CreateWrappedShaderModule(device, FXAA_VERT_SPV);
    m_fragment_shader = CreateWrappedShaderModule(device, FXAA_FRAG_SPV);
}

void FXAA::CreateDescriptorPool(const Device& device) {
    // 2 descriptors, 1 descriptor set per image
    m_descriptor_pool = CreateWrappedDescriptorPool(device, 2 * m_image_count, m_image_count);
}

void FXAA::CreateDescriptorSetLayouts(const Device& device) {
    m_descriptor_set_layout =
        CreateWrappedDescriptorSetLayout(device, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER});
}

void FXAA::CreateDescriptorSets(const Device& device) {
    VkDescriptorSetLayout layout = *m_descriptor_set_layout;

    for (u32 i = 0; i < m_image_count; i++) {
        m_descriptor_sets.push_back(CreateWrappedDescriptorSets(m_descriptor_pool, {layout}));
    }
}

void FXAA::CreatePipelineLayouts(const Device& device) {
    m_pipeline_layout = CreateWrappedPipelineLayout(device, m_descriptor_set_layout);
}

void FXAA::CreatePipelines(const Device& device) {
    m_pipeline = CreateWrappedPipeline(device, VK_FORMAT_R16G16B16A16_SFLOAT, m_pipeline_layout,
                                       std::tie(m_vertex_shader, m_fragment_shader));
}

void FXAA::UpdateDescriptorSets(const Device& device, VkImageView image_view, size_t image_index) {
    const VkDescriptorSet descriptor_set = m_descriptor_sets[image_index][0];
    std::vector<VkDescriptorImageInfo> image_infos;
    std::vector<VkWriteDescriptorSet> updates;
    image_infos.reserve(2);

    updates.push_back(CreateWriteDescriptorSet(image_infos, *m_sampler, image_view, descriptor_set, 0));
    updates.push_back(CreateWriteDescriptorSet(image_infos, *m_sampler, image_view, descriptor_set, 1));

    device.GetLogical().UpdateDescriptorSets(updates, {});
}

void FXAA::Draw(const Device& device, Scheduler& scheduler, size_t image_index, VkImage* inout_image, VkImageView* inout_image_view) {
    const VkImage input_image{*inout_image};
    const VkImage output_image{*m_image};
    const VkImageView output_view{*m_image_view};
    const VkDescriptorSet descriptor_set{m_descriptor_sets[image_index][0]};
    const VkPipeline pipeline{*m_pipeline};
    const VkPipelineLayout layout{*m_pipeline_layout};
    const VkExtent2D extent{m_extent};

    UpdateDescriptorSets(device, *inout_image_view, image_index);

    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([=](vk::CommandBuffer cmdbuf) {
        TransitionImageLayout(cmdbuf, input_image, VK_IMAGE_LAYOUT_GENERAL);
        TransitionImageLayout(cmdbuf, output_image, VK_IMAGE_LAYOUT_GENERAL,
                              VK_IMAGE_LAYOUT_UNDEFINED);
        BeginRendering(cmdbuf, output_view, extent, VK_ATTACHMENT_LOAD_OP_DONT_CARE);
        cmdbuf.BindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        cmdbuf.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, descriptor_set, {});
        cmdbuf.Draw(3, 1, 0, 0);
        cmdbuf.EndRendering();
        TransitionImageLayout(cmdbuf, output_image, VK_IMAGE_LAYOUT_GENERAL);
    });

    *inout_image = output_image;
    *inout_image_view = output_view;
}

} // namespace Vulkan
