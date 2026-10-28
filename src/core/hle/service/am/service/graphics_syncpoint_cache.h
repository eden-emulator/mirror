// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <limits>
#include <optional>
#include "common/common_types.h"

namespace Service::AM::GraphicsSyncpointCache {
constexpr u32 ValuesOffset = 0x40;
constexpr u32 GroupAckOffset = 0x640;
constexpr u32 SlotAckOffset = 0x648;
constexpr u32 GroupRequestOffset = 0;
constexpr u32 SlotRequestOffset = 8;
constexpr u32 CounterCount = 192;
constexpr u32 GroupCount = (CounterCount + 63) / 64;
constexpr u64 Unallocated = (std::numeric_limits<u64>::max)();

constexpr std::array<u32, 8> Header{
    1, 0, CounterCount - 1, ValuesOffset, GroupRequestOffset,
    GroupAckOffset, SlotRequestOffset, SlotAckOffset};
static_assert(ValuesOffset + CounterCount * sizeof(u64) <= GroupAckOffset);
static_assert(SlotAckOffset + GroupCount * sizeof(u64) <= 0x2000);
static_assert(SlotRequestOffset + GroupCount * sizeof(u64) <= 0x1000);

constexpr u64 ExtendCounter(u64 previous, u32 current) {
    if (previous == Unallocated) {
        return current;
    }
    u64 high = previous & 0xffffffff00000000ULL;
    if (current < static_cast<u32>(previous)) {
        high += 1ULL << 32;
    }
    return high | current;
}
struct CounterValue {
    u64 previous{Unallocated};

    u64 Observe(std::optional<u32> current) {
        if (!current) {
            return Unallocated;
        }
        previous = ExtendCounter(previous, *current);
        return previous;
    }
};
} // namespace Service::AM::GraphicsSyncpointCache
