// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <bit>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include "common/alignment.h"
#include "common/assert.h"
#include "common/common_types.h"
#include "common/literals.h"
#include "common/logging.h"
#include <ranges>
#include "video_core/vulkan_common/vma.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
#include "video_core/gpu_logging/gpu_logging.h"
#include "common/settings.h"

namespace Vulkan {
    namespace {

// Helpers translating MemoryUsage to flags/usage

        [[nodiscard]] VkMemoryPropertyFlags MemoryUsagePreferredVmaFlags(MemoryUsage usage) {
            if (usage == MemoryUsage::Download) {
                return VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            }
            return usage != MemoryUsage::DeviceLocal ? VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                                                     : VkMemoryPropertyFlagBits{};
        }

        [[nodiscard]] VmaAllocationCreateFlags MemoryUsageVmaFlags(MemoryUsage usage) {
            switch (usage) {
                case MemoryUsage::Upload:
                case MemoryUsage::Stream:
                    return VMA_ALLOCATION_CREATE_MAPPED_BIT |
                           VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
                case MemoryUsage::Download:
                    return VMA_ALLOCATION_CREATE_MAPPED_BIT |
                           VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
                case MemoryUsage::DeviceLocal:
                    return {};
            }
            return {};
        }

        [[nodiscard]] VmaMemoryUsage MemoryUsageVma(MemoryUsage usage) {
            switch (usage) {
                case MemoryUsage::DeviceLocal:
                case MemoryUsage::Stream:
                    return VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
                case MemoryUsage::Upload:
                case MemoryUsage::Download:
                    return VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
            }
            return VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        }


    } // namespace

    MemoryAllocator::MemoryAllocator(const Device &device_)
            : device{device_}, allocator{device.GetAllocator()},
              properties{device_.GetPhysical().GetMemoryProperties().memoryProperties} {

        // Preserve the previous "RenderDoc small heap" trimming behavior that we had in original vma minus the heap bug
        if (device.HasDebuggingToolAttached())
        {
            using namespace Common::Literals;
            ForEachDeviceLocalHostVisibleHeap(device, [this](size_t heap_idx, VkMemoryHeap &heap) {
                if (heap.size <= 256_MiB) {
                    for (u32 t = 0; t < properties.memoryTypeCount; ++t) {
                        if (properties.memoryTypes[t].heapIndex == heap_idx) {
                            valid_memory_types &= ~(1u << t);
                        }
                    }
                }
            });
        }
    }

    MemoryAllocator::~MemoryAllocator() = default;

    void MemoryAllocator::SetReclaimCallback(ReclaimCallback callback) {
        reclaim_callback = std::move(callback);
        vk::SetAllocatorOwnerThread();
    }

    bool MemoryAllocator::ReclaimAtLeast(u64 hint_bytes) const {
        if (!reclaim_callback || in_reclaim) {
            return false;
        }
        in_reclaim = true;
        const u64 freed = reclaim_callback(hint_bytes);
        in_reclaim = false;
        return freed > 0;
    }

    vk::Image MemoryAllocator::CreateImage(const VkImageCreateInfo &ci) const
    {
        const VmaAllocationCreateInfo alloc_ci = {
                .flags = VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT,
                .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
                .requiredFlags = 0,
                .preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                .memoryTypeBits = 0,
                .pool = VK_NULL_HANDLE,
                .pUserData = nullptr,
                .priority = 0.f,
        };

        VkImage handle{};
        VmaAllocation allocation{};
        VmaAllocationInfo alloc_info{};
        DEBUG_ASSERT(vk::OnAllocatorOwnerThread());

        VkResult res = vmaCreateImage(allocator, &ci, &alloc_ci, &handle, &allocation, &alloc_info);

        if (res != VK_SUCCESS && ReclaimAtLeast(IMAGE_RECLAIM_HINT)) {
            res = vmaCreateImage(allocator, &ci, &alloc_ci, &handle, &allocation, &alloc_info);
        }

        if (res != VK_SUCCESS) {
            auto relaxed_ci = alloc_ci;
            relaxed_ci.flags &= ~VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT;
            res = vmaCreateImage(allocator, &ci, &relaxed_ci, &handle, &allocation, &alloc_info);

            if (res != VK_SUCCESS) {
                relaxed_ci.preferredFlags &= ~VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
                res = vmaCreateImage(allocator, &ci, &relaxed_ci, &handle, &allocation, &alloc_info);
            }
        }

        vk::Check(res);

        // Log GPU memory allocation for images
        if (GPU::Logging::IsActive() &&
            Settings::values.gpu_log_memory_tracking.GetValue()) {
            GPU::Logging::GPULogger::GetInstance().LogMemoryAllocation(
                reinterpret_cast<uintptr_t>(alloc_info.deviceMemory),
                static_cast<u64>(alloc_info.size),
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
            );
        }

        return vk::Image(handle, ci.usage, *device.GetLogical(), allocator, allocation,
                         device.GetDispatchLoader());
    }

