// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <vector>

#include "common/common_types.h"

namespace VideoCommon {

class UsageTracker {
    // PAGE_SHIFT is a macro on FreeBSD
    static constexpr size_t BUFFER_BYTES_PER_BITSHIFT = 6;
    static constexpr size_t BUFFER_PAGE_SHIFT = 6 + BUFFER_BYTES_PER_BITSHIFT;
    static constexpr u64 BUFFER_PAGE_BYTES = u64{1} << BUFFER_PAGE_SHIFT;

    struct Page {
        u64 bits;
        u64 tick;
    };

public:
    explicit UsageTracker(size_t size) : pages((size >> BUFFER_PAGE_SHIFT) + 1) {}

    void Track(u64 offset, u64 size, u64 tick, u64 gpu_tick) noexcept {
        const u64 end = offset + size;
        if (size == 0 || ((end - 1) >> BUFFER_PAGE_SHIFT) >= pages.size()) {
            return;
        }
        for (u64 page = offset >> BUFFER_PAGE_SHIFT; page <= (end - 1) >> BUFFER_PAGE_SHIFT;
             ++page) {
            Page& entry = pages[page];
            if (entry.tick <= gpu_tick) {
                entry.bits = 0;
            }
            entry.bits |= PageMask(page, offset, end);
            entry.tick = (std::max)(entry.tick, tick);
        }
    }

    [[nodiscard]] bool IsUsed(u64 offset, u64 size, u64 gpu_tick) const noexcept {
        const u64 end = offset + size;
        if (size == 0 || ((end - 1) >> BUFFER_PAGE_SHIFT) >= pages.size()) {
            return false;
        }
        for (u64 page = offset >> BUFFER_PAGE_SHIFT; page <= (end - 1) >> BUFFER_PAGE_SHIFT;
             ++page) {
            const Page& entry = pages[page];
            if (entry.tick > gpu_tick && (entry.bits & PageMask(page, offset, end)) != 0) {
                return true;
            }
        }
        return false;
    }

private:
    [[nodiscard]] static u64 PageMask(u64 page, u64 offset, u64 end) noexcept {
        const u64 page_begin = page << BUFFER_PAGE_SHIFT;
        const u64 first =
            ((std::max)(offset, page_begin) - page_begin) >> BUFFER_BYTES_PER_BITSHIFT;
        const u64 last = ((std::min)(end, page_begin + BUFFER_PAGE_BYTES) - 1 - page_begin) >>
                         BUFFER_BYTES_PER_BITSHIFT;
        return (~u64{0} >> (63 - last)) & (~u64{0} << first);
    }

    std::vector<Page> pages;
};

} // namespace VideoCommon
