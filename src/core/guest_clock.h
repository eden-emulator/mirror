// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/hardware_properties.h"

namespace Core::Timing {

constexpr u32 GetGuestCounterFrequency(bool native_abi) {
    return native_abi ? 31'250'000U : static_cast<u32>(Hardware::CNTFREQ);
}

// Keep scheduler time in its existing units; convert only counters exposed to guests.
constexpr u64 GetGuestCounterTicks(u64 ticks, bool native_abi) {
    if (!native_abi) {
        return ticks;
    }
    const u64 frequency = GetGuestCounterFrequency(true);
    return (ticks / Hardware::CNTFREQ) * frequency +
           (ticks % Hardware::CNTFREQ) * frequency / Hardware::CNTFREQ;
}

} // namespace Core::Timing
