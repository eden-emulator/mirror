// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "common/common_types.h"
#include "common/guest_memory_access.h"

namespace Core::Timing {
struct EventType;
}

namespace Kernel {
class KEvent;
class KProcess;
class KTransferMemory;
} // namespace Kernel

namespace AudioCore {
class AudioCore;

/// What audio_core needs from the emulated system, implemented by Core::System.
/// audio_core must not include core headers or link against core; when it needs something new
/// from the system, add it here instead. See docs/Development.md.
class Host {
public:
    using TimedCallback = std::function<std::optional<std::chrono::nanoseconds>(
        s64 time, std::chrono::nanoseconds ns_late)>;

    [[nodiscard]] virtual bool IsPaused() const = 0;
    [[nodiscard]] virtual bool IsShuttingDown() const = 0;
    [[nodiscard]] virtual ::AudioCore::AudioCore& AudioCore() = 0;

    [[nodiscard]] virtual std::chrono::nanoseconds GlobalTimeNs() = 0;
    [[nodiscard]] virtual std::chrono::microseconds GlobalTimeUs() = 0;
    [[nodiscard]] virtual std::shared_ptr<Core::Timing::EventType> CreateTimingEvent(
        std::string name, TimedCallback&& callback) = 0;
    virtual void ScheduleLoopingEvent(std::chrono::nanoseconds start_time,
                                      std::chrono::nanoseconds resched_time,
                                      const std::shared_ptr<Core::Timing::EventType>& event) = 0;
    virtual void UnscheduleEvent(const std::shared_ptr<Core::Timing::EventType>& event) = 0;

    virtual void SignalEvent(Kernel::KEvent& event) = 0;
    virtual void ClearEvent(Kernel::KEvent& event) = 0;
    virtual void OpenProcess(Kernel::KProcess& process) = 0;
    virtual void CloseProcess(Kernel::KProcess& process) = 0;
    [[nodiscard]] virtual Core::Memory::GuestMemoryAccess& ProcessMemory(
        Kernel::KProcess& process) = 0;
    [[nodiscard]] virtual Common::ProcessAddress TransferMemoryAddress(
        Kernel::KTransferMemory& transfer_memory) = 0;

protected:
    ~Host() = default;
};

} // namespace AudioCore
