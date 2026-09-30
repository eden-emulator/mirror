// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <list>
#include <vector>

#include "common/math_util.h"
#include "video_core/renderer_vulkan/present/present_push_constants.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Layout {
struct FramebufferLayout;
}

namespace Tegra {
struct FramebufferConfig;
}

namespace Vulkan {

class Device;
struct Frame;
class Layer;
class Scheduler;
class RasterizerVulkan;

class WindowAdaptPass final {
public:
    explicit WindowAdaptPass(const Device& device, VkFormat frame_format, vk::Sampler&& sampler,
                             vk::ShaderModule&& fragment_shader);
    ~WindowAdaptPass();

    void Draw(const Device& device, RasterizerVulkan& rasterizer, Scheduler& scheduler, size_t image_index,
              std::list<Layer>& layers, std::span<const Tegra::FramebufferConfig> configs,
              const Layout::FramebufferLayout& layout, Frame* dst);

    void DrawGenerated(const Device& device, Scheduler& scheduler, Layer& layer, size_t generation,
                       VkImage image, VkImageView view, const Layout::FramebufferLayout& layout,
                       Frame* dst);

    VkDescriptorSetLayout GetDescriptorSetLayout();

private:
    void Record(Scheduler& scheduler, Frame* dst, std::vector<PresentPushConstants> push_constants,
                std::vector<VkDescriptorSet> descriptor_sets) const;

    void CreateDescriptorSetLayout(const Device& device);
    void CreatePipelineLayout(const Device& device);
    void CreateVertexShader(const Device& device);
    void CreatePipelines(const Device& device, VkFormat frame_format);

    vk::DescriptorSetLayout descriptor_set_layout;
    vk::PipelineLayout pipeline_layout;
    vk::Sampler sampler;
    vk::ShaderModule vertex_shader;
    vk::ShaderModule fragment_shader;
    vk::Pipeline opaque_pipeline;
    vk::Pipeline premultiplied_pipeline;
    vk::Pipeline coverage_pipeline;
    std::vector<VkPipeline> layer_pipelines;
    std::vector<PresentPushConstants> layer_push_constants;
    std::vector<VkDescriptorSet> layer_descriptor_sets;
};

} // namespace Vulkan
