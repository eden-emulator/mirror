// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstddef>

#include "video_core/renderer_vulkan/vk_command_pool.h"
#include "video_core/renderer_vulkan/vk_master_semaphore.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

constexpr size_t COMMAND_BUFFER_POOL_SIZE = 4;

struct CommandPool::Pool {
    vk::CommandPool handle;
    vk::CommandBuffers cmdbufs;
    u64 tick;
};

CommandPool::CommandPool(MasterSemaphore& master_semaphore_, const Device& device_)
    : master_semaphore{master_semaphore_}, device{device_} {}

CommandPool::~CommandPool() = default;

void CommandPool::AllocatePool() {
    Pool& pool = pools.emplace_back();
    pool.handle = device.GetLogical().CreateCommandPool({
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = device.GetGraphicsFamily(),
    });
    pool.cmdbufs = pool.handle.Allocate(COMMAND_BUFFER_POOL_SIZE);
    pool.tick = 0;
}

void CommandPool::AcquirePool() {
    if (!pools.empty()) {
        master_semaphore.Refresh();
        const u64 gpu_tick = master_semaphore.KnownGpuTick();
        for (size_t i = 0; i < pools.size(); ++i) {
            const size_t candidate = (current_pool + 1 + i) % pools.size();
            if (gpu_tick >= pools[candidate].tick) {
                current_pool = candidate;
                current_index = 0;
                pools[current_pool].handle.Reset();
                return;
            }
        }
    }
    AllocatePool();
    current_pool = pools.size() - 1;
    current_index = 0;
}

VkCommandBuffer CommandPool::Commit() {
    if (pools.empty() || current_index >= COMMAND_BUFFER_POOL_SIZE) {
        AcquirePool();
    }
    Pool& pool = pools[current_pool];
    pool.tick = master_semaphore.CurrentTick();
    return pool.cmdbufs[current_index++];
}

} // namespace Vulkan
