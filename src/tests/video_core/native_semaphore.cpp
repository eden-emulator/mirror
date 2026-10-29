// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <optional>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "common/common_types.h"
#include "video_core/native_semaphore.h"
#include "video_core/native_qmd.h"
#include "video_core/native_vprs.h"

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

TEST_CASE("Native QMD third release wakes graphics only after its real dependency completes", "[native-gpu]") {
    Tegra::NativeQmdScheduler scheduler;
    Tegra::NativeQmdScheduler::Descriptor descriptor{};
    descriptor[4] = 0x8040; // Two dependencies.
    descriptor[18] = 0x30;
    descriptor[24] = 0x06428010;
    descriptor[25] = 0x40880029;
    descriptor[26] = 0x210;
    descriptor[52] = 0x1609b004;
    descriptor[53] = 0x40800029;
    descriptor[54] = 0x2a30;
    std::vector<std::pair<std::uint64_t, std::uint32_t>> releases;
    const auto read = [&](std::uint64_t, auto& out) { out = descriptor; return true; };
    const auto release = [&](std::uint64_t address, std::uint32_t payload) {
        releases.emplace_back(address, payload);
        return true;
    };
    REQUIRE(scheduler.Signal(0x2915fd0700ULL, 3, read, release).empty());
    REQUIRE(releases.empty());
    REQUIRE(scheduler.Signal(0x2915fd0700ULL, 7, read, release).empty());
    REQUIRE(releases.empty());
    REQUIRE(scheduler.Signal(0x2915fd0700ULL, 7, read, release).empty());
    REQUIRE(releases == std::vector<std::pair<std::uint64_t, std::uint32_t>>{
        {0x2906428010ULL, 0x210}, {0x291609b004ULL, 0x2a30}});
    REQUIRE(Tegra::NativeSemaphoreWait{0x291609b004ULL, 0x2a30, 1}.Matches(releases.back().second));
}

TEST_CASE("Invalid third QMD releases cannot publish earlier release slots", "[native-gpu]") {
    for (const std::uint32_t bad : {0x60800029U, 0x80800029U, 0x41800029U, 0x44900029U}) {
        Tegra::NativeQmdScheduler scheduler;
        Tegra::NativeQmdScheduler::Descriptor descriptor{};
        descriptor[18] = 0x30;
        descriptor[24] = 0x1000;
        descriptor[25] = 0x40800029;
        descriptor[53] = bad;
        std::uint32_t released{};
        REQUIRE_FALSE(scheduler.Signal(0x10000, 3,
            [&](std::uint64_t, auto& out) { out = descriptor; return true; },
            [&](std::uint64_t, std::uint32_t) { ++released; return true; }).empty());
        REQUIRE(released == 0);
    }
}

TEST_CASE("Shared native QMD history survives long streams without losing dependencies", "[native-gpu]") {
    using Scheduler = Tegra::NativeQmdScheduler;
    Scheduler scheduler;
    Scheduler::Descriptor descriptor{};
    descriptor[18] = 0x30;
    descriptor[24] = 0x1000;
    descriptor[25] = 0x40800000;
    descriptor[26] = 1;
    const auto read = [&](Scheduler::Address, auto& out) { out = descriptor; return true; };
    std::uint32_t releases{};
    const auto release = [&](Scheduler::Address, Scheduler::Word) { ++releases; return true; };
    constexpr Scheduler::Address deferred = 0x10000;
    constexpr Scheduler::Address completed = 0x100000;
    REQUIRE(scheduler.Signal(deferred, 12, read, release).empty());
    for (std::uint64_t index = 0; index < 8192; ++index) {
        REQUIRE(scheduler.Signal(completed + index * 256, 3, read, release).empty());
    }
    REQUIRE(releases == 8192);
    REQUIRE(scheduler.Inspect(completed).flags == 7);
    REQUIRE(scheduler.Signal(completed, 2, read, release).empty());
    REQUIRE(releases == 8192);
    REQUIRE(scheduler.Inspect(deferred).deferred == 1);
    REQUIRE(scheduler.Signal(deferred, 3, read, release).empty());
    REQUIRE(releases == 8192);
    REQUIRE(scheduler.Inspect(deferred).dependencies == 1);
    REQUIRE(scheduler.Signal(deferred, 7, read, release).empty());
    REQUIRE(releases == 8193);
    REQUIRE(scheduler.Signal(completed, 12, read, release).empty());
    REQUIRE(scheduler.Signal(completed, 3, read, release).empty());
    REQUIRE(releases == 8193);
    REQUIRE(scheduler.Signal(completed, 7, read, release).empty());
    REQUIRE(releases == 8194);
}

TEST_CASE("Native VPRS gating checks every table without interpreting reserved control bits", "[native-gpu]") {
    std::array<u32,16> controls{};
    REQUIRE(Tegra::NativeVprsEnabledTables(controls) == 0);
    controls.fill(0xfffffffe);
    REQUIRE(Tegra::NativeVprsEnabledTables(controls) == 0);
    for (u32 table = 0; table < controls.size(); ++table) {
        controls[table] |= 1;
        REQUIRE(Tegra::NativeVprsEnabledTables(controls) == (1U << table));
        controls[table] &= ~1U;
    }
    controls[0] |= 1;
    controls[15] |= 1;
    REQUIRE(Tegra::NativeVprsEnabledTables(controls) == 0x8001);
}

TEST_CASE("Native VPRS full-rate fallback preserves coverage and rejects side-effect changes", "[native-gpu]") {
    std::array<u32,16> controls{}, rates_a{}, rates_b{};
    controls[0] = controls[1] = controls[2] = 1;
    rates_a[0] = 0x55555555;
    rates_a[1] = 0x55555558;
    rates_a[2] = 0x5555555b;
    rates_b[0] = rates_b[1] = rates_b[2] = 0x55555555;
    const auto captured = Tegra::ClassifyNativeVprs(controls, rates_a, rates_b);
    REQUIRE(captured.enabled == 7);
    REQUIRE(captured.coarse == 6);
    REQUIRE(captured.unsupported == 0);
    REQUIRE(captured.AllowsFullRate(false));
    REQUIRE_FALSE(captured.AllowsFullRate(true));
    for (u32 rate = 0; rate < 16; ++rate) for (u32 index = 0; index < 16; ++index) {
        auto a = rates_a, b = rates_b;
        auto& word = index < 8 ? a[0] : b[0];
        const u32 shift = (index & 7) * 4;
        word = (word & ~(15U << shift)) | (rate << shift);
        const auto changed = Tegra::ClassifyNativeVprs(controls, a, b);
        REQUIRE(changed.AllowsFullRate(false) == (rate >= 5 && rate <= 11));
        REQUIRE((changed.unsupported & 1U) == (rate < 5 || rate > 11 ? 1U : 0U));
    }
    controls[1] = controls[2] = 0;
    REQUIRE(Tegra::ClassifyNativeVprs(controls, rates_a, rates_b).AllowsFullRate(true));
    controls.fill(0);
    rates_a.fill(0); rates_b.fill(0);
    REQUIRE(Tegra::ClassifyNativeVprs(controls, rates_a, rates_b).AllowsFullRate(true));
}
