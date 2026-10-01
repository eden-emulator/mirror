// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <list>

#include "common/assert.h"
#include <ranges>

#include "video_core/renderer_vulkan/present/smaa.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/smaa_area_tex.h"
#include "video_core/smaa_search_tex.h"
#include "video_core/vulkan_common/vulkan_device.h"

#include "video_core/host_shaders/smaa_blending_weight_calculation_frag_spv.h"
#include "video_core/host_shaders/smaa_blending_weight_calculation_vert_spv.h"
#include "video_core/host_shaders/smaa_edge_detection_frag_spv.h"
#include "video_core/host_shaders/smaa_edge_detection_vert_spv.h"
#include "video_core/host_shaders/smaa_neighborhood_blending_frag_spv.h"
#include "video_core/host_shaders/smaa_neighborhood_blending_vert_spv.h"

namespace Vulkan {

SMAA::SMAA(const Device& device, MemoryAllocator& allocator, size_t image_count, VkExtent2D extent)
    : m_allocator(allocator)
    , m_extent(extent)
    , m_image_count(u32(image_count))
{
    CreateImages(device);
    CreateSampler(device);
    CreateShaders(device);
    CreateDescriptorPool(device);
    CreateDescriptorSetLayouts(device);
    CreateDescriptorSets(device);
    CreatePipelineLayouts(device);
    CreatePipelines(device);
}

SMAA::~SMAA() = default;

void SMAA::CreateImages(const Device& device) {
    static constexpr VkExtent2D area_extent{AREATEX_WIDTH, AREATEX_HEIGHT};
    static constexpr VkExtent2D search_extent{SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT};

    m_static_images[Area] = CreateWrappedImage(m_allocator, area_extent, VK_FORMAT_R8G8_UNORM);
    m_static_images[Search] = CreateWrappedImage(m_allocator, search_extent, VK_FORMAT_R8_UNORM);

    m_static_image_views[Area] =
        CreateWrappedImageView(device, m_static_images[Area], VK_FORMAT_R8G8_UNORM);
    m_static_image_views[Search] =
        CreateWrappedImageView(device, m_static_images[Search], VK_FORMAT_R8_UNORM);

    m_dynamic_images[Blend] =
        CreateWrappedImage(m_allocator, m_extent, VK_FORMAT_R16G16B16A16_SFLOAT);
    m_dynamic_images[Edges] = CreateWrappedImage(m_allocator, m_extent, VK_FORMAT_R16G16_SFLOAT);
    m_dynamic_images[Output] =
        CreateWrappedImage(m_allocator, m_extent, VK_FORMAT_R16G16B16A16_SFLOAT);

    m_dynamic_image_views[Blend] =
        CreateWrappedImageView(device, m_dynamic_images[Blend], VK_FORMAT_R16G16B16A16_SFLOAT);
    m_dynamic_image_views[Edges] =
        CreateWrappedImageView(device, m_dynamic_images[Edges], VK_FORMAT_R16G16_SFLOAT);
    m_dynamic_image_views[Output] =
        CreateWrappedImageView(device, m_dynamic_images[Output], VK_FORMAT_R16G16B16A16_SFLOAT);
}

void SMAA::CreateSampler(const Device& device) {
    m_sampler = CreateWrappedSampler(device);
}

void SMAA::CreateShaders(const Device& device) {
    // These match the order of the SMAAStage enum
    static constexpr std::array vert_shader_sources{
        ARRAY_TO_SPAN(SMAA_EDGE_DETECTION_VERT_SPV),
        ARRAY_TO_SPAN(SMAA_BLENDING_WEIGHT_CALCULATION_VERT_SPV),
        ARRAY_TO_SPAN(SMAA_NEIGHBORHOOD_BLENDING_VERT_SPV),
    };
    static constexpr std::array frag_shader_sources{
        ARRAY_TO_SPAN(SMAA_EDGE_DETECTION_FRAG_SPV),
        ARRAY_TO_SPAN(SMAA_BLENDING_WEIGHT_CALCULATION_FRAG_SPV),
        ARRAY_TO_SPAN(SMAA_NEIGHBORHOOD_BLENDING_FRAG_SPV),
    };

    for (size_t i = 0; i < MaxSMAAStage; i++) {
        m_vertex_shaders[i] = CreateWrappedShaderModule(device, vert_shader_sources[i]);
        m_fragment_shaders[i] = CreateWrappedShaderModule(device, frag_shader_sources[i]);
    }
}

void SMAA::CreateDescriptorPool(const Device& device) {
    // Edge detection: 1 descriptor
    // Blending weight calculation: 3 descriptors
    // Neighborhood blending: 2 descriptors

    // 6 descriptors, 3 descriptor sets per image
    m_descriptor_pool = CreateWrappedDescriptorPool(device, 6 * m_image_count, 3 * m_image_count);
}

void SMAA::CreateDescriptorSetLayouts(const Device& device) {
    m_descriptor_set_layouts[EdgeDetection] =
        CreateWrappedDescriptorSetLayout(device, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER});
    m_descriptor_set_layouts[BlendingWeightCalculation] =
        CreateWrappedDescriptorSetLayout(device, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER});
    m_descriptor_set_layouts[NeighborhoodBlending] =
        CreateWrappedDescriptorSetLayout(device, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER});
}

