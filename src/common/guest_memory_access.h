// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>

#include "common/common_types.h"
#include "common/guest_memory.h"
#include "common/typed_address.h"

namespace Core::Memory {

/**
 * Page size used by the ARM architecture. This is the smallest granularity with which memory can
 * be mapped.
 */
constexpr std::size_t YUZU_PAGEBITS = 12;
constexpr u64 YUZU_PAGESIZE = 1ULL << YUZU_PAGEBITS;
constexpr u64 YUZU_PAGEMASK = YUZU_PAGESIZE - 1;

/// Guest process memory as seen from outside core, implemented by Core::Memory::Memory
class GuestMemoryAccess {
public:
    static constexpr bool HAS_FLUSH_INVALIDATION = false;

    virtual u8* GetPointerSilent(Common::ProcessAddress vaddr) = 0;
    virtual u8* GetSpan(VAddr src_addr, std::size_t size) = 0;
    virtual bool ReadBlock(Common::ProcessAddress src_addr, void* dest_buffer,
                           std::size_t size) = 0;
    virtual bool ReadBlockUnsafe(Common::ProcessAddress src_addr, void* dest_buffer,
                                 std::size_t size) = 0;
    virtual bool WriteBlock(Common::ProcessAddress dest_addr, const void* src_buffer,
                            std::size_t size) = 0;
    virtual bool WriteBlockUnsafe(Common::ProcessAddress dest_addr, const void* src_buffer,
                                  std::size_t size) = 0;
    virtual bool ZeroBlock(Common::ProcessAddress dest_addr, std::size_t size) = 0;
    virtual void Write32(Common::ProcessAddress addr, u32 data) = 0;
    virtual void RasterizerMarkRegionCached(Common::ProcessAddress vaddr, u64 size,
                                            bool cached) = 0;

protected:
    ~GuestMemoryAccess() = default;
};

template <typename T, GuestMemoryFlags FLAGS>
using GuestAccessMemory = GuestMemory<GuestMemoryAccess, T, FLAGS>;

} // namespace Core::Memory
