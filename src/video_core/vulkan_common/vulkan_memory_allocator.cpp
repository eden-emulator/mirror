// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/common_types.h"
#include "common/literals.h"
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

    [[nodiscard]] VkMemoryPropertyFlags MemoryUsageRequiredVmaFlags(MemoryUsage usage) {
        if (usage == MemoryUsage::Upload || usage == MemoryUsage::Download) {
            return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        }
        return VkMemoryPropertyFlagBits{};
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

    template <typename F>
    [[nodiscard]] VkResult AllocateRelaxing(VmaAllocationCreateInfo& ci, F&& allocate) {
        VkResult result = allocate(ci);
        if (result == VK_SUCCESS) {
            return result;
        }
        if ((ci.flags & VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT) != 0) {
            ci.flags &= ~VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT;
            result = allocate(ci);
            if (result == VK_SUCCESS) {
                return result;
            }
        }
        if ((ci.preferredFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0) {
            ci.preferredFlags &= ~VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            result = allocate(ci);
        }
        return result;
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

vk::Image MemoryAllocator::CreateImage(const VkImageCreateInfo &ci) const
{
    VmaAllocationCreateInfo alloc_ci = {
            .flags = VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
            .requiredFlags = 0,
            .preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            .memoryTypeBits = valid_memory_types,
            .pool = VK_NULL_HANDLE,
            .pUserData = nullptr,
            .priority = 0.f,
    };

    VkImage handle{};
    VmaAllocation allocation{};
    VmaAllocationInfo alloc_info{};
    vk::Check(AllocateRelaxing(alloc_ci, [&](const VmaAllocationCreateInfo& retry) {
        return vmaCreateImage(allocator, &ci, &retry, &handle, &allocation, &alloc_info);
    }));

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
    VmaAllocationCreateInfo alloc_ci = {
        .flags = VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT | MemoryUsageVmaFlags(usage),
        .usage = MemoryUsageVma(usage),
        .requiredFlags = MemoryUsageRequiredVmaFlags(usage),
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

    vk::Check(AllocateRelaxing(alloc_ci, [&](const VmaAllocationCreateInfo& retry) {
        return vmaCreateBuffer(allocator, &ci, &retry, &handle, &allocation, &alloc_info);
    }));
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

    const vk::MemoryLocation location{
        .memory = alloc_info.deviceMemory,
        .offset = alloc_info.offset,
        .memory_type = alloc_info.memoryType,
    };
    return vk::Buffer(handle, *device.GetLogical(), allocator, allocation, mapped_data, is_coherent,
                      location, device.GetDispatchLoader());
}

vk::Buffer MemoryAllocator::CreateBuffer(const VkBufferCreateInfo &ci, MemoryUsage usage,
                                         VkDeviceSize min_alignment) const {
    if (min_alignment <= 1) {
        return CreateBuffer(ci, usage);
    }
    VkMemoryPropertyFlags anv_flags = 0;
    if (usage == MemoryUsage::Stream &&
        device.GetDriverID() == VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA) {
        anv_flags = VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    }
    u32 memory_type_bits = valid_memory_types;
    if (usage == MemoryUsage::Stream) {
        memory_type_bits = 0u;
    }
    VmaAllocationCreateInfo alloc_ci = {
        .flags = VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT | MemoryUsageVmaFlags(usage),
        .usage = MemoryUsageVma(usage),
        .requiredFlags = MemoryUsageRequiredVmaFlags(usage),
        .preferredFlags = MemoryUsagePreferredVmaFlags(usage) | anv_flags,
        .memoryTypeBits = memory_type_bits,
        .pool = VK_NULL_HANDLE,
        .pUserData = nullptr,
        .priority = 0.f,
    };

    VkBuffer handle{};
    VmaAllocationInfo alloc_info{};
    VmaAllocation allocation{};
    VkMemoryPropertyFlags property_flags{};

    vk::Check(AllocateRelaxing(alloc_ci, [&](const VmaAllocationCreateInfo& retry) {
        return vmaCreateBufferWithAlignment(allocator, &ci, &retry, min_alignment, &handle,
                                            &allocation, &alloc_info);
    }));
    vmaGetAllocationMemoryProperties(allocator, allocation, &property_flags);

    u8 *data = reinterpret_cast<u8 *>(alloc_info.pMappedData);
    std::span<u8> mapped_data{};
    if (data) {
        mapped_data = std::span<u8>{data, ci.size};
    }
    const bool is_coherent = (property_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;

    const vk::MemoryLocation location{
        .memory = alloc_info.deviceMemory,
        .offset = alloc_info.offset,
        .memory_type = alloc_info.memoryType,
    };
    return vk::Buffer(handle, *device.GetLogical(), allocator, allocation, mapped_data, is_coherent,
                      location, device.GetDispatchLoader());
}

} // namespace Vulkan
