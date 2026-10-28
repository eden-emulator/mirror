// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cstring>
#include "common/logging.h"
#include "core/core.h"
#include "video_core/macro.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/service/nvdrv/nvdrv.h"
#include "core/hle/service/nvdrv/devices/nvhost_gpu.h"
#include "core/hle/service/nvdrv/devices/nvhost_tsg_gpu.h"

namespace Service::Nvidia::Devices {

nvhost_tsg_gpu::nvhost_tsg_gpu(Core::System& system, Module& module_, NvCore::Container& core_)
    : nvdevice{system, Type::GpuTsg}, native_macros{std::make_shared<Tegra::NativeMacroContext>()}, module{module_}, core{core_} { native_macros->engine.native_turing = true; native_macros->compute_engine.native_turing = true; }

nvhost_tsg_gpu::~nvhost_tsg_gpu() { OnClose(0); }

void nvhost_tsg_gpu::OnOpen(NvCore::SessionId id, DeviceFD fd) {
    group_fd = fd;
    if (const auto* session = core.GetSession(id)) {
        process = session->process;
        process->Open(system.Kernel());
    }
}

void nvhost_tsg_gpu::OnClose(DeviceFD fd) {
    for (const auto& [channel_fd, channel] : channels) { channel->UnbindFromTsg(group_fd); }
    channels.clear();
    if (process) {
        process->Close(system.Kernel());
        process = nullptr;
    }
}


NvResult nvhost_tsg_gpu::Ioctl1(DeviceFD fd, Ioctl command, std::span<const u8> input,
                                std::span<u8> output) {
    std::array<u32, 8> words{};
    if (!input.empty()) { std::memcpy(words.data(), input.data(), std::min(input.size(), sizeof(words))); }
    LOG_DEBUG(Service_NVDRV, "native TSG ioctl={:#x}, input={}, output={}, words={:#x},{:#x},{:#x},{:#x},{:#x},{:#x},{:#x},{:#x}",
              command.raw, input.size(), output.size(), words[0], words[1], words[2], words[3],
              words[4], words[5], words[6], words[7]);
    if (command.raw != 0xc0045401 && command.raw != 0xc0045402) { return NvResult::NotImplemented; }
    if (!process || input.size() != 4 || output.size() != 4) { return NvResult::BadParameter; }
    const DeviceFD channel_fd = static_cast<s32>(words[0]);
    if (command.cmd == 1) {
        if (channels.contains(channel_fd)) { return NvResult::Busy; }
        if (channels.size() >= 64) { return NvResult::InsufficientMemory; }
        const auto device = module.GetDevice<nvdevice>(channel_fd);
        if (!device || device->GetType() != Type::GpuChannel) { return NvResult::BadParameter; }
        const auto channel = std::static_pointer_cast<nvhost_gpu>(device);
        const auto result = channel->BindToTsg(fd, process, native_macros);
        if (result != NvResult::Success) { return result; }
        channels.emplace(channel_fd, channel);
    } else {
        const auto it = channels.find(channel_fd);
        if (it == channels.end()) { return NvResult::BadParameter; }
        it->second->UnbindFromTsg(fd);
        channels.erase(it);
    }
    std::copy(input.begin(), input.end(), output.begin());
    return NvResult::Success;
}

NvResult nvhost_tsg_gpu::Ioctl2(DeviceFD fd, Ioctl command, std::span<const u8> input,
                                std::span<const u8> inline_input, std::span<u8> output) {
    return NvResult::NotImplemented;
}

NvResult nvhost_tsg_gpu::Ioctl3(DeviceFD fd, Ioctl command, std::span<const u8> input,
                                std::span<u8> output, std::span<u8> inline_output) {
    return NvResult::NotImplemented;
}

} // namespace Service::Nvidia::Devices
