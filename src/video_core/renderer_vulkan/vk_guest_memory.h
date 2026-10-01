// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <vector>

#include "common/common_types.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Common {
class HostMemory;
}

namespace Vulkan {

class Device;
class MemoryAllocator;
class Scheduler;

class GuestMemory {
public:
    struct Range {
        VkBuffer buffer;
        VkDeviceSize offset;
    };

    explicit GuestMemory(const Device& device, MemoryAllocator& memory_allocator,
                         Scheduler& scheduler, const Common::HostMemory& host_memory);

    [[nodiscard]] std::optional<Range> Find(const u8* pointer, size_t size) const;

    [[nodiscard]] bool Empty() const noexcept {
        return windows.empty();
    }

private:
    struct Window {
        vk::DeviceMemory memory;
        vk::ExternalBuffer buffer;
    };

    [[nodiscard]] bool IsCoherent(MemoryAllocator& memory_allocator, Scheduler& scheduler) const;

    std::vector<Window> windows;
    u8* base{};
    size_t window_size{};
};

}
