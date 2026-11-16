// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2014 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <span>
#include <string>
#include <vector>

#include "common/common_funcs.h"
#include "common/common_types.h"
#include "core/hardware_properties.h"

#include "core/hle/kernel/svc_types.h"

namespace Common {
struct PageTable;
}

namespace Kernel {
enum class DebugWatchpointType : u8;
struct DebugWatchpoint;
class KThread;
class KProcess;
} // namespace Kernel

namespace Core {
using WatchpointArray = std::array<Kernel::DebugWatchpoint, Core::Hardware::NUM_WATCHPOINTS>;

// NOTE: these values match the HaltReason enum in Dynarmic
enum class HaltReason : u64 {
    StepThread = 0x00000001,
    DataAbort = 0x00000004,
    BreakLoop = 0x02000000,
    SupervisorCall = 0x04000000,
    InstructionBreakpoint = 0x08000000,
    PrefetchAbort = 0x20000000,
};
DECLARE_ENUM_FLAG_OPERATORS(HaltReason);

enum class Architecture {
    AArch64,
    AArch32,
};

/// Generic ARMv8 CPU interface
class ArmInterface : Common::NonCopyable, Common::NonMoveable {
public:
    explicit ArmInterface(bool uses_wall_clock) : m_uses_wall_clock{uses_wall_clock} {}
    virtual ~ArmInterface() = default;

    // Perform any backend-specific initialization.
    virtual void Initialize() {}

    // Runs the CPU until an event happens.
    virtual HaltReason RunThread(Kernel::KThread* thread) = 0;

    // Runs the CPU for one instruction or until an event happens.
    virtual HaltReason StepThread(Kernel::KThread* thread) = 0;

    // Admits a backend-specific mechanism to lock the thread context.
    virtual void LockThread(Kernel::KThread* thread) {}
    virtual void UnlockThread(Kernel::KThread* thread) {}

    // Clear the entire instruction cache for this CPU.
    virtual void ClearInstructionCache() = 0;

    // Clear a range of the instruction cache for this CPU.
    virtual void InvalidateCacheRange(u64 addr, std::size_t size) = 0;

    // Drop any cached translation of a guest range whose mapping or tracking changed. Only
    // backends that map guest memory themselves need it, see SetWantsMappingNotifications.
    virtual void InvalidateMapping(u64 addr, std::size_t size) {}

    [[nodiscard]] static bool AnyBackendWantsMappingNotifications() {
        return s_wants_mapping_notifications.load(std::memory_order_relaxed);
    }

    [[nodiscard]] static bool WantsRasterizerTracking() {
        return s_wants_rasterizer_tracking.load(std::memory_order_relaxed);
    }

    static void SetWantsMappingNotifications() {
        s_wants_mapping_notifications.store(true, std::memory_order_relaxed);
    }

    static void SetWantsRasterizerTracking() {
        s_wants_rasterizer_tracking.store(true, std::memory_order_relaxed);
    }

    // The GPU wrote a range, or is about to read what the guest wrote to it. A backend that
    // left such pages accessible without watching them drops them here, so the next access is
    // seen again: a read gets the GPU's new data, a write marks the range dirty.
    virtual void OnGpuAccess(u64 addr, std::size_t size) {}

    [[nodiscard]] static bool AnyWatchedPages() {
        return s_watched_pages.load(std::memory_order_relaxed) != 0;
    }

    static void AddWatchedPages(s64 delta) {
        s_watched_pages.fetch_add(delta, std::memory_order_relaxed);
    }

    // Get the current architecture.
    // This returns AArch64 when PSTATE.nRW == 0 and AArch32 when PSTATE.nRW == 1.
    virtual Architecture GetArchitecture() const = 0;

    // Context accessors.
    // These should not be called if the CPU is running.
    virtual void GetContext(Kernel::Svc::ThreadContext& ctx) const = 0;
    virtual void SetContext(const Kernel::Svc::ThreadContext& ctx) = 0;
    virtual void SetTpidrroEl0(u64 value) = 0;

    virtual void GetSvcArguments(std::span<uint64_t, 8> args) const = 0;
    virtual void SetSvcArguments(std::span<const uint64_t, 8> args) = 0;
    virtual u32 GetSvcNumber() const = 0;

    void SetWatchpointArray(const WatchpointArray* watchpoints) {
        m_watchpoints = watchpoints;
    }

    // Signal an interrupt for execution to halt as soon as possible.
    // It is safe to call this if the CPU is not running.
    virtual void SignalInterrupt(Kernel::KThread* thread) = 0;

    // Stack trace generation.
    void LogBacktrace(Kernel::KProcess* process) const;

    // Debug functionality.
    virtual const Kernel::DebugWatchpoint* HaltedWatchpoint() const = 0;
    virtual void RewindBreakpointInstruction() = 0;

protected:
    const Kernel::DebugWatchpoint* MatchingWatchpoint(
        u64 addr, u64 size, Kernel::DebugWatchpointType access_type) const;

protected:
    const WatchpointArray* m_watchpoints{};
    bool m_uses_wall_clock{};
    static std::atomic<bool> s_wants_mapping_notifications;
    static std::atomic<bool> s_wants_rasterizer_tracking;
    static std::atomic<s64> s_watched_pages;
};

} // namespace Core
