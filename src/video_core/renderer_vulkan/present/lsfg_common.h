// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <deque>
#include <initializer_list>
#include <utility>
#include <vector>

#include "common/common_types.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

class Device;
class LsfgShaders;

constexpr VkFormat LSFG_DEFAULT_FORMAT = VK_FORMAT_R8G8B8A8_UNORM;

class LsfgImage {
public:
    LsfgImage() = default;
    LsfgImage(const Device& device, MemoryAllocator& memory_allocator, VkExtent2D extent_,
              VkFormat format = LSFG_DEFAULT_FORMAT);

    [[nodiscard]] VkImage Handle() const {
        return *image;
    }

    [[nodiscard]] VkImageView View() const {
        return *view;
    }

    [[nodiscard]] VkExtent2D Extent() const {
        return extent;
    }

    [[nodiscard]] VkImageLayout Layout() const {
        return layout;
    }

    void SetLayout(VkImageLayout new_layout) {
        layout = new_layout;
    }

private:
    vk::Image image;
    vk::ImageView view;
    VkExtent2D extent{};
    VkImageLayout layout{VK_IMAGE_LAYOUT_UNDEFINED};
};

class LsfgBarriers {
public:
    explicit LsfgBarriers(vk::CommandBuffer cmdbuf_) : cmdbuf{cmdbuf_} {}

    LsfgBarriers& WriteToRead(LsfgImage& image);
    LsfgBarriers& ReadToWrite(LsfgImage& image);

    template <typename Range>
    LsfgBarriers& WriteToReadAll(Range& images) {
        for (auto& image : images) {
            WriteToRead(image);
        }
        return *this;
    }

    template <typename Range>
    LsfgBarriers& ReadToWriteAll(Range& images) {
        for (auto& image : images) {
            ReadToWrite(image);
        }
        return *this;
    }

    void Build();

private:
    LsfgBarriers& Push(LsfgImage& image, VkAccessFlags src_access, VkAccessFlags dst_access);

    vk::CommandBuffer cmdbuf;
    std::vector<VkImageMemoryBarrier> barriers;
};

class LsfgDescriptorWriter {
public:
    explicit LsfgDescriptorWriter(VkDescriptorSet set_) : set{set_} {}

    LsfgDescriptorWriter& AddSampler(VkSampler sampler);
    LsfgDescriptorWriter& AddSampledImage(const LsfgImage& image);
    LsfgDescriptorWriter& AddStorageImage(const LsfgImage& image);
    LsfgDescriptorWriter& AddUniformBuffer(VkBuffer buffer, VkDeviceSize size);

    template <typename Range>
    LsfgDescriptorWriter& AddSampledImages(const Range& images) {
        for (const auto& image : images) {
            AddSampledImage(image);
        }
        return *this;
    }

    template <typename Range>
    LsfgDescriptorWriter& AddStorageImages(const Range& images) {
        for (const auto& image : images) {
            AddStorageImage(image);
        }
        return *this;
    }

    void Build(const Device& device);

private:
    LsfgDescriptorWriter& PushImage(VkDescriptorType type, VkSampler sampler, VkImageView view);

    VkDescriptorSet set;
    u32 binding{};
    std::deque<VkDescriptorImageInfo> image_infos;
    std::deque<VkDescriptorBufferInfo> buffer_infos;
    std::vector<VkWriteDescriptorSet> writes;
};

using LsfgBindings = std::initializer_list<std::pair<u32, VkDescriptorType>>;

class LsfgPass {
public:
    LsfgPass() = default;
    LsfgPass(const Device& device, const LsfgShaders& shaders, u32 shader_id,
             LsfgBindings bindings);

    [[nodiscard]] VkDescriptorSetLayout SetLayout() const {
        return *descriptor_set_layout;
    }

    [[nodiscard]] u32 DescriptorCount() const {
        return descriptor_count;
    }

    void Bind(vk::CommandBuffer cmdbuf, VkDescriptorSet set) const;

private:
    vk::DescriptorSetLayout descriptor_set_layout;
    vk::PipelineLayout pipeline_layout;
    vk::Pipeline pipeline;
    u32 descriptor_count{};
};

[[nodiscard]] vk::DescriptorPool CreateLsfgDescriptorPool(const Device& device, u32 max_sets);

[[nodiscard]] vk::Sampler CreateLsfgSampler(const Device& device, VkSamplerAddressMode address_mode,
                                            VkCompareOp compare_op, bool white_border);

} // namespace Vulkan
