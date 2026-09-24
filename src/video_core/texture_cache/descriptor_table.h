// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <bit>
#include <cstring>
#include <utility>
#include <vector>

#include "common/alignment.h"
#include "common/common_types.h"
#include "common/div_ceil.h"
#include "common/assert.h"
#include "common/slot_vector.h"
#include "video_core/memory_manager.h"
#include "video_core/rasterizer_interface.h"

namespace VideoCommon {

template <typename T>
class DescriptorTable {
public:
    struct Entry {
        T descriptor;
        Common::SlotId id;
        u32 generation;
    };

    [[nodiscard]] bool Synchronize(GPUVAddr gpu_addr, u32 limit) noexcept {
        bool ret = !(current_gpu_addr == gpu_addr && current_limit == limit);
        if (ret) {
            Refresh(gpu_addr, limit);
        }
        return ret;
    }

    void Invalidate() noexcept {
        ++generation;
    }

    [[nodiscard]] std::pair<Entry&, bool> Read(Tegra::MemoryManager const& gpu_memory, u32 index) noexcept {
        DEBUG_ASSERT(index <= current_limit);
        const GPUVAddr gpu_addr = current_gpu_addr + index * sizeof(T);
        T value{};
        if (!aligned) {
            gpu_memory.ReadBlockUnsafe(gpu_addr, std::addressof(value), sizeof(T));
        } else if (const u8* const ptr = gpu_memory.GetPointer(gpu_addr)) {
            std::memcpy(std::addressof(value), ptr, sizeof(T));
        }
        Entry& entry = entries[index];
        const bool is_new = entry.generation != generation || entry.descriptor != value;
        if (is_new) {
            entry.descriptor = value;
            entry.generation = generation;
        }
        return {entry, is_new};
    }

    void Refresh(GPUVAddr gpu_addr, u32 limit) noexcept {
        current_gpu_addr = gpu_addr;
        current_limit = limit;
        aligned = gpu_addr % sizeof(T) == 0;
        ++generation;
        if (entries.size() <= limit) {
            entries.resize(std::bit_ceil(size_t{limit} + 1));
        }
    }

    std::vector<Entry> entries;
    GPUVAddr current_gpu_addr{};
    u32 current_limit{};
    u32 generation{1};
    bool aligned{};
};

} // namespace VideoCommon
