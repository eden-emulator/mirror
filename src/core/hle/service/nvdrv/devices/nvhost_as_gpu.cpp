// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2021 yuzu Emulator Project
// SPDX-FileCopyrightText: 2021 Skyline Team and Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstring>
#include <limits>
#include <utility>

#include "common/alignment.h"
#include "common/assert.h"
#include "common/common_types.h"
#include "common/logging.h"
#include "core/core.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/service/nvdrv/devices/gpu_address_space_layout.h"
#include "core/hle/service/nvdrv/core/container.h"
#include "core/hle/service/nvdrv/core/nvmap.h"
#include "core/hle/service/nvdrv/devices/ioctl_serialization.h"
#include "core/hle/service/nvdrv/devices/nvhost_as_gpu.h"
#include "core/hle/service/nvdrv/devices/native_gpu_kind.h"
#include "core/hle/service/nvdrv/devices/nvhost_gpu.h"
#include "core/hle/service/nvdrv/nvdrv.h"
#include "video_core/control/channel_state.h"
#include "video_core/gpu.h"
#include "video_core/memory_manager.h"
#include "video_core/rasterizer_interface.h"

namespace Service::Nvidia::Devices {

nvhost_as_gpu::nvhost_as_gpu(Core::System& system_, Module& module_, NvCore::Container& core)
    : nvdevice{system_, Type::GpuAddressSpace}, module{module_}, container{core}, nvmap{core.GetNvMapFile()}, vm{},
      gmmu{} {}

nvhost_as_gpu::~nvhost_as_gpu() {
    for (const auto& [handle, address] : native_sparse_pins) nvmap.UnpinHandle(handle);
    if (m_owner_process) { m_owner_process->Close(system.Kernel()); }
}

NvResult nvhost_as_gpu::Ioctl1(DeviceFD fd, Ioctl command, std::span<const u8> input,
                               std::span<u8> output) {
    switch (command.group) {
    case 'A':
        switch (command.cmd) {
        case 0x1:
            return WrapFixed(this, &nvhost_as_gpu::BindChannel, input, output);
        case 0x2:
            return WrapFixed(this, &nvhost_as_gpu::AllocateSpace, input, output);
        case 0x3:
            return WrapFixed(this, &nvhost_as_gpu::FreeSpace, input, output);
        case 0x5:
            return WrapFixed(this, &nvhost_as_gpu::UnmapBuffer, input, output);
        case 0x6:
            return WrapFixed(this, &nvhost_as_gpu::MapBufferEx, input, output);
        case 0x8:
            return WrapFixed(this, &nvhost_as_gpu::GetVARegions1, input, output);
        case 0x9:
            return WrapFixed(this, &nvhost_as_gpu::AllocAsEx, input, output);
        case 0xb:
            return WrapFixed(this, &nvhost_as_gpu::GetSyncpointShimInfo, input, output);
        case 0xc:
            return WrapFixed(this, &nvhost_as_gpu::ModifyKind, input, output);
        case 0xd:
            if (!m_extended_abi) return NvResult::NotImplemented;
            return WrapVariable(this, &nvhost_as_gpu::NativeRemap, input, output);
        case 0x14:
            return WrapVariable(this, &nvhost_as_gpu::Remap, input, output);
        default:
            break;
        }
        break;
    default:
        break;
    }

    UNIMPLEMENTED_MSG("Unimplemented ioctl={:08X}", command.raw);
    return NvResult::NotImplemented;
}

NvResult nvhost_as_gpu::Ioctl2(DeviceFD fd, Ioctl command, std::span<const u8> input,
                               std::span<const u8> inline_input, std::span<u8> output) {
    UNIMPLEMENTED_MSG("Unimplemented ioctl={:08X}", command.raw);
    return NvResult::NotImplemented;
}

NvResult nvhost_as_gpu::Ioctl3(DeviceFD fd, Ioctl command, std::span<const u8> input,
                               std::span<u8> output, std::span<u8> inline_output) {
    switch (command.group) {
    case 'A':
        switch (command.cmd) {
        case 0x8:
            return WrapFixedInlOut(this, &nvhost_as_gpu::GetVARegions3, input, output,
                                   inline_output);
        default:
            break;
        }
        break;
    default:
        break;
    }
    UNIMPLEMENTED_MSG("Unimplemented ioctl={:08X}", command.raw);
    return NvResult::NotImplemented;
}

void nvhost_as_gpu::OnOpen(NvCore::SessionId session_id, DeviceFD fd) {
    m_fd = fd;
    if (const auto* session = container.GetSession(session_id)) {
        m_owner_process = session->process;
        m_owner_process->Open(system.Kernel());
        m_extended_abi = m_owner_process->UsesPAuthReturns();
    }
}
void nvhost_as_gpu::OnClose(DeviceFD fd) {}

NvResult nvhost_as_gpu::MapNative4K(u32 handle_id, u64 address, u64 buffer_offset, u64 size, u8 kind) {
    if (!m_extended_abi || !size || !Common::IsAligned(address, VM::YUZU_PAGESIZE) ||
        !Common::IsAligned(size, VM::YUZU_PAGESIZE) ||
        !Common::IsAligned(buffer_offset, VM::YUZU_PAGESIZE)) {
        return NvResult::BadParameter;
    }
    const auto handle = nvmap.GetHandle(handle_id);
    if (!handle || buffer_offset > handle->orig_size || size > handle->orig_size - buffer_offset) {
        return NvResult::BadParameter;
    }
    {
        const std::scoped_lock lock{mutex};
        if (!vm.initialised || address < vm.va_range_start || address >= vm.va_range_split ||
            size > vm.va_range_split - address) {
            return NvResult::BadParameter;
        }
        const auto next = allocation_map.lower_bound(address);
        if ((next != allocation_map.end() && next->first < address + size) ||
            (next != allocation_map.begin() &&
             std::prev(next)->first + std::prev(next)->second.size > address)) {
            return NvResult::BadParameter;
        }
    }
    IoctlAllocSpace reservation{};
    reservation.pages = static_cast<u32>(size >> VM::PAGE_SIZE_BITS);
    reservation.page_size = VM::YUZU_PAGESIZE;
    reservation.flags = MappingFlags::Fixed;
    reservation.offset = address;
    const auto reserved = AllocateSpace(reservation);
    if (reserved != NvResult::Success) { return reserved; }
    IoctlMapBufferEx mapping{};
    mapping.flags = MappingFlags::Fixed;
    mapping.kind = kind;
    mapping.handle = handle_id;
    mapping.page_size = VM::YUZU_PAGESIZE;
    mapping.buffer_offset = static_cast<s64>(buffer_offset);
    mapping.mapping_size = size;
    mapping.offset = static_cast<s64>(address);
    const auto result = MapBufferEx(mapping);
    if (result != NvResult::Success) {
        nvmap.UnpinHandle(handle_id);
        IoctlFreeSpace release{};
        release.offset = address;
        release.pages = reservation.pages;
        release.page_size = VM::YUZU_PAGESIZE;
        FreeSpace(release);
    }
    return result;
}

NvResult nvhost_as_gpu::ReserveNativeSparse4K(u64 address, u64 size) {
    if (!m_extended_abi || !size || !Common::IsAligned(address, VM::YUZU_PAGESIZE) ||
        !Common::IsAligned(size, VM::YUZU_PAGESIZE) ||
        (size >> VM::PAGE_SIZE_BITS) > std::numeric_limits<u32>::max()) {
        return NvResult::BadParameter;
    }
    {
        const std::scoped_lock lock{mutex};
        if (!vm.initialised || address < vm.va_range_start || address >= vm.va_range_split ||
            size > vm.va_range_split - address) {
            return NvResult::BadParameter;
        }
        const auto next = allocation_map.lower_bound(address);
        if ((next != allocation_map.end() && next->first < address + size) ||
            (next != allocation_map.begin() &&
             std::prev(next)->first + std::prev(next)->second.size > address)) {
            return NvResult::BadParameter;
        }
    }
    IoctlAllocSpace reservation{};
    reservation.pages = static_cast<u32>(size >> VM::PAGE_SIZE_BITS);
    reservation.page_size = VM::YUZU_PAGESIZE;
    reservation.flags = MappingFlags::Fixed | MappingFlags::Sparse;
    reservation.offset = address;
    return AllocateSpace(reservation);
}

NvResult nvhost_as_gpu::MapNativeImage(u32 handle_id, u64 buffer_offset, u64 size, u64& address) {
    const auto handle = nvmap.GetHandle(handle_id);
    if (!m_extended_abi || !handle || !handle->allocated || !size ||
        !Common::IsAligned(size, VM::YUZU_PAGESIZE) ||
        !Common::IsAligned(buffer_offset, VM::YUZU_PAGESIZE) ||
        buffer_offset > handle->orig_size || size > handle->orig_size - buffer_offset) {
        return NvResult::BadParameter;
    }
    IoctlAllocSpace reservation{};
    reservation.pages = static_cast<u32>(size >> VM::PAGE_SIZE_BITS);
    reservation.page_size = VM::YUZU_PAGESIZE;
    const auto reserved = AllocateSpace(reservation);
    if (reserved != NvResult::Success) return reserved;
    IoctlMapBufferEx mapping{};
    mapping.flags = MappingFlags::Fixed;
    mapping.handle = handle_id;
    mapping.page_size = VM::YUZU_PAGESIZE;
    mapping.buffer_offset = static_cast<s64>(buffer_offset);
    mapping.mapping_size = size;
    mapping.offset = static_cast<s64>(reservation.offset);
    const auto result = MapBufferEx(mapping);
    if (result == NvResult::Success) {
        address = reservation.offset;
    } else {
        nvmap.UnpinHandle(handle_id);
        IoctlFreeSpace release{};
        release.offset = reservation.offset;
        release.pages = reservation.pages;
        release.page_size = reservation.page_size;
        FreeSpace(release);
    }
    return result;
}

NvResult nvhost_as_gpu::UnmapNative4K(u64 address, u64 size) {
    if (!m_extended_abi || !size || !Common::IsAligned(address, VM::YUZU_PAGESIZE) ||
        !Common::IsAligned(size, VM::YUZU_PAGESIZE)) {
        return NvResult::BadParameter;
    }
    IoctlFreeSpace release{};
    release.offset = address;
    release.pages = static_cast<u32>(size >> VM::PAGE_SIZE_BITS);
    release.page_size = VM::YUZU_PAGESIZE;
    return FreeSpace(release);
}


NvResult nvhost_as_gpu::AllocAsEx(IoctlAllocAsEx& params) {
    LOG_DEBUG(Service_NVDRV, "called, flags={:#x}, as_fd={}, big_page_size={:#x}, reserved={:#x}, VA={:#x}/{:#x}/{:#x}",
              params.flags, params.as_fd, params.big_page_size, params.reserved,
              params.va_range_start, params.va_range_split, params.va_range_end);

    std::scoped_lock lock(mutex);

    if (vm.initialised) {
        ASSERT_MSG(false, "Cannot initialise an address space twice!");
        return NvResult::InvalidState;
    }

    std::shared_ptr<nvhost_as_gpu> shared_parent;
    if (m_extended_abi && params.as_fd != 0) {
        if (params.as_fd == m_fd) { return NvResult::BadParameter; }
        const auto device = module.GetDevice<nvdevice>(params.as_fd);
        if (!device || device->GetType() != Type::GpuAddressSpace) { return NvResult::BadParameter; }
        shared_parent = std::static_pointer_cast<nvhost_as_gpu>(device);
        if (!shared_parent->m_extended_abi || !shared_parent->vm.initialised ||
            shared_parent->m_owner_process != m_owner_process ||
            !IsSupportedNativeAddressSpaceView(
                params.flags, params.big_page_size, params.reserved,
                {params.va_range_start, params.va_range_split, params.va_range_end},
                {shared_parent->vm.va_range_start, shared_parent->vm.va_range_split,
                 shared_parent->vm.va_range_end})) { return NvResult::BadParameter; }
    }

    const u32 big_page_size = GetGpuAddressSpaceBigPageSize(m_extended_abi, params.big_page_size);
    if (big_page_size) {
        if (!std::has_single_bit(big_page_size)) {
            LOG_ERROR(Service_NVDRV, "Non power-of-2 big page size: {:#x}!", big_page_size);
            return NvResult::BadValue;
        }

        if ((big_page_size & VM::SUPPORTED_BIG_PAGE_SIZES) == 0) {
            LOG_ERROR(Service_NVDRV, "Unsupported big page size: {:#x}!", big_page_size);
            return NvResult::BadValue;
        }

        vm.big_page_size = big_page_size;
        vm.big_page_size_bits = static_cast<u32>(std::countr_zero(big_page_size));

        vm.va_range_start = big_page_size << VM::VA_START_SHIFT;
    }

    if (m_extended_abi && !params.va_range_start) {
        const auto layout = GetGpuAddressSpaceLayout(true);
        vm.va_range_start = layout.start;
        vm.va_range_split = layout.split;
        vm.va_range_end = layout.end;
    }

    // If this is unspecified then default values should be used
    if (params.va_range_start) {
        vm.va_range_start = params.va_range_start;
        vm.va_range_split = params.va_range_split;
        vm.va_range_end = params.va_range_end;
    }

    if (shared_parent) {
        // Both native small-page views reserve within the parent's region.
        // GPU mappings must resolve through the same page table.
        vm.va_range_split = params.va_range_end;
    }

    const u64 max_big_page_bits = Common::Log2Ceil(vm.va_range_end);

    const auto start_pages{static_cast<u32>(vm.va_range_start >> VM::PAGE_SIZE_BITS)};
    const auto end_pages{static_cast<u32>(vm.va_range_split >> VM::PAGE_SIZE_BITS)};
    vm.small_page_allocator.emplace(start_pages, end_pages);

    const auto start_big_pages{static_cast<u32>(vm.va_range_split >> vm.big_page_size_bits)};
    const auto end_big_pages{
        static_cast<u32>(vm.va_range_end >> vm.big_page_size_bits)};
    vm.big_page_allocator.emplace(start_big_pages, end_big_pages);

    if (shared_parent) {
        m_shared_parent = std::move(shared_parent);
        gmmu = m_shared_parent->gmmu;
        LOG_DEBUG(Service_NVDRV, "native AS view fd={} shares page table with fd={}", m_fd, params.as_fd);
    } else {
        gmmu = std::make_shared<Tegra::MemoryManager>(system, max_big_page_bits, vm.va_range_split, vm.big_page_size_bits, VM::PAGE_SIZE_BITS);
        system.GPU().InitAddressSpace(*gmmu);
    }
    vm.initialised = true;

    return NvResult::Success;
}

NvResult nvhost_as_gpu::AllocateSpace(IoctlAllocSpace& params) {
    LOG_DEBUG(Service_NVDRV, "called, pages={:X}, page_size={:X}, flags={:X}", params.pages,
              params.page_size, params.flags);

    std::scoped_lock lock(mutex);

    if (!vm.initialised) {
        return NvResult::BadValue;
    }

    if (params.page_size != VM::YUZU_PAGESIZE && params.page_size != vm.big_page_size) {
        return NvResult::BadValue;
    }

    if (params.page_size != vm.big_page_size &&
        ((params.flags & MappingFlags::Sparse) != MappingFlags::None) &&
        !(m_extended_abi && params.page_size == VM::YUZU_PAGESIZE)) {
        UNIMPLEMENTED_MSG("Sparse small pages are not implemented!");
        return NvResult::NotImplemented;
    }

    const u32 page_size_bits{params.page_size == VM::YUZU_PAGESIZE ? VM::PAGE_SIZE_BITS
                                                                   : vm.big_page_size_bits};

    auto& allocator{params.page_size == VM::YUZU_PAGESIZE ? *vm.small_page_allocator
                                                          : *vm.big_page_allocator};

    if ((params.flags & MappingFlags::Fixed) != MappingFlags::None) {
        allocator.AllocateFixed(static_cast<u32>(params.offset >> page_size_bits), params.pages);
    } else {
        params.offset = static_cast<u64>(allocator.Allocate(params.pages)) << page_size_bits;
        if (!params.offset) {
            ASSERT_MSG(false, "Failed to allocate free space in the GPU AS!");
            return NvResult::InsufficientMemory;
        }
    }

    u64 size{static_cast<u64>(params.pages) * params.page_size};

    if ((params.flags & MappingFlags::Sparse) != MappingFlags::None) {
        gmmu->MapSparse(params.offset, size, params.page_size != VM::YUZU_PAGESIZE);
    }

    allocation_map[params.offset] = {
        .mappings{},
        .size = size,
        .page_size = params.page_size,
        .sparse = (params.flags & MappingFlags::Sparse) != MappingFlags::None,
        .big_pages = params.page_size != VM::YUZU_PAGESIZE,
    };

    return NvResult::Success;
}

bool nvhost_as_gpu::FreeMappingLocked(u64 offset) noexcept {
    if (auto const it = mapping_map.find(offset); it != mapping_map.end()) {
        auto const mapping = it->second;
        if (!mapping.fixed) {
            auto& allocator{mapping.big_page ? *vm.big_page_allocator : *vm.small_page_allocator};
            u32 page_size_bits{mapping.big_page ? vm.big_page_size_bits : VM::PAGE_SIZE_BITS};
            u32 page_size{mapping.big_page ? vm.big_page_size : VM::YUZU_PAGESIZE};
            u64 aligned_size{Common::AlignUp(mapping.size, page_size)};
            allocator.Free(u32(mapping.offset >> page_size_bits), u32(aligned_size >> page_size_bits));
        }
        nvmap.UnpinHandle(mapping.handle);
        // Sparse mappings shouldn't be fully unmapped, just returned to their sparse state
        // Only FreeSpace can unmap them fully
        if (mapping.sparse_alloc) {
            gmmu->MapSparse(offset, mapping.size, mapping.big_page);
        } else {
            gmmu->Unmap(offset, mapping.size);
        }
        mapping_map.erase(offset);
        map_buffer_offsets.erase(static_cast<s64>(offset));
        return true;
    }
    return false;
}

NvResult nvhost_as_gpu::FreeSpace(IoctlFreeSpace& params) {
    LOG_DEBUG(Service_NVDRV, "called, offset={:X}, pages={:X}, page_size={:X}", params.offset, params.pages, params.page_size);
    std::scoped_lock lock(mutex);
    if (!vm.initialised) {
        return NvResult::BadValue;
    }
    if (auto const it = allocation_map.find(params.offset); it != allocation_map.end()) {
        auto const allocation = it->second;
        if (allocation.page_size != params.page_size || allocation.size != (u64(params.pages) * params.page_size))
            return NvResult::BadValue;

        for (const auto mapping_offset : allocation.mappings)
            if (!FreeMappingLocked(mapping_offset))
                return NvResult::BadValue;

        // Unset sparse flag if required
        if (allocation.sparse) {
            gmmu->Unmap(params.offset, allocation.size);
            native_sparse_mappings.Replace({params.offset, allocation.size, 0, 0});
            const auto retained = native_sparse_mappings.ReferencedHandles();
            for (auto pin = native_sparse_pins.begin(); pin != native_sparse_pins.end();) {
                if (!retained.contains(pin->first)) {
                    nvmap.UnpinHandle(pin->first);
                    pin = native_sparse_pins.erase(pin);
                } else ++pin;
            }
        }

        auto& allocator{params.page_size == VM::YUZU_PAGESIZE ? *vm.small_page_allocator : *vm.big_page_allocator};
        u32 page_size_bits{params.page_size == VM::YUZU_PAGESIZE ? VM::PAGE_SIZE_BITS : vm.big_page_size_bits};

        allocator.Free(u32(params.offset >> page_size_bits), u32(allocation.size >> page_size_bits));
        allocation_map.erase(params.offset);
        return NvResult::Success;
    }
    return NvResult::BadValue;
}

NvResult nvhost_as_gpu::GetNativeSyncpointShim(u64& address, u32& stride, u32& count) {
    if (!m_extended_abi || !vm.initialised) { return NvResult::NotSupported; }
    address = vm.va_range_split - VM::YUZU_PAGESIZE;
    if (!m_syncpoint_shim_reserved) {
        IoctlAllocSpace allocation{};
        allocation.pages = 1;
        allocation.page_size = VM::YUZU_PAGESIZE;
        allocation.flags = MappingFlags::Fixed;
        allocation.offset = address;
        const auto result = AllocateSpace(allocation);
        if (result != NvResult::Success) { return result; }
        m_syncpoint_shim_reserved = true;
        gmmu->SetNativeSyncpointShim(address);
    }
    stride = sizeof(u32);
    count = 192;
    return NvResult::Success;
}

NvResult nvhost_as_gpu::GetSyncpointShimInfo(IoctlSyncpointShimInfo& params) {
    u64 address{};
    u32 stride{}, count{};
    const auto result = GetNativeSyncpointShim(address, stride, count);
    params.address = address;
    params.stride = stride;
    params.count = count;
    LOG_DEBUG(Service_NVDRV, "native syncpoint shim address={:#x}, stride={}, count={}, result={}",
              address, stride, count, static_cast<u32>(result));
    return result;
}

NvResult nvhost_as_gpu::ModifyNativeKind(u64 address, u64 size, u8 requested, u8 fallback) {
    IoctlModifyKind params{};
    params.compressed_kind = requested;
    params.uncompressed_kind = fallback;
    params.mapping_base = address;
    params.size = size;
    return ModifyKind(params);
}

NvResult nvhost_as_gpu::ModifyKind(IoctlModifyKind& params) {
    const u32 requested = params.compressed_kind;
    const u32 fallback = params.uncompressed_kind;
    const u64 base = params.mapping_base;
    const u64 offset = params.buffer_offset;
    const u64 size = params.size;
    LOG_DEBUG(Service_NVDRV, "native modify kind base={:#x}, offset={:#x}, size={:#x}, kinds={:#x}/{:#x}",
              base, offset, size, requested, fallback);
    if (!m_extended_abi || params.reserved != 0 || fallback > 255 ||
        (requested > 255 && requested != 0xffff) || !size ||
        offset > std::numeric_limits<u64>::max() - base ||
        !Common::IsAligned(base + offset, VM::YUZU_PAGESIZE) ||
        !Common::IsAligned(size, VM::YUZU_PAGESIZE)) {
        return NvResult::BadParameter;
    }
    const auto kind = SelectNativeMemoryKind(static_cast<u8>(requested == 0xffff ? fallback : requested),
                                             static_cast<u8>(fallback));
    if (!kind) { return NvResult::NotSupported; }
    const std::scoped_lock lock{mutex};
    const u64 address = base + offset;
    auto it = mapping_map.upper_bound(address);
    if (!vm.initialised || it == mapping_map.begin()) { return NvResult::BadParameter; }
    --it;
    const auto& mapping = it->second;
    const u64 delta = address - it->first;
    if (delta >= mapping.size || size > mapping.size - delta) { return NvResult::BadParameter; }
    gmmu->Map(address, mapping.ptr + delta, size, static_cast<Tegra::PTEKind>(kind->backend),
              mapping.big_page);
    return NvResult::Success;
}

NvResult nvhost_as_gpu::NativeRemap(std::span<NativeSparseRemapEntry> entries) {
    const std::scoped_lock lock{mutex};
    if (!m_extended_abi || !vm.initialised || entries.empty() || entries.size() > 256) {
        return NvResult::BadParameter;
    }
    std::vector<NativeSparseRange> ranges;
    std::vector<NativeMemoryKind> kinds;
    auto updated = native_sparse_mappings;
    for (const auto& entry : entries) {
        LOG_DEBUG(Service_NVDRV, "Native remap request flags={:#x}, kinds={:#x}/{:#x}, memory={}, source_pages={:#x}, destination_pages={:#x}, pages={:#x}",
                  u32{entry.flags}, u16{entry.kind}, u16{entry.fallback_kind}, u32{entry.handle},
                  u64{entry.source_pages}, u64{entry.destination_pages}, u64{entry.pages});
        const auto range = DecodeNativeSparseRemap(entry);
        if (!range) return NvResult::BadParameter;
        auto allocation = allocation_map.upper_bound(range->address);
        if (allocation == allocation_map.begin()) return NvResult::BadParameter;
        --allocation;
        const auto& reservation = allocation->second;
        if (!reservation.sparse || reservation.big_pages || range->address < allocation->first ||
            range->address - allocation->first > reservation.size ||
            range->size > reservation.size - (range->address - allocation->first)) {
            return NvResult::BadParameter;
        }
        NativeMemoryKind kind{};
        if (range->handle) {
            const auto handle = nvmap.GetHandle(range->handle);
            const auto* session = handle ? container.GetSession(handle->session_id) : nullptr;
            if (!handle || !handle->allocated || !session || session->process != m_owner_process ||
                range->source_offset > handle->orig_size ||
                range->size > handle->orig_size - range->source_offset) {
                return NvResult::BadParameter;
            }
            const u16 requested = entry.kind == 0xffff ? u16{entry.fallback_kind} : u16{entry.kind};
            const u16 fallback = entry.fallback_kind == 0xffff ? requested : u16{entry.fallback_kind};
            if (requested > 255 || fallback > 255) return NvResult::NotImplemented;
            const auto selected = SelectNativeMemoryKind(static_cast<u8>(requested), static_cast<u8>(fallback));
            if (!selected) return NvResult::NotImplemented;
            kind = *selected;
        }
        updated.Replace(*range);
        ranges.push_back(*range);
        kinds.push_back(kind);
    }
    if (updated.Ranges().size() > 65536) return NvResult::InsufficientMemory;
    const auto retained = updated.ReferencedHandles();
    auto needed = retained;
    for (const auto& range : ranges) if (range.handle) needed.insert(range.handle);
    for (const auto handle : needed) {
        if (!native_sparse_pins.contains(handle)) {
            native_sparse_pins.emplace(handle, nvmap.PinHandle(handle, false));
        }
    }
    for (size_t i = 0; i < ranges.size(); ++i) {
        const auto& range = ranges[i];
        if (range.handle) {
            gmmu->Map(range.address, native_sparse_pins.at(range.handle) + range.source_offset,
                      range.size, static_cast<Tegra::PTEKind>(kinds[i].backend), false);
            if (u16{entries[i].kind} != kinds[i].selected) entries[i].kind = 0xffff;
            entries[i].fallback_kind = kinds[i].selected;
        } else {
            gmmu->MapSparse(range.address, range.size, false);
        }
        LOG_DEBUG(Service_NVDRV, "Native sparse remap VA={:#x}, size={:#x}, memory={}, offset={:#x}, kind={:#x}",
                  range.address, range.size, range.handle, range.source_offset, kinds[i].selected);
    }
    native_sparse_mappings = std::move(updated);
    for (auto it = native_sparse_pins.begin(); it != native_sparse_pins.end();) {
        if (!retained.contains(it->first)) {
            nvmap.UnpinHandle(it->first);
            it = native_sparse_pins.erase(it);
        } else ++it;
    }
    return NvResult::Success;
}

NvResult nvhost_as_gpu::Remap(std::span<IoctlRemapEntry> entries) {
    LOG_DEBUG(Service_NVDRV, "called, num_entries={:#x}", entries.size());

    if (!vm.initialised) {
        return NvResult::BadValue;
    }

    for (const auto& entry : entries) {
        GPUVAddr virtual_address{static_cast<u64>(entry.as_offset_big_pages)
                                 << vm.big_page_size_bits};
        u64 size{static_cast<u64>(entry.big_pages) << vm.big_page_size_bits};

        auto alloc{allocation_map.upper_bound(virtual_address)};

        if (alloc-- == allocation_map.begin() ||
            (virtual_address - alloc->first) + size > alloc->second.size) {
            LOG_WARNING(Service_NVDRV, "Cannot remap into an unallocated region!");
            return NvResult::BadValue;
        }

        if (!alloc->second.sparse) {
            LOG_WARNING(Service_NVDRV, "Cannot remap a non-sparse mapping!");
            return NvResult::BadValue;
        }

        const bool use_big_pages = alloc->second.big_pages;
        if (!entry.handle) {
            gmmu->MapSparse(virtual_address, size, use_big_pages);
        } else {
            auto handle{nvmap.GetHandle(entry.handle)};
            if (!handle) {
                return NvResult::BadValue;
            }

            DAddr base = nvmap.PinHandle(entry.handle, false);
            DAddr device_address{static_cast<DAddr>(
                base + (static_cast<u64>(entry.handle_offset_big_pages) << vm.big_page_size_bits))};

            gmmu->Map(virtual_address, device_address, size,
                      static_cast<Tegra::PTEKind>(entry.kind), use_big_pages);
        }
    }

    return NvResult::Success;
}

NvResult nvhost_as_gpu::MapBufferEx(IoctlMapBufferEx& params) {
    LOG_DEBUG(Service_NVDRV,
              "called, flags={:X}, nvmap_handle={:X}, buffer_offset={}, mapping_size={}"
              ", offset={:#x}",
              params.flags, params.handle, params.buffer_offset, params.mapping_size,
              params.offset);

    std::scoped_lock lock(mutex);

    if (!vm.initialised) {
        return NvResult::BadValue;
    }

    // Remaps a subregion of an existing mapping to a different PA
    if ((params.flags & MappingFlags::Remap) != MappingFlags::None) {
        if (auto const it = mapping_map.find(params.offset); it != mapping_map.end()) {
            auto const mapping = it->second;
            if (mapping.size < params.mapping_size) {
                LOG_WARNING(Service_NVDRV, "Cannot remap a partially mapped GPU address space region: {:#x}", params.offset);
                return NvResult::BadValue;
            }
            u64 gpu_address = u64(params.offset + params.buffer_offset);
            VAddr device_address{mapping.ptr + params.buffer_offset};
            gmmu->Map(gpu_address, device_address, params.mapping_size, Tegra::PTEKind(params.kind), mapping.big_page);
            return NvResult::Success;
        } else {
            LOG_WARNING(Service_NVDRV, "Cannot remap an unmapped GPU address space region: {:#x}", params.offset);
            return NvResult::BadValue;
        }
    }

    auto handle{nvmap.GetHandle(params.handle)};
    if (!handle) {
        return NvResult::BadValue;
    }

    DAddr device_address = DAddr(nvmap.PinHandle(params.handle, false) + params.buffer_offset);
    u64 size{params.mapping_size ? params.mapping_size : handle->orig_size};

    bool big_page{[&]() {
        if (Common::IsAligned(handle->align, vm.big_page_size)) {
            return true;
        } else if (Common::IsAligned(handle->align, VM::YUZU_PAGESIZE)) {
            return false;
        } else {
            ASSERT(false);
            return false;
        }
    }()};

    if ((params.flags & MappingFlags::Fixed) != MappingFlags::None) {
        auto alloc{allocation_map.upper_bound(params.offset)};

        if (alloc-- == allocation_map.begin() ||
            (params.offset - alloc->first) + size > alloc->second.size) {
            ASSERT_MSG(false, "Cannot perform a fixed mapping into an unallocated region!");
            return NvResult::BadValue;
        }

        const bool use_big_pages = alloc->second.big_pages && big_page;
        gmmu->Map(params.offset, device_address, size, static_cast<Tegra::PTEKind>(params.kind), use_big_pages);

        alloc->second.mappings.push_back(params.offset);
        mapping_map.insert_or_assign(params.offset, Mapping(params.handle, device_address, params.offset, size, true, use_big_pages, alloc->second.sparse));
    } else {
        auto& allocator{big_page ? *vm.big_page_allocator : *vm.small_page_allocator};
        u32 page_size{big_page ? vm.big_page_size : VM::YUZU_PAGESIZE};
        u32 page_size_bits{big_page ? vm.big_page_size_bits : VM::PAGE_SIZE_BITS};

        params.offset = u64(allocator.Allocate(u32(Common::AlignUp(size, page_size) >> page_size_bits))) << page_size_bits;
        if (!params.offset) {
            ASSERT_MSG(false, "Failed to allocate free space in the GPU AS!");
            return NvResult::InsufficientMemory;
        }
        gmmu->Map(params.offset, device_address, Common::AlignUp(size, page_size), Tegra::PTEKind(params.kind), big_page);
        mapping_map.insert_or_assign(params.offset, Mapping(params.handle, device_address, params.offset, size, false, big_page, false));
    }

    map_buffer_offsets.insert(params.offset);

    return NvResult::Success;
}

NvResult nvhost_as_gpu::UnmapBuffer(IoctlUnmapBuffer& params) {
    std::scoped_lock lock(mutex);
    if (auto const offset_it = map_buffer_offsets.find(params.offset); offset_it != map_buffer_offsets.end()) {
        LOG_DEBUG(Service_NVDRV, "called, offset={:#x}", params.offset);
        if (!vm.initialised) {
            return NvResult::BadValue;
        }

        auto const it = mapping_map.find(params.offset);
        auto const mapping = it->second;
        if (!mapping.fixed) {
            auto& allocator{mapping.big_page ? *vm.big_page_allocator : *vm.small_page_allocator};
            u32 page_size_bits{mapping.big_page ? vm.big_page_size_bits : VM::PAGE_SIZE_BITS};
            allocator.Free(u32(mapping.offset >> page_size_bits), u32(mapping.size >> page_size_bits));
        }

        // Sparse mappings shouldn't be fully unmapped, just returned to their sparse state
        // Only FreeSpace can unmap them fully
        if (mapping.sparse_alloc) {
            gmmu->MapSparse(params.offset, mapping.size, mapping.big_page);
        } else {
            gmmu->Unmap(params.offset, mapping.size);
        }

        nvmap.UnpinHandle(mapping.handle);
        mapping_map.erase(params.offset);
        map_buffer_offsets.erase(params.offset);
    }
    return NvResult::Success;
}

NvResult nvhost_as_gpu::BindChannel(IoctlBindChannel& params) {
    LOG_DEBUG(Service_NVDRV, "called, fd={:X}", params.fd);

    const auto device = module.GetDevice<nvdevice>(params.fd);
    if (!device || device->GetType() != nvdevice::Type::GpuChannel) { return NvResult::BadParameter; }
    const auto gpu_channel_device = std::static_pointer_cast<nvhost_gpu>(device);
    if (!vm.initialised || !gmmu || (gpu_channel_device->channel_state->initialized &&
        gpu_channel_device->channel_state->memory_manager != gmmu.get())) { return NvResult::InvalidState; }
    gpu_channel_device->channel_state->memory_manager_owner =
        std::shared_ptr<Tegra::MemoryManager>(module.GetDevice<nvhost_as_gpu>(m_fd), gmmu.get());
    gpu_channel_device->channel_state->memory_manager = gmmu.get();
    return NvResult::Success;
}

void nvhost_as_gpu::GetVARegionsImpl(IoctlGetVaRegions& params) {
    params.buf_size = 2 * sizeof(VaRegion);

    params.regions = std::array<VaRegion, 2>{
        VaRegion{
            .offset = GpuPageAddress(vm.small_page_allocator->GetVAStart(), VM::PAGE_SIZE_BITS),
            .page_size = VM::YUZU_PAGESIZE,
            ._pad0_{},
            .pages = vm.small_page_allocator->GetVALimit() - vm.small_page_allocator->GetVAStart(),
        },
        VaRegion{
            .offset = GpuPageAddress(vm.big_page_allocator->GetVAStart(), vm.big_page_size_bits),
            .page_size = vm.big_page_size,
            ._pad0_{},
            .pages = vm.big_page_allocator->GetVALimit() - vm.big_page_allocator->GetVAStart(),
        },
    };
    if (m_extended_abi && params.regions[1].pages == 0) {
        params.regions[1] = {};
        params.buf_size = sizeof(VaRegion);
    }
}

NvResult nvhost_as_gpu::GetVARegions1(IoctlGetVaRegions& params) {
    LOG_DEBUG(Service_NVDRV, "called, buf_addr={:X}, buf_size={:X}", params.buf_addr, params.buf_size);

    std::scoped_lock lock(mutex);

    if (!vm.initialised) {
        return NvResult::BadValue;
    }

    GetVARegionsImpl(params);

    return NvResult::Success;
}

NvResult nvhost_as_gpu::GetVARegions3(IoctlGetVaRegions& params, std::span<VaRegion> regions) {
    LOG_DEBUG(Service_NVDRV, "called, buf_addr={:X}, buf_size={:X}", params.buf_addr,
              params.buf_size);

    std::scoped_lock lock(mutex);

    if (!vm.initialised) {
        return NvResult::BadValue;
    }

    GetVARegionsImpl(params);

    const size_t num_regions = (std::min)(static_cast<size_t>(params.buf_size) / sizeof(VaRegion),
                                        regions.size());
    for (size_t i = 0; i < num_regions; i++) {
        regions[i] = params.regions[i];
    }

    return NvResult::Success;
}

Kernel::KEvent* nvhost_as_gpu::QueryEvent(u32 event_id) {
    LOG_CRITICAL(Service_NVDRV, "Unknown AS GPU Event {}", event_id);
    return nullptr;
}

} // namespace Service::Nvidia::Devices
