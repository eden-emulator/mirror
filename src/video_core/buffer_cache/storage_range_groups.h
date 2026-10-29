// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>
#include "common/common_types.h"

namespace VideoCommon {
struct StorageAddressRange { u64 address; u32 size; };
struct StorageRangeGroup {
    u64 address{}, end{};
    std::vector<size_t> members;
};
// Overlapping virtual views must share one backing allocation so writes through
// one descriptor remain visible through every alias in the same dispatch.
inline std::vector<StorageRangeGroup> GroupStorageRanges(std::span<const StorageAddressRange> ranges) {
    std::vector<size_t> order;
    for (size_t i = 0; i < ranges.size(); ++i) {
        if (!ranges[i].address || !ranges[i].size) continue;
        if (ranges[i].address > std::numeric_limits<u64>::max() - ranges[i].size)
            throw std::overflow_error("Storage address range overflow");
        order.push_back(i);
    }
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return ranges[a].address < ranges[b].address; });
    std::vector<StorageRangeGroup> groups;
    for (size_t i : order) {
        const u64 address = ranges[i].address, end = address + ranges[i].size;
        if (groups.empty() || address >= groups.back().end) groups.push_back({address, end, {i}});
        else {
            groups.back().end = std::max(groups.back().end, end);
            groups.back().members.push_back(i);
        }
    }
    return groups;
}
} // namespace VideoCommon
