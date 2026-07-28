// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <vector>

#include "core/core.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/svc.h"

namespace Kernel::Svc {
namespace {

constexpr bool IsValidSetMemoryPermission(MemoryPermission perm) {
    switch (perm) {
    case MemoryPermission::None:
    case MemoryPermission::Read:
    case MemoryPermission::ReadWrite:
        return true;
    default:
        return false;
    }
}

bool IsHomebrewInPlaceNextLoadCodeRange(const KProcess& process, u64 address, u64 size) {
    if (!process.IsHomebrewInPlaceNextLoad()) {
        return false;
    }

    const u64 code_start = GetInteger(process.GetEntryPoint());
    const size_t code_size = process.GetCodeSize();
    if (code_start == 0 || code_size == 0 || size > code_size || address < code_start) {
        return false;
    }

    return address - code_start <= code_size - size;
}

struct HomebrewInPlaceMemoryBlock {
    u64 address;
    u64 size;
    KMemoryState state;
    KMemoryPermission permission;
    KMemoryAttribute attribute;
    bool use_process_permission;
};

Result SetHomebrewInPlaceMemoryPermissionByBlocks(KProcess& process, u64 address, u64 size,
                                                  MemoryPermission perm, Result original_result) {
    auto& page_table = process.GetPageTable();
    const u64 end = address + size;
    const auto requested_permission = ConvertToKMemoryPermission(perm);
    std::vector<HomebrewInPlaceMemoryBlock> blocks;

    for (u64 cursor = address; cursor < end;) {
        KMemoryInfo info;
        PageInfo page_info;
        const auto query_result = page_table.QueryInfo(std::addressof(info),
                                                       std::addressof(page_info), cursor);
        if (query_result.IsError()) {
            LOG_WARNING(Kernel_SVC,
                        "NextLoad in-place: split permission query failed "
                        "address=0x{:016X}, result={:#X}, original={:#X}",
                        cursor, query_result.raw, original_result.raw);
            R_RETURN(original_result);
        }

        const u64 block_end = (std::min<u64>)(info.GetEndAddress(), end);
        if (block_end <= cursor) {
            LOG_WARNING(Kernel_SVC,
                        "NextLoad in-place: split permission walk stalled "
                        "cursor=0x{:016X}, block=0x{:016X}/0x{:X}, original={:#X}",
                        cursor, info.GetAddress(), info.GetSize(), original_result.raw);
            R_RETURN(original_result);
        }

        const bool can_reprotect = True(info.GetState() & KMemoryState::FlagCanReprotect);
        const bool can_process_reprotect = True(info.GetState() & KMemoryState::FlagCode);
        if (!can_reprotect && !can_process_reprotect) {
            LOG_WARNING(Kernel_SVC,
                        "NextLoad in-place: split permission unsupported block "
                        "address=0x{:016X}, size=0x{:X}, state=0x{:08X}, svc_state={}, "
                        "perm=0x{:08X}, attr=0x{:08X}, original={:#X}",
                        cursor, block_end - cursor, static_cast<u32>(info.GetState()),
                        static_cast<u32>(info.GetSvcState()),
                        static_cast<u32>(info.GetPermission()),
                        static_cast<u32>(info.GetAttribute()), original_result.raw);
            R_RETURN(original_result);
        }

        blocks.push_back({
            .address = cursor,
            .size = block_end - cursor,
            .state = info.GetState(),
            .permission = info.GetPermission(),
            .attribute = info.GetAttribute(),
            .use_process_permission = !can_reprotect && can_process_reprotect,
        });
        cursor = block_end;
    }

    for (const auto& block : blocks) {
        if (block.permission == requested_permission) {
            continue;
        }

        const auto block_result =
            block.use_process_permission
                ? page_table.SetProcessMemoryPermission(block.address, block.size, perm)
                : page_table.SetMemoryPermission(block.address, block.size, perm);
        if (block_result.IsError()) {
            LOG_WARNING(Kernel_SVC,
                        "NextLoad in-place: split permission block failed "
                        "address=0x{:016X}, size=0x{:X}, state=0x{:08X}, perm=0x{:08X}, "
                        "attr=0x{:08X}, result={:#X}, original={:#X}",
                        block.address, block.size, static_cast<u32>(block.state),
                        static_cast<u32>(block.permission), static_cast<u32>(block.attribute),
                        block_result.raw, original_result.raw);
            R_RETURN(block_result);
        }
    }

    R_SUCCEED();
}

struct HomebrewInPlaceDeviceSharedBlock {
    u64 address;
    u64 size;
    u16 device_use_count;
};

Result UnlockHomebrewInPlaceDeviceSharedSource(KProcess& process, u64 address, u64 size,
                                               Result original_result) {
    auto& page_table = process.GetPageTable();
    const u64 end = address + size;
    std::vector<HomebrewInPlaceDeviceSharedBlock> blocks;

    for (u64 cursor = address; cursor < end;) {
        KMemoryInfo info;
        PageInfo page_info;
        const auto query_result = page_table.QueryInfo(std::addressof(info),
                                                       std::addressof(page_info), cursor);
        if (query_result.IsError()) {
            R_RETURN(original_result);
        }

        const u64 block_end = (std::min<u64>)(info.GetEndAddress(), end);
        if (block_end <= cursor) {
            R_RETURN(original_result);
        }

        const bool can_device_map = True(info.GetState() & KMemoryState::FlagCanDeviceMap);
        const bool is_clean_memory =
            can_device_map && info.GetPermission() == KMemoryPermission::UserReadWrite &&
            info.GetAttribute() == KMemoryAttribute::None && info.m_device_use_count == 0;
        const bool is_stale_device_shared =
            can_device_map && info.GetPermission() == KMemoryPermission::UserReadWrite &&
            info.GetAttribute() == KMemoryAttribute::DeviceShared && info.m_device_use_count > 0;
        if (is_clean_memory) {
            cursor = block_end;
            continue;
        }
        if (!is_stale_device_shared) {
            R_RETURN(original_result);
        }

        blocks.push_back({
            .address = cursor,
            .size = block_end - cursor,
            .device_use_count = info.m_device_use_count,
        });
        cursor = block_end;
    }

    if (blocks.empty()) {
        R_RETURN(original_result);
    }

    for (const auto& block : blocks) {
        for (u16 unlock = 0; unlock < block.device_use_count; unlock++) {
            const auto unlock_result =
                page_table.UnlockForDeviceAddressSpace(block.address, block.size);
            if (unlock_result.IsError()) {
                LOG_WARNING(Kernel_SVC,
                            "NextLoad in-place: device-shared source unlock failed "
                            "address=0x{:016X}, size=0x{:X}, remaining={}, result={:#X}, "
                            "original={:#X}",
                            block.address, block.size, block.device_use_count - unlock - 1,
                            unlock_result.raw, original_result.raw);
                R_RETURN(original_result);
            }
        }
    }

    R_SUCCEED();
}

// Checks if address + size is greater than the given address
// This can return false if the size causes an overflow of a 64-bit type
// or if the given size is zero.
constexpr bool IsValidAddressRange(u64 address, u64 size) {
    return address + size > address;
}

// Helper function that performs the common sanity checks for svcMapMemory
// and svcUnmapMemory. This is doable, as both functions perform their sanitizing
// in the same order.
Result MapUnmapMemorySanityChecks(const KProcessPageTable& manager, u64 dst_addr, u64 src_addr, u64 size) {
    if (!Common::IsAligned(dst_addr, Core::Memory::YUZU_PAGESIZE)) {
        LOG_ERROR(Kernel_SVC, "Destination address is not aligned to 4KB, {:#016X}", dst_addr);
        R_THROW(ResultInvalidAddress);
    }

    if (!Common::IsAligned(src_addr, Core::Memory::YUZU_PAGESIZE)) {
        LOG_ERROR(Kernel_SVC, "Source address is not aligned to 4KB, {:#016X}", src_addr);
        R_THROW(ResultInvalidSize);
    }

    if (size == 0) {
        LOG_ERROR(Kernel_SVC, "Size is 0");
        R_THROW(ResultInvalidSize);
    }

    if (!Common::IsAligned(size, Core::Memory::YUZU_PAGESIZE)) {
        LOG_ERROR(Kernel_SVC, "Size is not aligned to 4KB, {:#016X}", size);
        R_THROW(ResultInvalidSize);
    }

    if (!IsValidAddressRange(dst_addr, size)) {
        LOG_ERROR(Kernel_SVC,
                  "Destination is not a valid address range, addr={:#016x}, size={:#016x}",
                  dst_addr, size);
        R_THROW(ResultInvalidCurrentMemory);
    }

    if (!IsValidAddressRange(src_addr, size)) {
        LOG_ERROR(Kernel_SVC, "Source is not a valid address range, addr={:#016x}, size={:#016x}",
                  src_addr, size);
        R_THROW(ResultInvalidCurrentMemory);
    }

    if (!manager.Contains(src_addr, size)) {
        LOG_ERROR(Kernel_SVC,
                  "Source is not within the address space, addr={:#016x}, size={:#016x}",
                  src_addr, size);
        R_THROW(ResultInvalidCurrentMemory);
    }

    R_SUCCEED();
}

} // namespace

Result SetMemoryPermission(Core::System& system, u64 address, u64 size, MemoryPermission perm) {
    LOG_DEBUG(Kernel_SVC, "called, address={:#016x}, size={:#x}, perm={:#08x}", address, size,
              perm);

    // Validate address / size.
    R_UNLESS(Common::IsAligned(address, PageSize), ResultInvalidAddress);
    R_UNLESS(Common::IsAligned(size, PageSize), ResultInvalidSize);
    R_UNLESS(size > 0, ResultInvalidSize);
    R_UNLESS((address < address + size), ResultInvalidCurrentMemory);

    // Validate the permission.
    R_UNLESS(IsValidSetMemoryPermission(perm), ResultInvalidNewMemoryPermission);

    // Validate that the region is in range for the current process.
    auto& process = GetCurrentProcess(system.Kernel());
    auto& page_table = process.GetPageTable();
    R_UNLESS(page_table.Contains(address, size), ResultInvalidCurrentMemory);

    // Set the memory attribute.
    const auto result = page_table.SetMemoryPermission(address, size, perm);
    if (result.raw == ResultInvalidCurrentMemory.raw &&
        IsHomebrewInPlaceNextLoadCodeRange(process, address, size)) {
        R_RETURN(
            SetHomebrewInPlaceMemoryPermissionByBlocks(process, address, size, perm, result));
    }

    R_RETURN(result);
}

Result SetMemoryAttribute(Core::System& system, u64 address, u64 size, u32 mask, u32 attr) {
    LOG_DEBUG(Kernel_SVC,
              "called, address={:#016x}, size={:#x}, mask={:#08x}, attribute={:#08x}", address,
              size, mask, attr);

    // Validate address / size.
    R_UNLESS(Common::IsAligned(address, PageSize), ResultInvalidAddress);
    R_UNLESS(Common::IsAligned(size, PageSize), ResultInvalidSize);
    R_UNLESS(size > 0, ResultInvalidSize);
    R_UNLESS((address < address + size), ResultInvalidCurrentMemory);

    // Validate the attribute and mask.
    constexpr u32 SupportedMask =
        static_cast<u32>(MemoryAttribute::Uncached | MemoryAttribute::PermissionLocked);
    R_UNLESS((mask | attr) == mask, ResultInvalidCombination);
    R_UNLESS((mask | attr | SupportedMask) == SupportedMask, ResultInvalidCombination);

    // Check that permission locked is either being set or not masked.
    R_UNLESS((static_cast<Svc::MemoryAttribute>(mask) & Svc::MemoryAttribute::PermissionLocked) ==
                 (static_cast<Svc::MemoryAttribute>(attr) & Svc::MemoryAttribute::PermissionLocked),
             ResultInvalidCombination);

    // Validate that the region is in range for the current process.
    auto& page_table{GetCurrentProcess(system.Kernel()).GetPageTable()};
    R_UNLESS(page_table.Contains(address, size), ResultInvalidCurrentMemory);

    // Set the memory attribute.
    R_RETURN(page_table.SetMemoryAttribute(address, size, static_cast<KMemoryAttribute>(mask),
                                           static_cast<KMemoryAttribute>(attr)));
}

/// Maps a memory range into a different range.
Result MapMemory(Core::System& system, u64 dst_addr, u64 src_addr, u64 size) {
    LOG_TRACE(Kernel_SVC, "called, dst_addr={:#x}, src_addr={:#x}, size={:#x}", dst_addr,
              src_addr, size);

    auto& process = GetCurrentProcess(system.Kernel());
    auto& page_table = process.GetPageTable();

    if (const Result result{MapUnmapMemorySanityChecks(page_table, dst_addr, src_addr, size)};
        result.IsError()) {
        return result;
    }

    const auto result = page_table.MapMemory(dst_addr, src_addr, size);
    if (result.raw == ResultInvalidCurrentMemory.raw && process.IsHomebrewInPlaceNextLoad()) {
        if (UnlockHomebrewInPlaceDeviceSharedSource(process, src_addr, size, result).IsSuccess()) {
            const auto retry_result = page_table.MapMemory(dst_addr, src_addr, size);
            if (retry_result.IsError()) {
                LOG_WARNING(Kernel_SVC,
                            "NextLoad in-place: svcMapMemory retry failed after "
                            "DeviceShared cleanup dst=0x{:016X}, src=0x{:016X}, size=0x{:X}, "
                            "result={:#X}",
                            dst_addr, src_addr, size, retry_result.raw);
            }
            R_RETURN(retry_result);
        }
    }

    R_RETURN(result);
}

/// Unmaps a region that was previously mapped with svcMapMemory
Result UnmapMemory(Core::System& system, u64 dst_addr, u64 src_addr, u64 size) {
    LOG_TRACE(Kernel_SVC, "called, dst_addr={:#x}, src_addr={:#x}, size={:#x}", dst_addr,
              src_addr, size);

    auto& process = GetCurrentProcess(system.Kernel());
    auto& page_table = process.GetPageTable();

    if (const Result result{MapUnmapMemorySanityChecks(page_table, dst_addr, src_addr, size)};
        result.IsError()) {
        return result;
    }

    const auto result = page_table.UnmapMemory(dst_addr, src_addr, size);
    if (result.raw == ResultInvalidCurrentMemory.raw && process.IsHomebrewInPlaceNextLoad()) {
        LOG_WARNING(Kernel_SVC,
                    "NextLoad in-place: svcUnmapMemory failed dst=0x{:016X}, "
                    "src=0x{:016X}, size=0x{:X}, result={:#X}",
                    dst_addr, src_addr, size, result.raw);
    }

    R_RETURN(result);
}

Result SetMemoryPermission64(Core::System& system, uint64_t address, uint64_t size,
                             MemoryPermission perm) {
    R_RETURN(SetMemoryPermission(system, address, size, perm));
}

Result SetMemoryAttribute64(Core::System& system, uint64_t address, uint64_t size, uint32_t mask,
                            uint32_t attr) {
    R_RETURN(SetMemoryAttribute(system, address, size, mask, attr));
}

Result MapMemory64(Core::System& system, uint64_t dst_address, uint64_t src_address,
                   uint64_t size) {
    R_RETURN(MapMemory(system, dst_address, src_address, size));
}

Result UnmapMemory64(Core::System& system, uint64_t dst_address, uint64_t src_address,
                     uint64_t size) {
    R_RETURN(UnmapMemory(system, dst_address, src_address, size));
}

Result SetMemoryPermission64From32(Core::System& system, uint32_t address, uint32_t size,
                                   MemoryPermission perm) {
    R_RETURN(SetMemoryPermission(system, address, size, perm));
}

Result SetMemoryAttribute64From32(Core::System& system, uint32_t address, uint32_t size,
                                  uint32_t mask, uint32_t attr) {
    R_RETURN(SetMemoryAttribute(system, address, size, mask, attr));
}

Result MapMemory64From32(Core::System& system, uint32_t dst_address, uint32_t src_address,
                         uint32_t size) {
    R_RETURN(MapMemory(system, dst_address, src_address, size));
}

Result UnmapMemory64From32(Core::System& system, uint32_t dst_address, uint32_t src_address,
                           uint32_t size) {
    R_RETURN(UnmapMemory(system, dst_address, src_address, size));
}

} // namespace Kernel::Svc
