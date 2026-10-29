// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <bit>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include "common/common_types.h"
#include "common/swap.h"

namespace Service::Nvidia::Devices {

struct NativeSparseRemapEntry {
    u32_le flags;
    u16_le kind;
    u16_le fallback_kind;
    u32_le handle;
    u32_le reserved;
    u64_le source_pages;
    u64_le destination_pages;
    u64_le pages;
};
static_assert(sizeof(NativeSparseRemapEntry) == 40);

struct NativeSparseRange {
    u64 address;
    u64 size;
    u64 source_offset;
    u32 handle;
};

inline std::optional<NativeSparseRange> DecodeNativeSparseRemap(const NativeSparseRemapEntry& entry) {
    const u32 flags = entry.flags;
    const u32 page_flags = flags & 0x7000;
    if (entry.reserved != 0 || (flags & ~0xf004U) || !std::has_single_bit(page_flags)) {
        return std::nullopt;
    }
    const u32 shift = page_flags == 0x1000 ? 12 : page_flags == 0x2000 ? 16 : 17;
    const u64 limit = std::numeric_limits<u64>::max() >> shift;
    if (!entry.pages || entry.pages > limit || entry.source_pages > limit ||
        entry.destination_pages > limit) {
        return std::nullopt;
    }
    const u64 size = u64{entry.pages} << shift;
    const u64 address = u64{entry.destination_pages} << shift;
    const u64 source = u64{entry.source_pages} << shift;
    if (address > std::numeric_limits<u64>::max() - size ||
        source > std::numeric_limits<u64>::max() - size || (!entry.handle && source)) {
        return std::nullopt;
    }
    return NativeSparseRange{address, size, source, entry.handle};
}

// Keep the surviving pieces of replaced mappings so their physical handles stay pinned.
class NativeSparseMappingTable {
public:
    void Replace(const NativeSparseRange& range) {
        const u64 end = range.address + range.size;
        auto it = ranges.lower_bound(range.address);
        if (it != ranges.begin()) {
            const auto previous = std::prev(it);
            if (previous->first + previous->second.size > range.address) it = previous;
        }
        while (it != ranges.end() && it->first < end) {
            const auto old = it->second;
            const u64 old_end = old.address + old.size;
            it = ranges.erase(it);
            if (old.address < range.address) {
                auto left = old;
                left.size = range.address - old.address;
                ranges.emplace(left.address, left);
            }
            if (old_end > end) {
                auto right = old;
                right.address = end;
                right.source_offset += end - old.address;
                right.size = old_end - end;
                ranges.emplace(right.address, right);
                break;
            }
        }
        if (range.handle) ranges.insert_or_assign(range.address, range);
    }
    std::set<u32> ReferencedHandles() const {
        std::set<u32> handles;
        for (const auto& [address, range] : ranges) handles.insert(range.handle);
        return handles;
    }
    const std::map<u64, NativeSparseRange>& Ranges() const { return ranges; }
private:
    std::map<u64, NativeSparseRange> ranges;
};

} // namespace Service::Nvidia::Devices
