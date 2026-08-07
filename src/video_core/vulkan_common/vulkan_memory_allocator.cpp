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

#ifdef __ANDROID__
#include <android/hardware_buffer.h>
#endif

namespace Vulkan {
    namespace {

        [[nodiscard]] std::optional<u32> FindImportMemoryType(
                const VkPhysicalDeviceMemoryProperties &props, u32 type_mask) {
            const auto find = [&](VkMemoryPropertyFlags wanted) -> std::optional<u32> {
                for (u32 i = 0; i < props.memoryTypeCount; ++i) {
                    if (((type_mask >> i) & 1u) != 0 &&
                        (props.memoryTypes[i].propertyFlags & wanted) == wanted) {
                        return i;
                    }
                }
                return std::nullopt;
            };
            auto type_index = find(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                   VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
            if (!type_index) {
                type_index = find(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            }
            return type_index;
        }

// Helpers translating MemoryUsage to flags/usage

        [[maybe_unused]] VkMemoryPropertyFlags MemoryUsagePropertyFlags(MemoryUsage usage) {
            switch (usage) {
                case MemoryUsage::DeviceLocal:
                    return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
                case MemoryUsage::Upload:
                    return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
                case MemoryUsage::Download:
                    return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                           VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
                case MemoryUsage::Stream:
                    return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            }
            ASSERT_MSG(false, "Invalid memory usage={}", usage);
            return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        }

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


// This avoids calling vkGetBufferMemoryRequirements* directly.
        template<typename T>
        static VkBuffer GetVkHandleFromBuffer(const T &buf) {
            if constexpr (requires { static_cast<VkBuffer>(buf); }) {
                return static_cast<VkBuffer>(buf);
            } else if constexpr (requires {{ buf.GetHandle() } -> std::convertible_to<VkBuffer>; }) {
                return buf.GetHandle();
            } else if constexpr (requires {{ buf.Handle() } -> std::convertible_to<VkBuffer>; }) {
                return buf.Handle();
            } else if constexpr (requires {{ buf.vk_handle() } -> std::convertible_to<VkBuffer>; }) {
                return buf.vk_handle();
            } else {
                static_assert(sizeof(T) == 0, "Cannot extract VkBuffer handle from vk::Buffer");
                return VK_NULL_HANDLE;
            }
        }

    } // namespace

//MemoryCommit is now VMA-backed
    MemoryCommit::MemoryCommit(VmaAllocator alloc, VmaAllocation a,
                               const VmaAllocationInfo &info) noexcept
            : allocator{alloc}, allocation{a}, memory{info.deviceMemory},
              offset{info.offset}, size{info.size}, mapped_ptr{info.pMappedData} {
        // Log GPU memory allocation
        if (GPU::Logging::IsActive() &&
            Settings::values.gpu_log_memory_tracking.GetValue()) {
            GPU::Logging::GPULogger::GetInstance().LogMemoryAllocation(
                reinterpret_cast<uintptr_t>(memory),
                static_cast<u64>(size),
                0  // Memory property flags (not easily available from VMA)
            );
        }
    }

    MemoryCommit::~MemoryCommit() { Release(); }

    MemoryCommit::MemoryCommit(MemoryCommit &&rhs) noexcept
            : allocator{std::exchange(rhs.allocator, nullptr)},
              allocation{std::exchange(rhs.allocation, nullptr)},
              memory{std::exchange(rhs.memory, VK_NULL_HANDLE)},
              offset{std::exchange(rhs.offset, 0)},
              size{std::exchange(rhs.size, 0)},
              mapped_ptr{std::exchange(rhs.mapped_ptr, nullptr)} {}

    MemoryCommit &MemoryCommit::operator=(MemoryCommit &&rhs) noexcept {
        if (this != &rhs) {
            Release();
            allocator = std::exchange(rhs.allocator, nullptr);
            allocation = std::exchange(rhs.allocation, nullptr);
            memory = std::exchange(rhs.memory, VK_NULL_HANDLE);
            offset = std::exchange(rhs.offset, 0);
            size = std::exchange(rhs.size, 0);
            mapped_ptr = std::exchange(rhs.mapped_ptr, nullptr);
        }
        return *this;
    }

    std::span<u8> MemoryCommit::Map()
    {
        if (!allocation) return {};
        if (!mapped_ptr) {
            if (vmaMapMemory(allocator, allocation, &mapped_ptr) != VK_SUCCESS) return {};
        }
        const size_t n = static_cast<size_t>(std::min<VkDeviceSize>(size,
                                                                    (std::numeric_limits<size_t>::max)()));
        return std::span<u8>{static_cast<u8 *>(mapped_ptr), n};
    }

    std::span<const u8> MemoryCommit::Map() const
    {
        if (!allocation) return {};
        if (!mapped_ptr) {
            void *p = nullptr;
            if (vmaMapMemory(allocator, allocation, &p) != VK_SUCCESS) return {};
            const_cast<MemoryCommit *>(this)->mapped_ptr = p;
        }
        const size_t n = static_cast<size_t>(std::min<VkDeviceSize>(size,
                                                                    (std::numeric_limits<size_t>::max)()));
        return std::span<const u8>{static_cast<const u8 *>(mapped_ptr), n};
    }

    void MemoryCommit::Unmap()
    {
        if (allocation && mapped_ptr) {
            vmaUnmapMemory(allocator, allocation);
            mapped_ptr = nullptr;
        }
    }

    void MemoryCommit::Release() {
        if (allocation && allocator) {
            // Log GPU memory deallocation
            if (GPU::Logging::IsActive() &&
                Settings::values.gpu_log_memory_tracking.GetValue() &&
                memory != VK_NULL_HANDLE) {
                GPU::Logging::GPULogger::GetInstance().LogMemoryDeallocation(
                    reinterpret_cast<uintptr_t>(memory)
                );
            }

            if (mapped_ptr) {
                vmaUnmapMemory(allocator, allocation);
                mapped_ptr = nullptr;
            }
            vmaFreeMemory(allocator, allocation);
        }
        allocation = nullptr;
        allocator = nullptr;
        memory = VK_NULL_HANDLE;
        offset = 0;
        size = 0;
    }

    HostMemoryImport::HostMemoryImport(const Device &device_, void *base, size_t size,
                                       std::span<AHardwareBuffer *const> hardware_buffers,
                                       size_t hardware_buffer_window, size_t hardware_buffer_base)
            : device{device_} {
        if (ImportHardwareBuffers(hardware_buffers, hardware_buffer_window, hardware_buffer_base,
                                  size)) {
            return;
        }
        if (device.IsTiler()) {
            LOG_INFO(Render_Vulkan,
                     "Unified memory disabled, hardware buffer import is the only path supported "
                     "by tiler drivers");
            return;
        }
        if (ImportHostPointer(base, size)) {
            return;
        }
        LOG_INFO(Render_Vulkan, "Unified memory disabled, no host memory import path");
    }

    bool HostMemoryImport::ImportHostPointer(void *base, size_t size) {
        if (!device.IsExtExternalMemoryHostSupported()) {
            LOG_INFO(Render_Vulkan,
                     "Unified memory disabled, VK_EXT_external_memory_host is not supported");
            return false;
        }
        const u64 alignment = device.GetMinImportedHostPointerAlignment();
        if (alignment == 0 || !Common::IsAligned(reinterpret_cast<uintptr_t>(base), alignment) ||
            !Common::IsAligned(size, alignment)) {
            LOG_INFO(Render_Vulkan,
                     "Unified memory disabled, host allocation does not satisfy alignment {}",
                     alignment);
            return false;
        }
        using namespace Common::Literals;
        constexpr VkDeviceSize DesktopWindowSize = 4_GiB;
        VkDeviceSize candidate_window = DesktopWindowSize;
        const u64 max_buffer_size = device.GetMaxBufferSize();
        if (max_buffer_size != 0 && max_buffer_size < candidate_window) {
            candidate_window = max_buffer_size;
        }
        const u64 max_allocation_size = device.GetMaxMemoryAllocationSize();
        if (max_allocation_size != 0 && max_allocation_size < candidate_window) {
            candidate_window = max_allocation_size;
        }
        candidate_window = Common::AlignDown(candidate_window, alignment);
        if (candidate_window == 0) {
            return false;
        }
        window_size = candidate_window;

        const auto &logical = device.GetLogical();
        const auto memory_props = device.GetPhysical().GetMemoryProperties().memoryProperties;

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
            const auto type_index = FindImportMemoryType(memory_props, type_mask);
            if (!type_index) {
                logical.DestroyBufferRaw(new_buffer);
                break;
            }
            constexpr VkDeviceSize MaxHeapFractionDenominator = 2;
            const u32 heap_index = memory_props.memoryTypes[*type_index].heapIndex;
            const VkDeviceSize heap_size = memory_props.memoryHeaps[heap_index].size;
            const VkDeviceSize heap_import_limit = heap_size / MaxHeapFractionDenominator;
            if (imported_size + window_len > heap_import_limit) {
                LOG_INFO(Render_Vulkan,
                         "Stopping guest memory import at {} MiB to leave room on heap {} of {} MiB",
                         imported_size >> 20, heap_index, heap_size >> 20);
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
            LOG_INFO(Render_Vulkan, "Host pointer import failed");
            return false;
        }
        LOG_INFO(Render_Vulkan,
                 "Imported {} MiB of guest memory for unified memory access in {} windows",
                 imported_size >> 20, windows.size());
        return true;
    }

    bool HostMemoryImport::ImportHardwareBuffers(
            [[maybe_unused]] std::span<AHardwareBuffer *const> hardware_buffers,
            [[maybe_unused]] size_t hardware_buffer_window,
            [[maybe_unused]] size_t hardware_buffer_base, [[maybe_unused]] size_t size) {
#ifdef __ANDROID__
        if (hardware_buffers.empty() || hardware_buffer_window == 0 ||
            !device.IsExtExternalMemoryAhbSupported()) {
            return false;
        }
        using namespace Common::Literals;
        u64 max_allocation_size = device.GetMaxMemoryAllocationSize();
        if (device.IsTiler()) {
            constexpr u64 TilerAllocationLimit = 1_GiB;
            max_allocation_size = max_allocation_size != 0
                                          ? (std::min)(max_allocation_size, TilerAllocationLimit)
                                          : TilerAllocationLimit;
        }
        if (max_allocation_size != 0 && hardware_buffer_window > max_allocation_size) {
            LOG_WARNING(Render_Vulkan,
                        "Hardware buffer windows of {} MiB exceed the {} MiB allocation limit",
                        hardware_buffer_window >> 20, max_allocation_size >> 20);
            return false;
        }
        if (hardware_buffer_base >= size) {
            return false;
        }
        const auto &logical = device.GetLogical();
        const auto memory_props = device.GetPhysical().GetMemoryProperties().memoryProperties;
        window_size = hardware_buffer_window;
        base_offset = hardware_buffer_base;
        for (size_t i = 0; i < hardware_buffers.size(); ++i) {
            const size_t offset = hardware_buffer_base + i * hardware_buffer_window;
            if (offset >= size) {
                break;
            }
            const VkDeviceSize window_len = (std::min)(
                    static_cast<VkDeviceSize>(size - offset),
                    static_cast<VkDeviceSize>(hardware_buffer_window));
            VkAndroidHardwareBufferPropertiesANDROID ahb_props{
                    .sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID,
                    .pNext = nullptr,
                    .allocationSize = 0,
                    .memoryTypeBits = 0,
            };
            if (logical.GetAndroidHardwareBufferPropertiesANDROID(hardware_buffers[i],
                                                                  &ahb_props) != VK_SUCCESS ||
                ahb_props.memoryTypeBits == 0 || ahb_props.allocationSize < window_len) {
                break;
            }
            const VkExternalMemoryBufferCreateInfo external_info{
                    .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,
                    .pNext = nullptr,
                    .handleTypes =
                            VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID,
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
            const u32 type_mask = requirements.memoryTypeBits & ahb_props.memoryTypeBits;
            if (type_mask == 0 || requirements.size > ahb_props.allocationSize) {
                logical.DestroyBufferRaw(new_buffer);
                break;
            }
            const auto type_index = FindImportMemoryType(memory_props, type_mask);
            if (!type_index) {
                logical.DestroyBufferRaw(new_buffer);
                break;
            }
            const VkImportAndroidHardwareBufferInfoANDROID import_info{
                    .sType = VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID,
                    .pNext = nullptr,
                    .buffer = hardware_buffers[i],
            };
            const VkMemoryDedicatedAllocateInfo dedicated_info{
                    .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
                    .pNext = &import_info,
                    .image = VK_NULL_HANDLE,
                    .buffer = new_buffer,
            };
            const VkMemoryAllocateInfo alloc_info{
                    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                    .pNext = &dedicated_info,
                    .allocationSize = ahb_props.allocationSize,
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
            LOG_INFO(Render_Vulkan, "Hardware buffer import failed");
            window_size = 0;
            base_offset = 0;
            return false;
        }
        foreign_ownership = true;
        LOG_INFO(Render_Vulkan,
                 "Imported {} MiB of guest memory at {:#x} via hardware buffers in {} windows",
                 imported_size >> 20, base_offset, windows.size());
        return true;
#else
        return false;
#endif
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
              properties{device_.GetPhysical().GetMemoryProperties().memoryProperties},
              buffer_image_granularity{
                      device_.GetPhysical().GetProperties().limits.bufferImageGranularity} {

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
        vk::Check(vmaCreateImage(allocator, &ci, &alloc_ci, &handle, &allocation, &alloc_info));

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

        vk::Check(vmaCreateBuffer(allocator, &ci, &alloc_ci, &handle, &allocation, &alloc_info));
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

    MemoryCommit MemoryAllocator::Commit(const VkMemoryRequirements &reqs, MemoryUsage usage)
    {
        const auto vma_usage = MemoryUsageVma(usage);
        VmaAllocationCreateInfo ci{};
        ci.flags = VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT | MemoryUsageVmaFlags(usage);
        ci.usage = vma_usage;
        ci.memoryTypeBits = reqs.memoryTypeBits & valid_memory_types;
        ci.requiredFlags = 0;
        ci.preferredFlags = MemoryUsagePreferredVmaFlags(usage);

        VmaAllocation a{};
        VmaAllocationInfo info{};

        VkResult res = vmaAllocateMemory(allocator, &reqs, &ci, &a, &info);

        if (res != VK_SUCCESS) {
            // Relax 1: drop budget constraint
            auto ci2 = ci;
            ci2.flags &= ~VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT;
            res = vmaAllocateMemory(allocator, &reqs, &ci2, &a, &info);

            // Relax 2: if we preferred DEVICE_LOCAL, drop that preference
            if (res != VK_SUCCESS && (ci.preferredFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                auto ci3 = ci2;
                ci3.preferredFlags &= ~VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
                res = vmaAllocateMemory(allocator, &reqs, &ci3, &a, &info);
            }
        }

        vk::Check(res);
        return MemoryCommit(allocator, a, info);
    }

    MemoryCommit MemoryAllocator::Commit(const vk::Buffer &buffer, MemoryUsage usage) {
        // Allocate memory appropriate for this buffer automatically
        const auto vma_usage = MemoryUsageVma(usage);

        VmaAllocationCreateInfo ci{};
        ci.flags = VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT | MemoryUsageVmaFlags(usage);
        ci.usage = vma_usage;
        ci.requiredFlags = 0;
        ci.preferredFlags = MemoryUsagePreferredVmaFlags(usage);
        ci.pool = VK_NULL_HANDLE;
        ci.pUserData = nullptr;
        ci.priority = 0.0f;

        const VkBuffer raw = *buffer;

        VmaAllocation a{};
        VmaAllocationInfo info{};

        // Let VMA infer memory requirements from the buffer
        VkResult res = vmaAllocateMemoryForBuffer(allocator, raw, &ci, &a, &info);

        if (res != VK_SUCCESS) {
            auto ci2 = ci;
            ci2.flags &= ~VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT;
            res = vmaAllocateMemoryForBuffer(allocator, raw, &ci2, &a, &info);

            if (res != VK_SUCCESS && (ci.preferredFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                auto ci3 = ci2;
                ci3.preferredFlags &= ~VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
                res = vmaAllocateMemoryForBuffer(allocator, raw, &ci3, &a, &info);
            }
        }

        vk::Check(res);
        vk::Check(vmaBindBufferMemory2(allocator, a, 0, raw, nullptr));
        return MemoryCommit(allocator, a, info);
    }



} // namespace Vulkan
