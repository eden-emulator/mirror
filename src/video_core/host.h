// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <functional>

#include "common/common_types.h"

namespace Core::Memory {
class GuestMemoryAccess;
}

namespace Tegra {
class GPU;
namespace Host1x {
class Host1x;
}
} // namespace Tegra

namespace VideoCore {

/// What video_core needs from the emulated system, implemented by Core::System.
/// video_core must not include core headers or link against core; when it needs something new
/// from the system, add it here instead. See docs/Development.md.
class Host {
public:
    [[nodiscard]] virtual bool IsPoweredOn() const = 0;
    [[nodiscard]] virtual Tegra::GPU& GPU() = 0;
    [[nodiscard]] virtual Tegra::Host1x::Host1x& Host1x() = 0;
    [[nodiscard]] virtual const u8* DeviceMemoryBase() = 0;
    [[nodiscard]] virtual Core::Memory::GuestMemoryAccess& ApplicationGuestMemory() = 0;
    virtual void RegisterHostThread() = 0;
    virtual void GatherGPUDirtyMemory(std::function<void(PAddr, size_t)>& callback) = 0;
    [[nodiscard]] virtual u64 GPUTicks() = 0;
    virtual void EndGameFrame() = 0;

protected:
    ~Host() = default;
};

} // namespace VideoCore
