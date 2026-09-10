// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <chrono>
#include <map>
#include <string>
#include <vector>

#include "common/common_types.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

class Device;
class Scheduler;

class PostProcessChain {
public:
    explicit PostProcessChain(const Device& device, MemoryAllocator& allocator, Scheduler& scheduler,
                              size_t image_count, VkExtent2D extent);
    ~PostProcessChain();

    void Draw(const Device& device, Scheduler& scheduler, size_t image_index, VkImage* inout_image,
              VkImageView* inout_image_view);

    bool Empty() const;

private:
    enum class UniformKind : u32 {
        Boolean,
        Integer,
        Floating,
    };

    enum class UniformSource : u32 {
        Value,
        FrameTime,
        FrameCount,
        Timer,
        Random,
        PingPong,
    };

    struct UniformWrite {
        std::string name;
        u32 offset{};
        u32 components{};
        UniformKind kind{UniformKind::Floating};
        UniformSource source{UniformSource::Value};
        std::array<f32, 4> fallback{};
        std::array<f32, 4> args{};
        f32 state{};
        f32 direction{1.0f};
    };

    struct Texture {
        std::string name;
        vk::Image image{};
        vk::ImageView view{};
        VkExtent2D extent{};
        VkFormat format{};
        bool is_backbuffer{};
    };

    struct Sampler {
        vk::Sampler sampler{};
        size_t texture_index{};
    };

    struct SamplerBinding {
        u32 binding{};
        size_t sampler_index{};
    };

    struct Pass {
        vk::RenderPass renderpass{};
        vk::Pipeline pipeline{};
        vk::DescriptorSetLayout sampler_layout{};
        vk::PipelineLayout pipeline_layout{};
        vk::DescriptorSets sampler_sets{};
        std::vector<SamplerBinding> sampler_bindings{};
        std::vector<vk::Framebuffer> framebuffers{};
        size_t target_texture{};
        VkExtent2D extent{};
        u32 num_vertices{3};
        bool clear{};
        bool writes_backbuffer{};
        u32 backbuffer_slot{};
    };

    struct Effect {
        size_t entry_index{};
        std::string file{};
        std::map<std::string, vk::ShaderModule> shaders{};
        std::vector<Texture> textures{};
        std::vector<Sampler> samplers{};
        std::vector<Pass> passes{};
        std::vector<UniformWrite> uniforms{};
        u32 uniform_size{};
        std::vector<vk::Buffer> uniform_buffers{};
        vk::DescriptorSetLayout uniform_layout{};
        vk::DescriptorPool descriptor_pool{};
        vk::DescriptorSets uniform_sets{};
        size_t backbuffer_pass_count{};
        u32 backbuffer_slots{1};
    };

    struct FrameImages {
        std::array<vk::Image, 2> images{};
        std::array<vk::ImageView, 2> views{};
    };

    bool BuildEffects(const Device& device, MemoryAllocator& allocator, Scheduler& scheduler);
    void CreatePingPongImages(const Device& device, MemoryAllocator& allocator);
    void PrepareImages(const Device& device, Scheduler& scheduler);
    void UpdateUniforms(Effect& effect, size_t image_index, f32 delta_seconds);
    void UpdateDescriptors(const Device& device, Effect& effect, Pass& pass, size_t image_index,
                           VkImageView backbuffer_view);

    const VkExtent2D m_extent;
    const u32 m_image_count;

    std::vector<Effect> m_effects{};
    std::vector<FrameImages> m_frames{};

    vk::Sampler m_fallback_sampler{};
    vk::Image m_fallback_image{};
    vk::ImageView m_fallback_view{};

    std::chrono::steady_clock::time_point m_start{};
    std::chrono::steady_clock::time_point m_previous{};
    u64 m_frame_count{};
    bool m_images_ready{};
};

} // namespace Vulkan
