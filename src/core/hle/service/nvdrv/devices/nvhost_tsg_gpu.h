// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <map>
#include <memory>
#include "core/hle/service/nvdrv/devices/nvdevice.h"

namespace Tegra { struct NativeMacroContext; }
namespace Kernel { class KProcess; }
namespace Service::Nvidia { class Module; }

namespace Service::Nvidia::Devices {
class nvhost_gpu;

class nvhost_tsg_gpu final : public nvdevice {
public:
    nvhost_tsg_gpu(Core::System& system, Module& module, NvCore::Container& core);
    ~nvhost_tsg_gpu() override;
    NvResult Ioctl1(DeviceFD fd, Ioctl command, std::span<const u8> input,
                    std::span<u8> output) override;
    NvResult Ioctl2(DeviceFD fd, Ioctl command, std::span<const u8> input,
                    std::span<const u8> inline_input, std::span<u8> output) override;
    NvResult Ioctl3(DeviceFD fd, Ioctl command, std::span<const u8> input,
                    std::span<u8> output, std::span<u8> inline_output) override;
    void OnOpen(NvCore::SessionId session, DeviceFD fd) override;
    void OnClose(DeviceFD fd) override;
private:
    std::shared_ptr<Tegra::NativeMacroContext> native_macros;
    Module& module;
    NvCore::Container& core;
    Kernel::KProcess* process{};
    DeviceFD group_fd{-1};
    std::map<DeviceFD, std::shared_ptr<nvhost_gpu>> channels;
};

} // namespace Service::Nvidia::Devices
