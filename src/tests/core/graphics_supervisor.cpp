// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include "core/hle/service/am/service/native_video_layout.h"
#include "core/hle/service/am/service/gpu_memory_registration.h"
#include <catch2/catch_test_macros.hpp>
#include "core/hle/service/am/service/graphics_supervisor_protocol.h"
#include "core/hle/service/am/service/graphics_syncpoint_cache.h"
#include "core/hle/service/am/service/native_image_layout.h"

using Service::AM::GraphicsSupervisorProtocol::DecodeBootstrap;

TEST_CASE("GPU sparse requests preserve native byte ranges and handles", "[graphics-supervisor]") {
    using namespace Service::AM;
    GpuSparseMapRequest request{};
    request.address = 0x8100000000;
    request.size = 0x12c00000;
    request.memory = 128;
    request.kind = request.fallback_kind = 6;
    auto entry = DecodeGpuSparseMap(request);
    REQUIRE(entry);
    auto range = Service::Nvidia::Devices::DecodeNativeSparseRemap(*entry);
    REQUIRE(range);
    REQUIRE(range->address == 0x8100000000);
    REQUIRE(range->size == 0x12c00000);
    REQUIRE(range->handle == 128);
    REQUIRE(range->source_offset == 0);
    request.source_offset = 0x12345000;
    request.cache = 1;
    entry = DecodeGpuSparseMap(request);
    REQUIRE(entry);
    REQUIRE(entry->flags == 0x1004);
    REQUIRE(Service::Nvidia::Devices::DecodeNativeSparseRemap(*entry)->source_offset == 0x12345000);
    for (const auto address : {0x8100000001ULL, 0xfffffffffffff000ULL}) {
        auto invalid = request;
        invalid.address = address;
        REQUIRE_FALSE(DecodeGpuSparseMap(invalid));
    }
    auto invalid = request;
    invalid.memory = 0;
    REQUIRE_FALSE(DecodeGpuSparseMap(invalid));
    invalid.source_offset = 0;
    REQUIRE(DecodeGpuSparseMap(invalid));
    invalid = request;
    invalid.read_only = 1;
    REQUIRE_FALSE(DecodeGpuSparseMap(invalid));
    invalid = request;
    invalid.size = 0;
    REQUIRE_FALSE(DecodeGpuSparseMap(invalid));
}

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

TEST_CASE("Graphics supervisor local bootstrap excludes remote memory requests", "[graphics-supervisor]") {
    using namespace Service::AM::GraphicsSupervisorProtocol;
    for (u64 flags : {0x8000000000000005ULL, 0x8000000000000006ULL, 0x8000000000000007ULL}) {
        BootstrapRequest request{0xffff8001, 0x51, flags, {}};
        REQUIRE(IsLocalBootstrap(request));
        for (size_t parameter = 0; parameter < request.parameters.size(); ++parameter) {
            auto remote = request;
            remote.parameters[parameter] = 0x1000;
            REQUIRE_FALSE(IsLocalBootstrap(remote));
        }
    }
    for (u64 flags : {0ULL, 5ULL, 6ULL, 7ULL, 0x8000000000000001ULL, 0x8000000000000004ULL, 0x800000000000000fULL}) {
        REQUIRE_FALSE(IsLocalBootstrap(BootstrapRequest{0xffff8001, 0x51, flags, {}}));
    }
}

