// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include "common/alignment.h"
#include "common/assert.h"
#include "common/logging.h"
#include "video_core/renderer_vulkan/vk_descriptor_buffer.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/vulkan_common/vulkan_device.h"

namespace Vulkan {

DescriptorBufferRing::DescriptorBufferRing(const Device& device_,
                                           MemoryAllocator& memory_allocator)
    : device{device_} {
    if (!device.IsExtDescriptorBufferSupported() || !device.IsBufferDeviceAddressSupported()) {
        return;
    }
    alignment = std::max<VkDeviceSize>(
        device.DescriptorBufferProperties().descriptorBufferOffsetAlignment, 1);
    const VkDeviceSize total = FRAME_SIZE * FRAMES_IN_FLIGHT + alignment;
    const VkBufferCreateInfo buffer_ci{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size = total,
        .usage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT |
                 VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT |
                 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
    };
    buffer = memory_allocator.CreateBuffer(buffer_ci, MemoryUsage::Upload);
    if (!buffer.IsHostVisible()) {
        LOG_WARNING(Render_Vulkan, "Descriptor buffer is not host visible, disabling");
        buffer.reset();
        return;
    }
    if (device.HasDebuggingToolAttached()) {
        buffer.SetObjectNameEXT("Descriptor buffer");
    }
    const VkDeviceAddress raw_address = device.GetLogical().GetBufferDeviceAddress(*buffer);
    base_address = Common::AlignUp(raw_address, alignment);
    base_host = buffer.Mapped().data() + (base_address - raw_address);
}

DescriptorBufferRing::~DescriptorBufferRing() = default;

void DescriptorBufferRing::TickFrame() {
    if (++frame_index >= FRAMES_IN_FLIGHT) {
        frame_index = 0;
    }
    frame_start = static_cast<VkDeviceSize>(frame_index) * FRAME_SIZE;
    cursor = 0;
    frame_reused = true;
}

DescriptorBufferRing::Allocation DescriptorBufferRing::Allocate(Scheduler& scheduler,
                                                                VkDeviceSize size) {
    ASSERT(buffer);
    const VkDeviceSize needed = Common::AlignUp(size, alignment);
    if (needed > FRAME_SIZE) {
        LOG_ERROR(Render_Vulkan, "Descriptor set of {} bytes exceeds frame capacity {}", needed,
                  FRAME_SIZE);
        return Allocation{};
    }
    if (frame_reused) {
        frame_reused = false;
        scheduler.Wait(frame_ticks[frame_index]);
    }
    if (cursor + needed > FRAME_SIZE) {
        LOG_WARNING(Render_Vulkan, "Descriptor buffer frame exhausted, stalling on the GPU");
        scheduler.Finish();
        cursor = 0;
    }
    const VkDeviceSize offset = frame_start + cursor;
    cursor += needed;
    frame_ticks[frame_index] = scheduler.CurrentTick();
    return Allocation{
        .host = base_host + offset,
        .offset = offset,
    };
}

VkDescriptorBufferBindingInfoEXT DescriptorBufferRing::BindingInfo() const noexcept {
    return VkDescriptorBufferBindingInfoEXT{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT,
        .pNext = nullptr,
        .address = base_address,
        .usage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT |
                 VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT,
    };
}

} // namespace Vulkan
