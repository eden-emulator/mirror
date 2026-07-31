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

    HostMemoryImport::HostMemoryImport(const Device &device_, void *base, size_t size)
            : device{device_} {
        if (!device.IsExtExternalMemoryHostSupported()) {
            LOG_INFO(Render_Vulkan,
                     "Unified memory disabled, VK_EXT_external_memory_host is not supported");
            return;
        }
        const u64 alignment = device.GetMinImportedHostPointerAlignment();
        if (alignment == 0 || !Common::IsAligned(reinterpret_cast<uintptr_t>(base), alignment) ||
            !Common::IsAligned(size, alignment)) {
            LOG_INFO(Render_Vulkan,
                     "Unified memory disabled, host allocation does not satisfy alignment {}",
                     alignment);
            return;
        }
        using namespace Common::Literals;
        VkDeviceSize candidate_window = 1_GiB;
        const u64 max_buffer_size = device.GetMaxBufferSize();
        if (max_buffer_size != 0 && max_buffer_size < candidate_window) {
            candidate_window = Common::AlignDown(max_buffer_size, alignment);
        }
        if (candidate_window == 0) {
            return;
        }
        window_size = candidate_window;

        const auto &logical = device.GetLogical();
        const auto memory_props = device.GetPhysical().GetMemoryProperties().memoryProperties;
        const auto find_type = [&](u32 type_mask, VkMemoryPropertyFlags wanted)
                -> std::optional<u32> {
            for (u32 i = 0; i < memory_props.memoryTypeCount; ++i) {
                if (((type_mask >> i) & 1u) != 0 &&
                    (memory_props.memoryTypes[i].propertyFlags & wanted) == wanted) {
                    return i;
                }
            }
            return std::nullopt;
        };

        for (size_t offset = 0; offset < size; offset += window_size) {
            u8 *const window_base = static_cast<u8 *>(base) + offset;
            const VkDeviceSize window_len =
                    (std::min)(static_cast<VkDeviceSize>(size - offset), window_size);
            VkMemoryHostPointerPropertiesEXT host_props{
                    .sType = VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT,
                    .pNext = nullptr,
                    .memoryTypeBits = 0,
            };
            if (logical.GetMemoryHostPointerPropertiesEXT(
                        VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT, window_base,
                        &host_props) != VK_SUCCESS ||
                host_props.memoryTypeBits == 0) {
                break;
            }
            const VkExternalMemoryBufferCreateInfo external_info{
                    .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,
                    .pNext = nullptr,
                    .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,
            };
            const VkBufferCreateInfo buffer_ci{
                    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                    .pNext = &external_info,
                    .flags = 0,
                    .size = window_len,
                    .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                    .queueFamilyIndexCount = 0,
                    .pQueueFamilyIndices = nullptr,
            };
            VkBuffer new_buffer{};
            if (logical.CreateBufferRaw(buffer_ci, &new_buffer) != VK_SUCCESS) {
                break;
            }
            const VkMemoryRequirements requirements =
                    logical.GetBufferMemoryRequirements(new_buffer);
            const u32 type_mask = requirements.memoryTypeBits & host_props.memoryTypeBits;
            if (type_mask == 0 || requirements.size > window_len) {
                logical.DestroyBufferRaw(new_buffer);
                break;
            }
            auto type_index = find_type(type_mask, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                                           VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
            if (!type_index) {
                type_index = find_type(type_mask, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            }
            if (!type_index) {
                logical.DestroyBufferRaw(new_buffer);
                break;
            }
            const VkImportMemoryHostPointerInfoEXT import_info{
                    .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT,
                    .pNext = nullptr,
                    .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,
                    .pHostPointer = window_base,
            };
            const VkMemoryAllocateInfo alloc_info{
                    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                    .pNext = &import_info,
                    .allocationSize = window_len,
                    .memoryTypeIndex = *type_index,
            };
            vk::DeviceMemory memory = logical.TryAllocateMemory(alloc_info);
            if (!memory) {
                logical.DestroyBufferRaw(new_buffer);
                break;
            }
            if (logical.BindBufferMemory(new_buffer, *memory, 0) != VK_SUCCESS) {
                logical.DestroyBufferRaw(new_buffer);
                break;
            }
            windows.push_back(Window{
                    .memory = std::move(memory),
                    .buffer = new_buffer,
            });
            imported_size += static_cast<size_t>(window_len);
        }
        if (windows.empty()) {
            LOG_INFO(Render_Vulkan, "Unified memory disabled, host memory import failed");
            return;
        }
        LOG_INFO(Render_Vulkan,
                 "Imported {} MiB of guest memory for unified memory access in {} windows",
                 imported_size >> 20, windows.size());
    }

    HostMemoryImport::~HostMemoryImport() {
        for (Window &window : windows) {
            if (window.buffer != VK_NULL_HANDLE) {
                device.GetLogical().DestroyBufferRaw(window.buffer);
            }
        }
    }

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
                .flags = device.CanReportMemoryUsage() ? VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT : 0u,
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
            .flags = (device.CanReportMemoryUsage() ? VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT : 0u) |
                     MemoryUsageVmaFlags(usage),
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
