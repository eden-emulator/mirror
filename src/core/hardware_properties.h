// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <cstdlib>
#include <tuple>

#include "common/bit_util.h"
#include "common/common_types.h"

namespace Core {

namespace Hardware {

constexpr u64 BASE_CLOCK_RATE = 1'020'000'000; // Default CPU Frequency = 1020 MHz
constexpr u64 CNTFREQ = 19'200'000;            // CNTPCT_EL0 Frequency = 19.2 MHz
// Number of emulated CPU cores - sync with dynarmic exclusive_monitor.h. Switch 2 applications may use
// cores 0..5 (their NPDM ThreadInfo allows them); Switch 1 processes are limited to 0..3 by their own
// NPDM core range, so cores 4 and 5 stay idle for them.
constexpr u32 NUM_CPU_CORES = 6;
// Core count of the Switch 1 kernel. Kept for the paths whose behaviour must not change with the
// extra cores: guest tick amortization and the core id checks seen by non-extended processes.
constexpr u32 NUM_LEGACY_CPU_CORES = 4;
// Physical core that system work runs on: virtual core 63, service and dummy threads, host threads.
constexpr s32 SYSTEM_CPU_CORE = 3;

// Virtual to Physical core map. Virtual 0..5 are the application cores. The Switch 2 system modules
// run on virtual 6 and 7 (their NPDMs use cores 6..7 or 63); those map to the system core, as 63 does.
constexpr std::array<s32, Common::BitSize<u64>()> VirtualToPhysicalCoreMap{
    0, 1, 2, 3, 4, 5, 3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3,
};
static_assert(VirtualToPhysicalCoreMap[63] == SYSTEM_CPU_CORE);

static constexpr inline size_t NumVirtualCores = Common::BitSize<u64>();

static constexpr inline u64 VirtualCoreMask = [] {
    u64 mask = 0;
    for (size_t i = 0; i < NumVirtualCores; ++i) {
        mask |= (UINT64_C(1) << i);
    }
    return mask;
}();

static constexpr inline u64 ConvertVirtualCoreMaskToPhysical(u64 v_core_mask) {
    u64 p_core_mask = 0;
    while (v_core_mask != 0) {
        const u64 next = std::countr_zero(v_core_mask);
        v_core_mask &= ~(static_cast<u64>(1) << next);
        p_core_mask |= (static_cast<u64>(1) << VirtualToPhysicalCoreMap[next]);
    }
    return p_core_mask;
}

// Runtime translations used by thread creation and core masks. The EDEN_CORE_SPREAD experiment (4..6 -> 1..3)
// is gone: virtual cores 4 and 5 now have physical cores of their own.
inline s32 ConvertVirtualCoreToPhysical(size_t virtual_core) {
    return VirtualToPhysicalCoreMap[virtual_core];
}

inline u64 ConvertVirtualCoreMaskToPhysicalRuntime(u64 v_core_mask) {
    u64 p_core_mask = 0;
    while (v_core_mask != 0) {
        const u64 next = std::countr_zero(v_core_mask);
        v_core_mask &= ~(static_cast<u64>(1) << next);
        p_core_mask |= (static_cast<u64>(1) << ConvertVirtualCoreToPhysical(next));
    }
    return p_core_mask;
}

// Cortex-A57 supports 4 memory watchpoints
constexpr u64 NUM_WATCHPOINTS = 4;

} // namespace Hardware

} // namespace Core