    vk::Buffer MemoryAllocator::CreateBuffer(const VkBufferCreateInfo &ci, MemoryUsage usage) const {
        // MESA will do memcpy() if not marked as host cached, so just force mark it for most buffers
        auto const anv_flags = (usage == MemoryUsage::Stream
            && device.GetDriverID() == VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA)
            ? VK_MEMORY_PROPERTY_HOST_CACHED_BIT : 0;
        const VmaAllocationCreateInfo alloc_ci = {
            .flags = VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT | MemoryUsageVmaFlags(usage),
            .usage = MemoryUsageVma(usage),
            .requiredFlags = 0,
            .preferredFlags = MemoryUsagePreferredVmaFlags(usage) | anv_flags,
            .memoryTypeBits = usage == MemoryUsage::Stream ? 0u : valid_memory_types,
            .pool = VK_NULL_HANDLE,
            .pUserData = nullptr,
            .priority = 0.f,
        };

        VkBuffer handle{};
        VmaAllocationInfo alloc_info{};
        VmaAllocation allocation{};
        VkMemoryPropertyFlags property_flags{};

        DEBUG_ASSERT(vk::OnAllocatorOwnerThread());

        VkResult res = vmaCreateBuffer(allocator, &ci, &alloc_ci, &handle, &allocation, &alloc_info);

        if (res != VK_SUCCESS && ReclaimAtLeast(ci.size)) {
            res = vmaCreateBuffer(allocator, &ci, &alloc_ci, &handle, &allocation, &alloc_info);
        }

        if (res != VK_SUCCESS) {
            auto relaxed_ci = alloc_ci;
            relaxed_ci.flags &= ~VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT;
            res = vmaCreateBuffer(allocator, &ci, &relaxed_ci, &handle, &allocation, &alloc_info);

            if (res != VK_SUCCESS &&
                (relaxed_ci.preferredFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                relaxed_ci.preferredFlags &= ~VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
                res = vmaCreateBuffer(allocator, &ci, &relaxed_ci, &handle, &allocation,
                                      &alloc_info);
            }
        }

        vk::Check(res);
        vmaGetAllocationMemoryProperties(allocator, allocation, &property_flags);

        // Log GPU memory allocation for buffers
        if (GPU::Logging::IsActive() &&
            Settings::values.gpu_log_memory_tracking.GetValue()) {
            GPU::Logging::GPULogger::GetInstance().LogMemoryAllocation(
                reinterpret_cast<uintptr_t>(alloc_info.deviceMemory),
                static_cast<u64>(alloc_info.size),
                property_flags
            );
        }

        u8 *data = reinterpret_cast<u8 *>(alloc_info.pMappedData);
        const std::span<u8> mapped_data = data ? std::span<u8>{data, ci.size} : std::span<u8>{};
        const bool is_coherent = (property_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;

        return vk::Buffer(handle, *device.GetLogical(), allocator, allocation, mapped_data,
                          is_coherent,
                          device.GetDispatchLoader());
    }

} // namespace Vulkan