void SMAA::CreateDescriptorSets(const Device& device) {
    std::vector<VkDescriptorSetLayout> layouts(m_descriptor_set_layouts.size());
    std::ranges::transform(m_descriptor_set_layouts, layouts.begin(),
                           [](auto& layout) { return *layout; });

    for (u32 i = 0; i < m_image_count; i++) {
        m_descriptor_sets.push_back(CreateWrappedDescriptorSets(m_descriptor_pool, layouts));
    }
}

void SMAA::CreatePipelineLayouts(const Device& device) {
    for (size_t i = 0; i < MaxSMAAStage; i++) {
        m_pipeline_layouts[i] = CreateWrappedPipelineLayout(device, m_descriptor_set_layouts[i]);
    }
}

void SMAA::CreatePipelines(const Device& device) {
    static constexpr std::array<VkFormat, MaxSMAAStage> formats{
        VK_FORMAT_R16G16_SFLOAT,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_FORMAT_R16G16B16A16_SFLOAT,
    };
    for (size_t i = 0; i < MaxSMAAStage; i++) {
        m_pipelines[i] =
            CreateWrappedPipeline(device, formats[i], m_pipeline_layouts[i],
                                  std::tie(m_vertex_shaders[i], m_fragment_shaders[i]));
    }
}

void SMAA::UpdateDescriptorSets(const Device& device, VkImageView image_view, size_t image_index) {
    const vk::DescriptorSets& descriptor_sets = m_descriptor_sets[image_index];
    std::vector<VkDescriptorImageInfo> image_infos;
    std::vector<VkWriteDescriptorSet> updates;
    image_infos.reserve(6);

    updates.push_back(CreateWriteDescriptorSet(image_infos, *m_sampler, image_view,
                                               descriptor_sets[EdgeDetection], 0));

    updates.push_back(CreateWriteDescriptorSet(image_infos, *m_sampler,
                                               *m_dynamic_image_views[Edges],
                                               descriptor_sets[BlendingWeightCalculation],
                                               0));
    updates.push_back(CreateWriteDescriptorSet(image_infos, *m_sampler, *m_static_image_views[Area],
                                               descriptor_sets[BlendingWeightCalculation],
                                               1));
    updates.push_back(
        CreateWriteDescriptorSet(image_infos, *m_sampler, *m_static_image_views[Search],
                                 descriptor_sets[BlendingWeightCalculation], 2));

    updates.push_back(CreateWriteDescriptorSet(image_infos, *m_sampler, image_view,
                                               descriptor_sets[NeighborhoodBlending], 0));
    updates.push_back(CreateWriteDescriptorSet(image_infos, *m_sampler,
                                               *m_dynamic_image_views[Blend],
                                               descriptor_sets[NeighborhoodBlending], 1));

    device.GetLogical().UpdateDescriptorSets(updates, {});
}

