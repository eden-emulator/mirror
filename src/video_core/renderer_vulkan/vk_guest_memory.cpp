// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cstring>

#include "video_core/renderer_vulkan/vk_guest_memory.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"

namespace Vulkan {
namespace {
constexpr size_t PROBE_WORDS = 1024;
constexpr size_t PROBE_SIZE = PROBE_WORDS * sizeof(u32);
constexpr u32 PROBE_CPU_STRIDE = 0x9E3779B9U;
constexpr u32 PROBE_GPU_VALUE = 0x5AC3E10FU;
}

GuestMemory::GuestMemory([[maybe_unused]] const Device& device,
                         MemoryAllocator& memory_allocator, Scheduler& scheduler,
                         [[maybe_unused]] const Common::HostMemory& host_memory) {
    if (!windows.empty() && !IsCoherent(memory_allocator, scheduler)) {
        windows.clear();
    }
}

std::optional<GuestMemory::Range> GuestMemory::Find(const u8* pointer, size_t size) const {
    if (windows.empty() || pointer < base) {
        return std::nullopt;
    }
    const size_t offset = static_cast<size_t>(pointer - base);
    const size_t index = offset / window_size;
    const size_t local_offset = offset % window_size;
    if (index >= windows.size() || window_size - local_offset < size) {
        return std::nullopt;
    }
    return Range{*windows[index].buffer, local_offset};
}

bool GuestMemory::IsCoherent(MemoryAllocator& memory_allocator, Scheduler& scheduler) const {
    const VkDeviceSize probe_offset = window_size - PROBE_SIZE;
    u8* const probe = base + (windows.size() - 1) * window_size + probe_offset;
    std::array<u8, PROBE_SIZE> saved;
    std::memcpy(saved.data(), probe, PROBE_SIZE);
    std::array<u32, PROBE_WORDS> pattern;
    for (size_t index = 0; index < PROBE_WORDS; ++index) {
        pattern[index] = static_cast<u32>(index + 1) * PROBE_CPU_STRIDE;
    }
    std::memcpy(probe, pattern.data(), PROBE_SIZE);

    vk::Buffer readback = memory_allocator.CreateBuffer(
        VkBufferCreateInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .size = PROBE_SIZE,
            .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
        },
        MemoryUsage::Download);
    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([src = *windows.back().buffer, dst = *readback,
                      probe_offset](vk::CommandBuffer cmdbuf) {
        static constexpr VkMemoryBarrier2 READ_BEFORE_WRITE{
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
            .pNext = nullptr,
            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        };
        static constexpr VkMemoryBarrier2 WRITE_TO_HOST{
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
            .pNext = nullptr,
            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
            .dstAccessMask = VK_ACCESS_2_HOST_READ_BIT,
        };
        const VkBufferCopy2 copy{
            .sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2,
            .pNext = nullptr,
            .srcOffset = probe_offset,
            .dstOffset = 0,
            .size = PROBE_SIZE,
        };
        cmdbuf.CopyBuffer(src, dst, copy);
        cmdbuf.PipelineBarrier(READ_BEFORE_WRITE);
        cmdbuf.FillBuffer(src, probe_offset, PROBE_SIZE, PROBE_GPU_VALUE);
        cmdbuf.PipelineBarrier(WRITE_TO_HOST);
    });
    scheduler.Finish();

    const bool gpu_sees_cpu =
        std::memcmp(readback.Mapped().data(), pattern.data(), PROBE_SIZE) == 0;
    std::array<u32, PROBE_WORDS> written;
    std::memcpy(written.data(), probe, PROBE_SIZE);
    const bool cpu_sees_gpu =
        std::ranges::all_of(written, [](u32 word) { return word == PROBE_GPU_VALUE; });
    std::memcpy(probe, saved.data(), PROBE_SIZE);
    return gpu_sees_cpu && cpu_sees_gpu;
}

}
