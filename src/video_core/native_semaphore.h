// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

namespace Tegra {
struct NativeSemaphoreWait {
    std::uint64_t address;
    std::uint32_t value;
    std::uint32_t operation;

    static constexpr bool Supported(std::uint32_t operation) {
        return operation == 1 || operation == 4 || operation == 8;
    }
    constexpr bool Matches(std::uint32_t current) const {
        switch (operation) {
        case 1: return current == value;
        case 4: return current >= value;
        case 8: return (current & value) != 0;
        default: return false;
        }
    }
};

// Revisit dependencies after another channel makes progress. A blocked
// channel keeps its FIFO cursor until its real semaphore predicate is true.
template <class Channels, class Ready, class Dispatch>
std::size_t ResumeReadyNativeChannels(const Channels& channels, Ready ready, Dispatch dispatch) {
    std::size_t resumed = 0;
    for (std::size_t pass = 0; pass < channels.size(); ++pass) {
        bool progress = false;
        for (const auto& channel : channels) {
            if (!ready(channel)) continue;
            dispatch(channel);
            ++resumed;
            progress = true;
        }
        if (!progress) break;
    }
    return resumed;
}
} // namespace Tegra
