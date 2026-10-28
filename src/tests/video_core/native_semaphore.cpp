// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <optional>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "common/common_types.h"
#include "video_core/native_semaphore.h"

TEST_CASE("Native semaphore predicates require the actual producer value", "[native-gpu]") {
    const Tegra::NativeSemaphoreWait equal{0x1000, 840, 1};
    REQUIRE_FALSE(equal.Matches(0));
    REQUIRE_FALSE(equal.Matches(839));
    REQUIRE(equal.Matches(840));
    REQUIRE_FALSE(equal.Matches(841));
    const Tegra::NativeSemaphoreWait gequal{0x1000, 840, 4};
    REQUIRE_FALSE(gequal.Matches(0));
    REQUIRE_FALSE(gequal.Matches(839));
    REQUIRE(gequal.Matches(840));
    REQUIRE(gequal.Matches(841));
    const Tegra::NativeSemaphoreWait mask{0x1000, 0x24, 8};
    REQUIRE_FALSE(mask.Matches(0));
    REQUIRE_FALSE(mask.Matches(0x10));
    REQUIRE(mask.Matches(4));
    REQUIRE(mask.Matches(0x20));
    REQUIRE_FALSE(Tegra::NativeSemaphoreWait::Supported(0));
    REQUIRE_FALSE(Tegra::NativeSemaphoreWait::Supported(2));
    REQUIRE_FALSE(Tegra::NativeSemaphoreWait{0x1000, 0, 2}.Matches(0));
}

TEST_CASE("Native channels resume in dependency order without completing blocked work", "[native-gpu]") {
    const std::array<size_t, 3> channels{0, 1, 2};
    std::array<std::optional<Tegra::NativeSemaphoreWait>, 3> waits{
        Tegra::NativeSemaphoreWait{0x1000, 1, 4},
        Tegra::NativeSemaphoreWait{0x2000, 1, 1},
        Tegra::NativeSemaphoreWait{0x3000, 1, 4},
    };
    std::array<u32, 3> words{};
    std::array<u32, 3> following_commands{};
    std::vector<size_t> order;
    const auto ready = [&](size_t channel) {
        return waits[channel] && waits[channel]->Matches(words[channel]);
    };
    const auto dispatch = [&](size_t channel) {
        REQUIRE(ready(channel));
        waits[channel].reset();
        ++following_commands[channel];
        order.push_back(channel);
        if (channel) words[channel - 1] = 1;
    };
    REQUIRE(Tegra::ResumeReadyNativeChannels(channels, ready, dispatch) == 0);
    REQUIRE(order.empty());
    REQUIRE(following_commands == std::array<u32, 3>{0, 0, 0});
    // A real independent producer releases the last channel. Its remaining
    // FIFO commands release the next channel, then the original consumer.
    words[2] = 1;
    REQUIRE(Tegra::ResumeReadyNativeChannels(channels, ready, dispatch) == 3);
    REQUIRE(order == std::vector<size_t>{2, 1, 0});
    REQUIRE(following_commands == std::array<u32, 3>{1, 1, 1});
    REQUIRE(Tegra::ResumeReadyNativeChannels(channels, ready, dispatch) == 0);
    REQUIRE(following_commands == std::array<u32, 3>{1, 1, 1});
}
