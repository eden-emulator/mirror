// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include "core/hle/service/am/service/gpu_memory_registration.h"
#include <catch2/catch_test_macros.hpp>
#include "core/hle/service/am/service/graphics_supervisor_protocol.h"
#include "core/hle/service/am/service/graphics_syncpoint_cache.h"
#include "core/hle/service/am/service/native_image_layout.h"

using Service::AM::GraphicsSupervisorProtocol::DecodeBootstrap;

TEST_CASE("Graphics supervisor decodes the captured TIPC bootstrap", "[graphics-supervisor]") {
    const std::array<u32, 16> packet{0x14, 0x8000000c, 2, 0xffff8001,
                                   0x51, 0, 5, 0x80000000};
    const auto request = DecodeBootstrap(packet);
    REQUIRE(request.has_value());
    REQUIRE(request->process_handle == 0xffff8001);
    REQUIRE(request->applet_resource_user_id == 0x51);
    REQUIRE(request->flags == 0x8000000000000005ULL);
    REQUIRE(request->parameters == std::array<u64, 4>{});
    for (size_t length = 0; length < packet.size(); ++length) {
        REQUIRE_FALSE(DecodeBootstrap(std::span{packet}.first(length)));
    }
}

TEST_CASE("Graphics supervisor rejects incompatible packet layouts", "[graphics-supervisor]") {
    const std::array<u32, 16> original{0x14, 0x8000000c, 2, 0xffff8001,
                                     0x51, 0, 5, 0x80000000};
    for (const auto replacement : {0U, 0x15U, 0x10014U}) {
        auto packet = original;
        packet[0] = replacement;
        REQUIRE_FALSE(DecodeBootstrap(packet));
    }
    for (const auto replacement : {0U, 0x8000000bU, 0x8000000dU}) {
        auto packet = original;
        packet[1] = replacement;
        REQUIRE_FALSE(DecodeBootstrap(packet));
    }
    for (const auto replacement : {0U, 1U, 3U, 0x20U}) {
        auto packet = original;
        packet[2] = replacement;
        REQUIRE_FALSE(DecodeBootstrap(packet));
    }
}


TEST_CASE("Graphics syncpoint cache extends real counter values across wraparound", "[graphics-supervisor]") {
    using namespace Service::AM::GraphicsSyncpointCache;
    REQUIRE(ExtendCounter(Unallocated, 7) == 7);
    REQUIRE(ExtendCounter(7, 12) == 12);
    REQUIRE(ExtendCounter(0xfffffffeULL, 0xffffffffU) == 0xffffffffULL);
    REQUIRE(ExtendCounter(0xffffffffULL, 0) == 0x100000000ULL);
    REQUIRE(ExtendCounter(0x1ffffffffULL, 3) == 0x200000003ULL);
    REQUIRE(Header[0] == 1);
    REQUIRE(Header[2] + 1 == CounterCount);
    REQUIRE(Header[3] % 8 == 0);
    REQUIRE(Header[3] + CounterCount * 8 <= Header[5]);
    REQUIRE(Header[7] + GroupCount * 8 <= 0x2000);
}

TEST_CASE("Graphics syncpoint cache retains wrap history across unallocated snapshots", "[graphics-supervisor]") {
    using namespace Service::AM::GraphicsSyncpointCache;
    CounterValue counter;
    REQUIRE(counter.Observe(std::nullopt) == Unallocated);
    REQUIRE(counter.Observe(0xfffffffeU) == 0xfffffffeULL);
    REQUIRE(counter.Observe(1U) == 0x100000001ULL);
    REQUIRE(counter.Observe(std::nullopt) == Unallocated);
    REQUIRE(counter.Observe(3U) == 0x100000003ULL);
}


TEST_CASE("Native block image storage covers every padded GOB", "[graphics-supervisor]") {
    using Service::AM::CalculateNativeImageRequirements;
    std::array<u32, 16> preference{0, 2, 0x0437077f, 0, 7, 120, 1, 1};
    const auto requirements = CalculateNativeImageRequirements(preference, 1);
    REQUIRE(requirements.has_value());
    REQUIRE(requirements->size == 8847360);
    REQUIRE(requirements->alignment == 8192);
    REQUIRE(requirements->size % requirements->alignment == 0);
    preference[1] = 1; // The GPU descriptor uses the same block storage.
    REQUIRE(CalculateNativeImageRequirements(preference, 1)->size == requirements->size);
    preference[2] = 0; // A one-pixel image still occupies a complete block.
    preference[5] = 1;
    REQUIRE(CalculateNativeImageRequirements(preference, 1)->size == 8192);
}

TEST_CASE("Native block image sizing rejects unsupported layouts and undersized pitches", "[graphics-supervisor]") {
    using Service::AM::CalculateNativeImageRequirements;
    const std::array<u32, 16> original{0, 2, 0x0437077f, 0, 7, 120, 1, 1};
    for (const auto count : {0U, 2U, 0xffffffffU}) {
        REQUIRE_FALSE(CalculateNativeImageRequirements(original, count));
    }
    for (const auto index : {0, 1, 3, 6, 7, 8, 15}) {
        auto preference = original;
        preference[index] ^= 4;
        REQUIRE_FALSE(CalculateNativeImageRequirements(preference, 1));
    }
    for (const auto block : {0U, 2U, 9U, 32U, 0xffffffffU}) {
        auto preference = original;
        preference[4] = block;
        REQUIRE_FALSE(CalculateNativeImageRequirements(preference, 1));
    }
    for (const auto pitch : {0U, 119U, 4097U, 0xffffffffU}) {
        auto preference = original;
        preference[5] = pitch;
        REQUIRE_FALSE(CalculateNativeImageRequirements(preference, 1));
    }
    REQUIRE_FALSE(CalculateNativeImageRequirements(std::span{original}.first(15), 1));
}

TEST_CASE("GPU memory registration ignores padding around its byte selectors", "[graphics-supervisor]") {
    std::array<u32, 8> packet{0x75, 6, 0x1fdd6000, 0x21, 0x1fddcfff, 0x21, 0xaabbcc00, 0x101};
    const auto request = Service::AM::DecodeGpuMemoryRegistration(packet);
    REQUIRE(request.has_value());
    REQUIRE(request->begin == 0x211fdd6000ULL);
    REQUIRE(request->end == 0x211fddcfffULL);
    REQUIRE(request->flags == 0);
    REQUIRE(request->device == 1);
    packet[7] = 0xffffff02;
    REQUIRE(Service::AM::DecodeGpuMemoryRegistration(packet)->device == 2);
    packet[0] = 0x76;
    REQUIRE_FALSE(Service::AM::DecodeGpuMemoryRegistration(packet));
    REQUIRE_FALSE(Service::AM::DecodeGpuMemoryRegistration(std::span<const u32>{packet.data(), 7}));
}

TEST_CASE("Native presentation slots ignore leftover IPC padding", "[graphics-supervisor]") {
    using Service::AM::GraphicsSupervisorProtocol::DecodeBufferSlot;
    REQUIRE(DecodeBufferSlot(0) == 0);
    REQUIRE(DecodeBufferSlot(0x4f434601) == 1);
    REQUIRE(DecodeBufferSlot(0xffffff0e) == 14);
    REQUIRE(DecodeBufferSlot(0x4f43460f) == 15);
}
