// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <deque>
#include <utility>

#include "common/common_types.h"

namespace VideoCommon {

template <typename T>
class DeferredDestructionQueue {
public:
    void Push(T&& object, u64 sync_point) {
        entries.emplace_back(std::move(object), sync_point);
    }

    void Reclaim(u64 completed_sync_point) {
        while (!entries.empty() && entries.front().sync_point <= completed_sync_point) {
            entries.pop_front();
        }
    }

    void Clear() {
        entries.clear();
    }

    [[nodiscard]] size_t Size() const noexcept {
        return entries.size();
    }

    [[nodiscard]] bool Empty() const noexcept {
        return entries.empty();
    }

private:
    struct Entry {
        Entry(T&& object_, u64 sync_point_) noexcept
            : object{std::move(object_)}, sync_point{sync_point_} {}

        T object;
        u64 sync_point;
    };

    std::deque<Entry> entries;
};

} // namespace VideoCommon
