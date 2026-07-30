// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>

#include "common/common_types.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

class Device;
class Scheduler;

class DescriptorBufferRing final {
    static constexpr size_t FRAMES_IN_FLIGHT = 8;
    static constexpr VkDeviceSize FRAME_SIZE = 512 * 1024;

public:
    explicit DescriptorBufferRing(const Device& device_, MemoryAllocator& memory_allocator);
    ~DescriptorBufferRing();

    struct Allocation {
        u8* host{};
        VkDeviceSize offset{};
    };

    [[nodiscard]] static constexpr VkDeviceSize MaxAllocationSize() noexcept {
        return FRAME_SIZE;
    }

    void TickFrame();

    [[nodiscard]] Allocation Allocate(Scheduler& scheduler, VkDeviceSize size);

    [[nodiscard]] VkDescriptorBufferBindingInfoEXT BindingInfo() const noexcept;

    [[nodiscard]] bool IsValid() const noexcept {
        return static_cast<bool>(buffer);
    }

private:
    const Device& device;
    vk::Buffer buffer;
    VkDeviceAddress base_address{};
    u8* base_host{};
    VkDeviceSize alignment{1};
    size_t frame_index{};
    VkDeviceSize frame_start{};
    VkDeviceSize cursor{};
    std::array<u64, FRAMES_IN_FLIGHT> frame_ticks{};
    bool frame_reused{};
};

} // namespace Vulkan
