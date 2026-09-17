// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <limits>
#include <optional>

#include <boost/container/small_vector.hpp>

#include "common/common_types.h"
#include "common/container/unordered_map.h"
#include "video_core/memory_manager.h"

namespace VideoCommon {

struct VirtualSegment {
    GPUVAddr gpu_addr;
    DAddr device_addr;
    u32 size;
};

using VirtualSegments = boost::container::small_vector<VirtualSegment, 8>;

class VirtualRangeCache {
public:
    static constexpr size_t MAX_ENTRIES = 8192;

    const VirtualSegments* Query(Tegra::MemoryManager& memory, GPUVAddr gpu_addr, u32 size) {
        const u64 current = generation.load(std::memory_order_acquire);
        if (entries.size() > MAX_ENTRIES) {
            entries.clear();
        }
        const size_t as_id = memory.GetID();
        const u64 key = MakeKey(as_id, gpu_addr);
        const auto it = entries.find(key);
        if (it != entries.end() && it->second.generation == current &&
            it->second.as_id == as_id && it->second.gpu_addr == gpu_addr &&
            it->second.size == size) {
            return &it->second.segments;
        }
        Entry entry{};
        entry.generation = current;
        entry.as_id = as_id;
        entry.gpu_addr = gpu_addr;
        entry.size = size;
        const auto ranges = memory.GetSubmappedRange(gpu_addr, size);
        GPUVAddr expected = gpu_addr;
        bool contiguous = true;
        for (const auto& [range_addr, range_size] : ranges) {
            if (range_addr != expected || range_size == 0) {
                contiguous = false;
                break;
            }
            const std::optional<DAddr> device_addr = memory.GpuToCpuAddress(range_addr);
            if (!device_addr || *device_addr == 0) {
                contiguous = false;
                break;
            }
            if (range_size > static_cast<size_t>((std::numeric_limits<u32>::max)())) {
                contiguous = false;
                break;
            }
            entry.segments.push_back(VirtualSegment{
                .gpu_addr = range_addr,
                .device_addr = *device_addr,
                .size = static_cast<u32>(range_size),
            });
            expected += range_size;
        }
        if (!contiguous || expected != gpu_addr + size) {
            entry.segments.clear();
        }
        const auto result = entries.insert_or_assign(key, std::move(entry));
        return &result.first->second.segments;
    }

    void Unmap(size_t, GPUVAddr, u64 size) {
        if (size != 0) {
            generation.fetch_add(1, std::memory_order_release);
        }
    }

private:
    struct Entry {
        VirtualSegments segments;
        u64 generation{};
        size_t as_id{};
        GPUVAddr gpu_addr{};
        u32 size{};
    };

    static u64 MakeKey(size_t as_id, GPUVAddr gpu_addr) {
        return (static_cast<u64>(as_id) << 48) ^ gpu_addr;
    }

    ::Common::unordered_map<u64, Entry> entries;
    std::atomic<u64> generation{};
};

} // namespace VideoCommon