void SMAA::UploadImages(const Device& device, Scheduler& scheduler) {
    if (m_images_ready) {
        return;
    }

    static constexpr VkExtent2D area_extent{AREATEX_WIDTH, AREATEX_HEIGHT};
    static constexpr VkExtent2D search_extent{SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT};

    UploadImage(device, m_allocator, scheduler, m_static_images[Area], area_extent,
                VK_FORMAT_R8G8_UNORM, ARRAY_TO_SPAN(areaTexBytes));
    UploadImage(device, m_allocator, scheduler, m_static_images[Search], search_extent,
                VK_FORMAT_R8_UNORM, ARRAY_TO_SPAN(searchTexBytes));

    m_images_ready = true;
}

void SMAA::Draw(const Device& device, Scheduler& scheduler, size_t image_index, VkImage* inout_image, VkImageView* inout_image_view) {
    const vk::DescriptorSets& descriptor_sets = m_descriptor_sets[image_index];

    VkImage input_image = *inout_image;
    VkImage output_image = *m_dynamic_images[Output];
    VkImage edges_image = *m_dynamic_images[Edges];
    VkImage blend_image = *m_dynamic_images[Blend];

    VkDescriptorSet edge_detection_descriptor_set = descriptor_sets[EdgeDetection];
    VkDescriptorSet blending_weight_calculation_descriptor_set =
        descriptor_sets[BlendingWeightCalculation];
    VkDescriptorSet neighborhood_blending_descriptor_set =
        descriptor_sets[NeighborhoodBlending];

    VkImageView edges_view = *m_dynamic_image_views[Edges];
    VkImageView blend_view = *m_dynamic_image_views[Blend];
    VkImageView output_view = *m_dynamic_image_views[Output];

    UploadImages(device, scheduler);
    UpdateDescriptorSets(device, *inout_image_view, image_index);

    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([=, this](vk::CommandBuffer cmdbuf) {
        TransitionImageLayout(cmdbuf, input_image, VK_IMAGE_LAYOUT_GENERAL);
        TransitionImageLayout(cmdbuf, edges_image, VK_IMAGE_LAYOUT_GENERAL,
                              VK_IMAGE_LAYOUT_UNDEFINED);
        BeginRendering(cmdbuf, edges_view, m_extent, VK_ATTACHMENT_LOAD_OP_CLEAR);
        cmdbuf.BindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, *m_pipelines[EdgeDetection]);
        cmdbuf.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  *m_pipeline_layouts[EdgeDetection], 0,
                                  edge_detection_descriptor_set, {});
        cmdbuf.Draw(3, 1, 0, 0);
        cmdbuf.EndRendering();

        TransitionImageLayout(cmdbuf, edges_image, VK_IMAGE_LAYOUT_GENERAL);
        TransitionImageLayout(cmdbuf, blend_image, VK_IMAGE_LAYOUT_GENERAL,
                              VK_IMAGE_LAYOUT_UNDEFINED);
        BeginRendering(cmdbuf, blend_view, m_extent, VK_ATTACHMENT_LOAD_OP_DONT_CARE);
        cmdbuf.BindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                            *m_pipelines[BlendingWeightCalculation]);
        cmdbuf.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  *m_pipeline_layouts[BlendingWeightCalculation], 0,
                                  blending_weight_calculation_descriptor_set, {});
        cmdbuf.Draw(3, 1, 0, 0);
        cmdbuf.EndRendering();

        TransitionImageLayout(cmdbuf, blend_image, VK_IMAGE_LAYOUT_GENERAL);
        TransitionImageLayout(cmdbuf, output_image, VK_IMAGE_LAYOUT_GENERAL,
                              VK_IMAGE_LAYOUT_UNDEFINED);
        BeginRendering(cmdbuf, output_view, m_extent, VK_ATTACHMENT_LOAD_OP_DONT_CARE);
        cmdbuf.BindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, *m_pipelines[NeighborhoodBlending]);
        cmdbuf.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  *m_pipeline_layouts[NeighborhoodBlending], 0,
                                  neighborhood_blending_descriptor_set, {});
        cmdbuf.Draw(3, 1, 0, 0);
        cmdbuf.EndRendering();
        TransitionImageLayout(cmdbuf, output_image, VK_IMAGE_LAYOUT_GENERAL);
    });

    *inout_image = *m_dynamic_images[Output];
    *inout_image_view = *m_dynamic_image_views[Output];
}

} // namespace Vulkan