TEST_CASE("Graphics supervisor accepts the supported display interface versions", "[graphics-supervisor]") {
    using Service::AM::GraphicsSupervisorProtocol::IsSupportedDisplaySession;
    REQUIRE(IsSupportedDisplaySession(0x300020000ULL));
    REQUIRE(IsSupportedDisplaySession(0x300030000ULL));
    REQUIRE(IsSupportedDisplaySession(0x300040000ULL));
    for (u64 flags : {0ULL, 0x300020001ULL, 0x300040001ULL, 0x300030001ULL, 0x400040000ULL}) {
        REQUIRE_FALSE(IsSupportedDisplaySession(flags));
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
    REQUIRE(requirements->alignment == 4096);
    REQUIRE(requirements->size % requirements->alignment == 0);
    preference[1] = 1; // The GPU descriptor uses the same block storage.
    REQUIRE(CalculateNativeImageRequirements(preference, 1)->size == requirements->size);
    preference[2] = 0; // A one-pixel image still occupies a complete block.
    preference[5] = 1;
    REQUIRE(CalculateNativeImageRequirements(preference, 1)->size == 8192);
}

TEST_CASE("Native 4K inter-module images retain complete block storage", "[graphics-supervisor]") {
    using Service::AM::CalculateNativeImageRequirements;
    std::array<u32, 16> preference{0, 2, 0x086f0eff, 0, 7, 240, 4, 1};
    const auto requirements = CalculateNativeImageRequirements(preference, 1);
    REQUIRE(requirements.has_value());
    REQUIRE(requirements->size == 33423360);
    REQUIRE(requirements->alignment == 4096);
    preference[5] = 239;
    REQUIRE_FALSE(CalculateNativeImageRequirements(preference, 1));
    preference[5] = 240;
    preference[1] = 1;
    REQUIRE(CalculateNativeImageRequirements(preference, 1)->size == requirements->size);
}

TEST_CASE("Native block image sizing rejects unsupported layouts and undersized pitches", "[graphics-supervisor]") {
    using Service::AM::CalculateNativeImageRequirements;
    const std::array<u32, 16> original{0, 2, 0x0437077f, 0, 7, 120, 1, 1};
    // The count word multiplies the size (firmware sizer); zero is rejected.
    REQUIRE_FALSE(CalculateNativeImageRequirements(original, 0));
    REQUIRE(CalculateNativeImageRequirements(original, 1)->size == 8847360);
    REQUIRE(CalculateNativeImageRequirements(original, 2)->size == 2 * 8847360);
    REQUIRE(CalculateNativeImageRequirements(original, 4)->size == 4 * 8847360);
    REQUIRE_FALSE(CalculateNativeImageRequirements(original, 0xffffffffU));
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

TEST_CASE("Graphics bootstrap variants preserve their native shader profiles", "[graphics-supervisor]") {
    using Service::AM::GraphicsSupervisorProtocol::NativeGpuProfileForBootstrap;
    REQUIRE(NativeGpuProfileForBootstrap(0x8000000000000005ULL) == 0x807);
    REQUIRE(NativeGpuProfileForBootstrap(0x8000000000000006ULL) == 0x808);
    REQUIRE(NativeGpuProfileForBootstrap(0x8000000000000007ULL) == 0x808);
    for (const u64 flags : {0ULL, 5ULL, 6ULL, 7ULL, 0x8000000000000004ULL,
                            0x800000000000000fULL}) {
        REQUIRE(NativeGpuProfileForBootstrap(flags) == 0);
    }
}

TEST_CASE("Native sparse region requests decode byte fields without IPC padding", "[graphics-supervisor]") {
    using namespace Service::AM;
    std::array<u32, 12> packet{0xe6, 10, 3, 0, 0x81, 0x12bfffff, 0x81,
                               0xffffff0c, 0x12340001, 0xffff0001, 0xabcd0001, 7};
    const auto request = DecodeGpuSparseRegion(packet);
    REQUIRE(request);
    REQUIRE(request->address_space == 3);
    REQUIRE(request->begin == 0x8100000000ULL);
    REQUIRE(request->end == 0x8112bfffffULL);
    REQUIRE(request->page_shift == 12);
    REQUIRE(request->allocation_mode == 1);
    REQUIRE(request->device == 1);
    REQUIRE(request->sync == 1);
    REQUIRE(request->notification == 7);
    REQUIRE(IsValidGpuSparseRegionRange(*request));
    for (const auto replacement : {0U, 0xe5U, 0xe7U}) {
        auto malformed = packet;
        malformed[0] = replacement;
        REQUIRE_FALSE(DecodeGpuSparseRegion(malformed));
    }
    packet[1] = 9;
    REQUIRE_FALSE(DecodeGpuSparseRegion(packet));
    REQUIRE_FALSE(DecodeGpuSparseRegion(std::span<const u32>{packet}.first(11)));
}

TEST_CASE("Native sparse regions reject misalignment, wraparound and invalid page shifts", "[graphics-supervisor]") {
    using namespace Service::AM;
    GpuSparseRegionRequest request{3, 0x8100000000ULL, 0x8112bfffffULL, 12, 1, 1, 1, 7};
    for (const u8 shift : {0, 11, 13, 31, 64, 255}) {
        auto invalid = request;
        invalid.page_shift = shift;
        REQUIRE_FALSE(IsValidGpuSparseRegionRange(invalid));
    }
    ++request.begin;
    REQUIRE_FALSE(IsValidGpuSparseRegionRange(request));
    --request.begin;
    --request.end;
    REQUIRE_FALSE(IsValidGpuSparseRegionRange(request));
    request.end = request.begin - 1;
    REQUIRE_FALSE(IsValidGpuSparseRegionRange(request));
    request.end = ~u64{0};
    REQUIRE_FALSE(IsValidGpuSparseRegionRange(request));
    request.end = request.begin + 0xffff;
    request.page_shift = 16;
    REQUIRE(IsValidGpuSparseRegionRange(request));
}

TEST_CASE("Native video context decodes its versioned local request", "[graphics-supervisor]") {
    std::array<u32, 5> packet{0x39a, 3, 0, 0x30000, 3};
    using Service::AM::GraphicsSupervisorProtocol::DecodeVideoContext;
    const auto request = DecodeVideoContext(packet);
    REQUIRE(request.has_value());
    REQUIRE_FALSE(request->remote_memory);
    REQUIRE(request->library_version == 0x300030000ULL);
    for (size_t size = 0; size < packet.size(); ++size) REQUIRE_FALSE(DecodeVideoContext(std::span{packet}.first(size)));
    packet[2] = 1;
    REQUIRE(DecodeVideoContext(packet)->remote_memory);
    packet[2] = 2;
    REQUIRE_FALSE(DecodeVideoContext(packet));
    packet[2] = 0;
    packet[1] = 2;
    REQUIRE_FALSE(DecodeVideoContext(packet));
    packet[1] = 3;
    packet[0] = 0x398;
    REQUIRE_FALSE(DecodeVideoContext(packet));
}

TEST_CASE("Native video block images include per-image stride padding", "[graphics-supervisor]") {
    using Service::AM::CalculateNativeVideoImageLayout;
    std::array<u32, 16> preference{0, 5, 0x059f09ff};
    const auto captured = CalculateNativeVideoImageLayout(preference, 4);
    REQUIRE(captured.has_value());
    REQUIRE(captured->size == 0x3c00000);
    REQUIRE(captured->alignment == 4096);
    REQUIRE(captured->row_bytes == 10240);
    REQUIRE(captured->image_bytes == 0xf00000);
    REQUIRE(captured->image_stride == 0xf00000);
    preference[2] = 0;
    REQUIRE(CalculateNativeVideoImageLayout(preference, 1)->size == 8192);
    REQUIRE(CalculateNativeVideoImageLayout(preference, 2)->size == 16384);
    preference[2] = (128U << 16) | 16;
    const auto padded = CalculateNativeVideoImageLayout(preference, 3);
    REQUIRE(padded->row_bytes == 128);
    REQUIRE(padded->image_bytes == 32768);
    REQUIRE(padded->size == 98304);
    REQUIRE_FALSE(CalculateNativeVideoImageLayout(preference, 0));
    REQUIRE_FALSE(CalculateNativeVideoImageLayout(std::span{preference}.first(15), 1));
    preference[3] = 1;
    REQUIRE_FALSE(CalculateNativeVideoImageLayout(preference, 1));
    preference[3] = 0;
    preference[2] = 0xffffffff;
    REQUIRE_FALSE(CalculateNativeVideoImageLayout(preference, 1));
    preference[2] = 0;
    preference[1] = 4;
    REQUIRE_FALSE(CalculateNativeVideoImageLayout(preference, 1));
}

TEST_CASE("Native decoder initialization validates the copied transfer-memory layout", "[graphics-supervisor]") {
    using Service::AM::GraphicsSupervisorProtocol::DecodeVideoDecoderInitialization;
    std::array<u32, 10> packet{0xd9, 0x80000006, 2, 0x3501a1, 0x8bfb000, 0, 3, 3, 2, 1};
    const auto request = DecodeVideoDecoderInitialization(packet);
    REQUIRE(request.has_value());
    REQUIRE(request->memory_size == 0x8bfb000);
    REQUIRE(request->codec == 3);
    REQUIRE(request->input_format == 3);
    REQUIRE(request->output_format == 2);
    REQUIRE(request->output_mode == 1);
    for (size_t size = 0; size < packet.size(); ++size)
        REQUIRE_FALSE(DecodeVideoDecoderInitialization(std::span{packet}.first(size)));
    packet[2] = 0;
    REQUIRE_FALSE(DecodeVideoDecoderInitialization(packet));
    packet[2] = 2;
    packet[1] = 6;
    REQUIRE_FALSE(DecodeVideoDecoderInitialization(packet));
}

TEST_CASE("Native video configuration validates geometry and shared-input capacity", "[graphics-supervisor]") {
    using Service::AM::GraphicsSupervisorProtocol::DecodeVideoDecoderConfiguration;
    std::array<u32, 13> packet{0x100075, 8, 10, 0, 0, 2560, 1440, 8, 0x800000};
    const auto request = DecodeVideoDecoderConfiguration(packet);
    REQUIRE(request.has_value());
    REQUIRE(request->width == 2560);
    REQUIRE(request->height == 1440);
    REQUIRE(request->frame_rate == 8);
    REQUIRE(request->input_capacity == 0x800000);
    for (size_t size = 0; size < packet.size(); ++size)
        REQUIRE_FALSE(DecodeVideoDecoderConfiguration(std::span{packet}.first(size)));
    for (u32 index : {5U, 6U, 7U, 8U}) {
        const auto saved = packet[index];
        packet[index] = 0;
        REQUIRE_FALSE(DecodeVideoDecoderConfiguration(packet));
        packet[index] = saved;
    }
    packet[7] = 61;
    REQUIRE_FALSE(DecodeVideoDecoderConfiguration(packet));
    packet[7] = 8;
    packet[10] = 1;
    REQUIRE_FALSE(DecodeVideoDecoderConfiguration(packet));
    packet[10] = 0;
    packet[0] = 0x75;
    REQUIRE_FALSE(DecodeVideoDecoderConfiguration(packet));
}

TEST_CASE("Native video registration accepts an image handle without extra flags", "[graphics-supervisor]") {
    using Service::AM::GraphicsSupervisorProtocol::DecodeVideoImageRegistration;
    std::array<u32, 6> packet{0x147, 4, 5, 0, 0, 0};
    REQUIRE(DecodeVideoImageRegistration(packet) == 5U);
    for (size_t size = 0; size < packet.size(); ++size)
        REQUIRE_FALSE(DecodeVideoImageRegistration(std::span{packet}.first(size)));
    for (size_t i = 3; i < packet.size(); ++i) {
        packet[i] = 1;
        REQUIRE_FALSE(DecodeVideoImageRegistration(packet));
        packet[i] = 0;
    }
    packet[2] = 0;
    REQUIRE_FALSE(DecodeVideoImageRegistration(packet));
}

TEST_CASE("Native syncpoint waits preserve modular fence comparisons", "[graphics-supervisor]") {
    using namespace Service::AM::GraphicsSupervisorProtocol;
    std::array<u32, 7> packet{0xd9, 5, 0, ~3U, 1, 0, 0};
    const auto request = DecodeSyncpointWait(packet);
    REQUIRE(request);
    REQUIRE(request->id == 3);
    REQUIRE(request->threshold == 1);
    for (size_t size = 0; size < packet.size(); ++size)
        REQUIRE_FALSE(DecodeSyncpointWait(std::span{packet}.first(size)));
    packet[6] = 0xabcdef01;
    REQUIRE(DecodeSyncpointWait(packet)->wide);
    packet[6] = 2;
    REQUIRE_FALSE(DecodeSyncpointWait(packet));
    REQUIRE_FALSE(IsSyncpointWaitSatisfied(0, 1, false));
    REQUIRE(IsSyncpointWaitSatisfied(1, 1, false));
    REQUIRE(IsSyncpointWaitSatisfied(0, 0xffffffff, false));
    REQUIRE_FALSE(IsSyncpointWaitSatisfied(0xffffffff, 0, false));
    REQUIRE(IsSyncpointWaitSatisfied(1, 0x100000001ULL, false));
    REQUIRE_FALSE(IsSyncpointWaitSatisfied(1, 0x100000001ULL, true));
    REQUIRE(IsSyncpointWaitSatisfied(2, 1, true));
}

TEST_CASE("Native BT709 output preserves video black and white levels", "[graphics-supervisor]") {
    REQUIRE(Service::AM::ConvertNativeVideoYuv709(16, 128, 128, false) == std::array<u8, 4>{0, 0, 0, 255});
    REQUIRE(Service::AM::ConvertNativeVideoYuv709(235, 128, 128, false) == std::array<u8, 4>{255, 255, 255, 255});
    REQUIRE(Service::AM::ConvertNativeVideoYuv709(0, 128, 128, true) == std::array<u8, 4>{0, 0, 0, 255});
    REQUIRE(Service::AM::ConvertNativeVideoYuv709(255, 128, 128, true) == std::array<u8, 4>{255, 255, 255, 255});
    const auto red = Service::AM::ConvertNativeVideoYuv709(63, 102, 240, false);
    REQUIRE(red[0] >= 254);
    REQUIRE(red[1] <= 1);
    REQUIRE(red[2] <= 1);
}
