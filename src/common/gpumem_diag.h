// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <cstdlib>

#include "common/common_types.h"

namespace Common::GpuMemDiag {

// Diagnostic (EDEN_DIAG_GPUMEM=1): live-object counters for the memory growth investigation.
// They are updated unconditionally (relaxed atomics) and only reported when the variable is set.
inline bool Enabled() {
    static const bool enabled = std::getenv("EDEN_DIAG_GPUMEM") != nullptr;
    return enabled;
}

inline std::atomic<s64> guest_memories{};  ///< CreateGpuAccessibleMemory objects alive
inline std::atomic<s64> guest_mappings{};  ///< MapNative4K mappings alive
inline std::atomic<s64> sparse_regions{};  ///< sparse regions alive
inline std::atomic<s64> nvmap_handles{};   ///< nvmap handles alive
inline std::atomic<u64> memories_created{};
inline std::atomic<u64> memories_destroyed{};

} // namespace Common::GpuMemDiag
