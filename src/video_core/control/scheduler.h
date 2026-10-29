// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>
#include <mutex>
#include <vector>
#include "common/container/unordered_map.h"

#include "video_core/dma_pusher.h"

namespace Tegra {

class GPU;

namespace Control {

struct ChannelState;

class Scheduler {
public:
    void Push(GPU& gpu, s32 channel, CommandList&& entries);

    void ResumePending(GPU& gpu);
    [[nodiscard]] bool HasPendingNativeChannels() const { return !pending_native_channels.empty(); }

    void DeclareChannel(std::shared_ptr<ChannelState> new_channel);

private:
    ::Common::unordered_map<s32, std::shared_ptr<ChannelState>> channels;
    std::mutex scheduling_guard;
    std::vector<std::shared_ptr<ChannelState>> pending_native_channels;
};

} // namespace Control

} // namespace Tegra
