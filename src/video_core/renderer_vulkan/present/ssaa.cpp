// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/common_types.h"

#include "video_core/host_shaders/ssaa_frag_spv.h"
#include "video_core/host_shaders/full_screen_triangle_vert_spv.h"
#include "video_core/renderer_vulkan/present/ssaa.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/vulkan_common/vulkan_device.h"

namespace Vulkan {

SSAA::SSAA(const Device& device, MemoryAllocator& allocator, size_t image_count, VkExtent2D extent)
    : m_device(device), m_allocator(allocator), m_extent(extent)
    , m_image_count(u32(image_count))
{
    CreateImages();
    CreateRenderPasses();
    CreateSampler();
    CreateShaders();
    CreateDescriptorPool();
    CreateDescriptorSetLayouts();
    CreateDescriptorSets();
    CreatePipelineLayouts();
    CreatePipelines();
}

SSAA::~SSAA() = default;

void SSAA::CreateImages() {
    for (u32 i = 0; i < m_image_count; i++) {
        Image& image = m_dynamic_images.emplace_back();
        image.image = CreateWrappedImage(m_allocator, m_extent, VK_FORMAT_R16G16B16A16_SFLOAT);
        image.image_view = CreateWrappedImageView(m_device, image.image, VK_FORMAT_R16G16B16A16_SFLOAT);
    }
}

void SSAA::CreateRenderPasses() {
    m_renderpass = CreateWrappedRenderPass(m_device, VK_FORMAT_R16G16B16A16_SFLOAT);
    for (auto& image : m_dynamic_images) {
        image.framebuffer = CreateWrappedFramebuffer(m_device, m_renderpass, image.image_view, m_extent);
    }
}

void SSAA::CreateSampler() {
    m_sampler = CreateWrappedSampler(m_device);
}

void SSAA::CreateShaders() {
    m_vertex_shader = CreateWrappedShaderModule(m_device, FULL_SCREEN_TRIANGLE_VERT_SPV);
    m_fragment_shader = CreateWrappedShaderModule(m_device, SSAA_FRAG_SPV);
}

void SSAA::CreateDescriptorPool() {
    // 2 descriptors, 1 descriptor set per image
    m_descriptor_pool = CreateWrappedDescriptorPool(m_device, 2 * m_image_count, m_image_count);
}

void SSAA::CreateDescriptorSetLayouts() {
    m_descriptor_set_layout = CreateWrappedDescriptorSetLayout(m_device, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER});
}

void SSAA::CreateDescriptorSets() {
    VkDescriptorSetLayout layout = *m_descriptor_set_layout;
    for (auto& images : m_dynamic_images)
        images.descriptor_sets = CreateWrappedDescriptorSets(m_descriptor_pool, {layout});
}

void SSAA::CreatePipelineLayouts() {
    m_pipeline_layout = CreateWrappedPipelineLayout(m_device, m_descriptor_set_layout);
}

void SSAA::CreatePipelines() {
    m_pipeline = CreateWrappedPipeline(m_device, m_renderpass, m_pipeline_layout, std::tie(m_vertex_shader, m_fragment_shader));
}

void SSAA::UpdateDescriptorSets(VkImageView image_view, size_t image_index) {
    Image& image = m_dynamic_images[image_index];
    std::vector<VkDescriptorImageInfo> image_infos;
    std::vector<VkWriteDescriptorSet> updates;
    image_infos.reserve(2);
    updates.push_back(CreateWriteDescriptorSet(image_infos, *m_sampler, image_view, image.descriptor_sets[0], 0));
    updates.push_back(CreateWriteDescriptorSet(image_infos, *m_sampler, image_view, image.descriptor_sets[0], 1));
    m_device.GetLogical().UpdateDescriptorSets(updates, {});
}

void SSAA::UploadImages(Scheduler& scheduler) {
    if (!m_images_ready) {
        m_images_ready = true;
        scheduler.Record([&](vk::CommandBuffer cmdbuf) {
            for (auto& image : m_dynamic_images)
                ClearColorImage(cmdbuf, *image.image);
        });
        scheduler.Finish();
    }
}

void SSAA::Draw(Scheduler& scheduler, size_t image_index, VkImage* inout_image, VkImageView* inout_image_view) {
    const Image& image{m_dynamic_images[image_index]};
    const VkImage input_image{*inout_image};
    const VkImage output_image{*image.image};
    const VkDescriptorSet descriptor_set{image.descriptor_sets[0]};
    const VkFramebuffer framebuffer{*image.framebuffer};
    const VkRenderPass renderpass{*m_renderpass};
    const VkPipeline pipeline{*m_pipeline};
    const VkPipelineLayout layout{*m_pipeline_layout};
    const VkExtent2D extent{m_extent};

    UploadImages(scheduler);
    UpdateDescriptorSets(*inout_image_view, image_index);

    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([=](vk::CommandBuffer cmdbuf) {
        TransitionImageLayout(cmdbuf, input_image, VK_IMAGE_LAYOUT_GENERAL);
        TransitionImageLayout(cmdbuf, output_image, VK_IMAGE_LAYOUT_GENERAL);
        BeginRenderPass(cmdbuf, renderpass, framebuffer, extent);
        cmdbuf.BindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        cmdbuf.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, descriptor_set, {});
        cmdbuf.Draw(3, 1, 0, 0);
        cmdbuf.EndRenderPass();
        TransitionImageLayout(cmdbuf, output_image, VK_IMAGE_LAYOUT_GENERAL);
    });

    *inout_image = *image.image;
    *inout_image_view = *image.image_view;
}

} // namespace Vulkan
