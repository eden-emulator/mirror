// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include "video_core/macro.h"
#include "video_core/native_qmd.h"
#include "video_core/dirty_flags.h"
#include <catch2/catch_test_macros.hpp>
#include "core/hle/kernel/k_memory_block.h"
#include "core/hle/service/nvdrv/devices/native_gpu_kind.h"
#include "core/hle/service/nvdrv/devices/native_gpu_context.h"
#include "core/hle/service/nvdrv/devices/native_gpu_characteristics.h"
#include "core/hle/service/nvdrv/devices/gpu_address_space_layout.h"

TEST_CASE("Native GPU characteristics replace topology with ioctl interface levels",
          "[native-gpu]") {
    std::array<u8, 0xa0> legacy;
    legacy.fill(0xcc);
    const u32_le arch{0x120};
    const u32_le impl{0xb};
    std::memcpy(legacy.data(), &arch, sizeof(arch));
    std::memcpy(legacy.data() + 4, &impl, sizeof(impl));
    const u32_le available{0x30000};
    std::memcpy(legacy.data() + 0x34, &available, sizeof(available));
    const auto native = Service::Nvidia::Devices::MakeNativeGpuCharacteristics(legacy);
    REQUIRE(native.size() == 0xd0);
    u32_le native_arch, native_impl;
    std::memcpy(&native_arch, native.data(), sizeof(native_arch));
    std::memcpy(&native_impl, native.data() + 4, sizeof(native_impl));
    REQUIRE(static_cast<u32>(native_arch) == 0x170);
    REQUIRE(static_cast<u32>(native_impl) == 0xe);
    REQUIRE(std::equal(legacy.begin() + 8, legacy.begin() + 0x28, native.begin() + 8));
    REQUIRE(std::equal(legacy.begin() + 0x2c, legacy.begin() + 0x34, native.begin() + 0x2c));
    REQUIRE(std::equal(legacy.begin() + 0x38, legacy.begin() + 0x3c, native.begin() + 0x38));
    REQUIRE(std::equal(legacy.begin() + 0x44, legacy.begin() + 0x50, native.begin() + 0x44));
    REQUIRE(std::equal(legacy.begin() + 0x58, legacy.begin() + 0x70, native.begin() + 0x58));
    u32_le sm_version, spa_version;
    std::memcpy(&sm_version, native.data() + 0x3c, sizeof(sm_version));
    std::memcpy(&spa_version, native.data() + 0x40, sizeof(spa_version));
    REQUIRE(static_cast<u32>(sm_version) == 0x807);
    REQUIRE(static_cast<u32>(spa_version) == 0x807);
    u32_le available_sizes;
    std::memcpy(&available_sizes, native.data() + 0x34, sizeof(available_sizes));
    REQUIRE(static_cast<u32>(available_sizes) == 0x11000);
    u32_le big_page_size;
    std::memcpy(&big_page_size, native.data() + 0x28, sizeof(big_page_size));
    REQUIRE(static_cast<u32>(big_page_size) == 0x1000);
    const auto level = [&](size_t offset) {
        u16_le value;
        std::memcpy(&value, native.data() + offset, sizeof(value));
        return static_cast<s16>(static_cast<u16>(value));
    };
    REQUIRE(level(0x70) == 0x1c);
    REQUIRE(level(0x72) == 2);
    u64_le flags;
    std::memcpy(&flags, native.data() + 0x50, sizeof(flags));
    REQUIRE((static_cast<u64>(flags) & (1ULL << 8)) != 0);
    REQUIRE((static_cast<u64>(flags) & ~((1ULL << 8) | (1ULL << 26) | (1ULL << 34) | (0xfULL << 40))) == (0xccccccccccccccccULL & ~((1ULL << 8) | (1ULL << 26) | (1ULL << 34) | (0xfULL << 40))));
    REQUIRE((static_cast<u64>(flags) & (0xfULL << 40)) == (0xfULL << 40));
    REQUIRE((static_cast<u64>(flags) & (1ULL << 34)) != 0);
    REQUIRE(level(0x74) == -1);
    REQUIRE(level(0x76) == 0x1d);
    // The captured SDK rejects the old topology value 2 here, requiring >= 6.
    REQUIRE(level(0x78) == 0x14);
    REQUIRE(std::all_of(native.begin() + 0x7a, native.end(), [](u8 b) { return b == 0; }));
}

TEST_CASE("Native GPU VA layout accommodates the captured NVN bootstrap reservation",
          "[native-gpu]") {
    using Service::Nvidia::Devices::GetGpuAddressSpaceLayout;
    constexpr auto native = GetGpuAddressSpaceLayout(true);
    constexpr auto legacy = GetGpuAddressSpaceLayout(false);
    REQUIRE(native.split - native.start >= 0x7ff8000000ULL + (1ULL << 32));
    REQUIRE(native.start == legacy.start);
    REQUIRE(native.end == 1ULL << 40);
    REQUIRE(native.end > native.split);
    REQUIRE(legacy.split == 1ULL << 34);
    REQUIRE(legacy.end == 1ULL << 37);
}

TEST_CASE("GPU region addresses retain bits above the 32-bit page index", "[native-gpu]") {
    using Service::Nvidia::Devices::GpuPageAddress;
    REQUIRE(GpuPageAddress(0x400000, 17) == 0x8000000000ULL);
    REQUIRE(GpuPageAddress(0x20000, 17) == 0x400000000ULL);
    REQUIRE(GpuPageAddress(0x8000, 12) == 0x8000000ULL);
}

TEST_CASE("GPU memory eligibility survives splitting and remains separate from cache attributes",
          "[native-gpu]") {
    using namespace Kernel;
    KMemoryBlock right{KProcessAddress{0x10000}, 4, KMemoryState::Normal,
                       KMemoryPermission::UserReadWrite, KMemoryAttribute::Uncached};
    right.UpdateAttribute(KMemoryAttribute::GpuAccessible, KMemoryAttribute::GpuAccessible);
    KMemoryBlock left;
    right.Split(&left, KProcessAddress{0x12000});
    REQUIRE(left.GetNumPages() == 2);
    REQUIRE(right.GetNumPages() == 2);
    REQUIRE(left.GetMemoryInfo().GetSvcMemoryInfo().attribute ==
            (Svc::MemoryAttribute::Uncached | Svc::MemoryAttribute::GpuAccessible));
    REQUIRE(right.GetAttribute() == left.GetAttribute());
    right.UpdateAttribute(KMemoryAttribute::GpuAccessible, KMemoryAttribute::None);
    REQUIRE(right.GetAttribute() == KMemoryAttribute::Uncached);
    REQUIRE(left.GetAttribute() ==
            (KMemoryAttribute::Uncached | KMemoryAttribute::GpuAccessible));
    REQUIRE(right.GetPermission() == KMemoryPermission::UserReadWrite);
    REQUIRE(right.GetState() == KMemoryState::Normal);
}

TEST_CASE("Native 4 KiB allocation preference retains a separate large-page GPU pool", "[native-gpu]") {
    using Service::Nvidia::Devices::GetGpuAddressSpaceBigPageSize;
    REQUIRE(GetGpuAddressSpaceBigPageSize(true, 0x1000) == 0x10000);
    REQUIRE(GetGpuAddressSpaceBigPageSize(true, 0x20000) == 0x20000);
    REQUIRE(GetGpuAddressSpaceBigPageSize(false, 0x1000) == 0x1000);
    REQUIRE(GetGpuAddressSpaceBigPageSize(false, 0) == 0);
}

TEST_CASE("Native generic memory kind is translated rather than treated as legacy depth memory",
          "[native-gpu]") {
    using Service::Nvidia::Devices::TranslateNativeUncompressedKind;
    REQUIRE(TranslateNativeUncompressedKind(0, 0) == static_cast<u8>(Tegra::PTEKind::PITCH));
    REQUIRE(TranslateNativeUncompressedKind(6, 6) == static_cast<u8>(Tegra::PTEKind::GENERIC_16BX2));
    REQUIRE(TranslateNativeUncompressedKind(6, 6) != static_cast<u8>(Tegra::PTEKind::Z16_MS16_2C));
    REQUIRE_FALSE(TranslateNativeUncompressedKind(8, 6));
    REQUIRE_FALSE(TranslateNativeUncompressedKind(7, 7));
    REQUIRE_FALSE(TranslateNativeUncompressedKind(0xfe, 0xfe));
}

TEST_CASE("Native mappings report the actual selected memory kind", "[native-gpu]") {
    using Service::Nvidia::Devices::SelectNativeMemoryKind;
    const auto normal = SelectNativeMemoryKind(6, 6);
    REQUIRE(normal.has_value());
    REQUIRE(normal->selected == 6);
    REQUIRE(normal->backend == 0xfe);
    const auto fallback = SelectNativeMemoryKind(8, 6);
    REQUIRE(fallback.has_value());
    REQUIRE(fallback->selected == 6);
    REQUIRE(fallback->backend == normal->backend);
    REQUIRE(SelectNativeMemoryKind(0, 0)->selected == 0);
    const auto depth = SelectNativeMemoryKind(0xc, 3);
    REQUIRE(depth.has_value());
    REQUIRE(depth->selected == 3);
    REQUIRE(depth->backend == static_cast<u8>(Tegra::PTEKind::S8Z24));
    REQUIRE(SelectNativeMemoryKind(3, 3)->backend == depth->backend);
    const auto depth16 = SelectNativeMemoryKind(0xb, 1);
    REQUIRE(depth16.has_value());
    REQUIRE(depth16->selected == 1);
    REQUIRE(depth16->backend == static_cast<u8>(Tegra::PTEKind::Z16));
    REQUIRE(SelectNativeMemoryKind(1, 1)->backend == depth16->backend);
    REQUIRE_FALSE(SelectNativeMemoryKind(0xb, 6));
    REQUIRE_FALSE(SelectNativeMemoryKind(0xc, 6));
    REQUIRE_FALSE(SelectNativeMemoryKind(8, 8));
    REQUIRE_FALSE(SelectNativeMemoryKind(8, 0));
    REQUIRE_FALSE(SelectNativeMemoryKind(9, 6));
}

TEST_CASE("Native GPU context allocations classify explicit known engine identities", "[native-gpu]") {
    using Service::Nvidia::Devices::GetNativeGpuContextSlot;
    REQUIRE(GetNativeGpuContextSlot(0xc797) == 1U);
    REQUIRE(GetNativeGpuContextSlot(0xc7c0) == 2U);
    REQUIRE_FALSE(GetNativeGpuContextSlot(0xa143));
    REQUIRE(GetNativeGpuContextSlot(0xc7b5) == 4U);
    REQUIRE(GetNativeGpuContextSlot(0xc56f) == 5U);
    REQUIRE_FALSE(GetNativeGpuContextSlot(0));
    REQUIRE_FALSE(GetNativeGpuContextSlot(0xb197));
    REQUIRE_FALSE(GetNativeGpuContextSlot(0xffffc797));
    REQUIRE_FALSE(GetNativeGpuContextSlot(0xdeadbeef));
}

TEST_CASE("Native MME uploads use instruction addresses and bypass Maxwell code storage", "[native-gpu]") {
    Tegra::MacroEngine engine{true};
    engine.native_turing = true;
    engine.ClearCode(8);
    engine.AddCode(8, 0x11223344);
    engine.AddCode(8, 0x55667788);
    engine.AddCode(8, 0x99aabbcc);
    REQUIRE(engine.native_code[8] == 0);
    REQUIRE(engine.native_code[24] == 0x11223344);
    REQUIRE(engine.native_code[25] == 0x55667788);
    REQUIRE(engine.native_code[26] == 0x99aabbcc);
    REQUIRE(engine.uploaded_macro_code.empty());
    engine.ClearCode(0xffffffff);
    engine.AddCode(0xffffffff, 1);
    REQUIRE(engine.native_code[24] == 0x11223344);
    engine.native_turing = false;
    engine.AddCode(8, 7);
    REQUIRE(engine.uploaded_macro_code.at(8).front() == 7);
}

TEST_CASE("Native MME loops consume the exact parameter count, including zero iterations", "[native-gpu]") {
    Tegra::TuringMacroInterpreter interpreter;
    constexpr std::array<u32, 15> code{
        0x00000003U, 0x18c00000U, 0x31c00300U,
        0x00000003U, 0x18c00000U, 0xb00c5f00U,
        0x00740003U, 0x18c00740U, 0x318c0300U,
        0x00000003U, 0x18c00000U, 0x318c0301U,
        0x00000003U, 0x18c00000U, 0x318c0300U,
    };
    std::vector<u32> output;
    auto emit = [&](u32 method, u32 value) { REQUIRE(method == 0xd00); output.push_back(value); return true; };
    auto read = [](u32) { return 0U; };
    const std::array<u32, 1> zero{0};
    auto result = interpreter.Execute(code, 0, zero, read, emit);
    REQUIRE(result.success);
    REQUIRE(result.consumed == 1);
    REQUIRE(output.empty());
    const std::array<u32, 3> two{2, 99, 88};
    result = interpreter.Execute(code, 0, two, read, emit);
    REQUIRE(result.success);
    REQUIRE(result.consumed == 3);
    REQUIRE(output == std::vector<u32>{99, 88});
    result = interpreter.Execute(code, 0, std::span<const u32>{}, read, emit);
    REQUIRE_FALSE(result.success);
    REQUIRE(result.error == "parameter FIFO underflow");
    result = interpreter.Execute(code, 5, zero, read, emit);
    REQUIRE_FALSE(result.success);
    REQUIRE(result.error == "instruction outside uploaded RAM");
}

TEST_CASE("Native MME pairs ALU carry with two incrementing outputs", "[native-gpu]") {
    Tegra::TuringMacroInterpreter interpreter;
    constexpr std::array<u32, 6> code{
        0x20340003U, 0x18084740U, 0x3bc00301U,
        0x00000003U, 0x18c00000U, 0x318c0300U,
    };
    const std::array<u32, 2> params{0xffffffffU, 1};
    std::vector<std::pair<u32, u32>> writes;
    const auto result = interpreter.Execute(code, 0, params, [](u32) { return 0U; },
        [&](u32 method, u32 value) { writes.emplace_back(method, value); return true; });
    REQUIRE(result.success);
    REQUIRE(result.consumed == 2);
    REQUIRE(writes == std::vector<std::pair<u32, u32>>{{0xd00, 0}, {0xd01, 1}});
}

TEST_CASE("Native empty QMDs release only after their dependency signals", "[native-gpu]") {
    Tegra::NativeQmdScheduler scheduler;
    Tegra::NativeQmdScheduler::Descriptor descriptor{};
    descriptor[4] = 0x8040; // Captured QMD: two dependencies, no CTAs.
    descriptor[18] = 0x00010030;
    descriptor[24] = 0x100000;
    descriptor[25] = 0x40880021; // One-word release, sysmembar, upper address 0x21.
    descriptor[26] = 0;
    u32 releases{};
    const auto read = [&](u64, auto& out) { out = descriptor; return true; };
    const auto release = [&](u64 address, u32 value) {
        REQUIRE(address == 0x2100100000ULL);
        REQUIRE(value == 42);
        ++releases;
        return true;
    };
    REQUIRE(scheduler.Signal(0x2100200000ULL, 3, read, release).empty());
    REQUIRE(releases == 0);
    REQUIRE(scheduler.Signal(0x2100200000ULL, 7, read, release).empty());
    REQUIRE(releases == 0);
    descriptor[26] = 42;
    REQUIRE(scheduler.Signal(0x2100200000ULL, 7, read, release).empty());
    REQUIRE(releases == 1);
}

TEST_CASE("Native QMD scheduler rejects real shader work without reporting completion", "[native-gpu]") {
    Tegra::NativeQmdScheduler scheduler;
    Tegra::NativeQmdScheduler::Descriptor descriptor{};
    descriptor[4] = 1U << 14;
    descriptor[18] = 0x30;
    descriptor[12] = descriptor[13] = descriptor[14] = 1;
    u32 releases{};
    const auto result = scheduler.Signal(0x10000, 3,
        [&](u64, auto& out) { out = descriptor; return true; },
        [&](u64, u32) { ++releases; return true; });
    REQUIRE(result == "native shader dispatch required");
    REQUIRE(releases == 0);
}

TEST_CASE("Native dependent QMD signals survive until the child is uploaded", "[native-gpu]") {
    Tegra::NativeQmdScheduler scheduler;
    Tegra::NativeQmdScheduler::Descriptor parent{}, child{};
    parent[4] = 1U << 14;
    parent[18] = 0x30;
    parent[15] = 0x200;
    parent[16] = 9; // Enabled child decrement-dependence operation.
    child[4] = 2U << 14;
    child[18] = 0x30;
    child[24] = 0x30000;
    child[25] = 0x40880000;
    child[26] = 99;
    u32 releases{};
    const auto read = [&](u64 address, auto& out) { out = address == 0x10000 ? parent : child; return true; };
    const auto release = [&](u64 address, u32 value) {
        REQUIRE(address == 0x30000);
        REQUIRE(value == 99);
        ++releases;
        return true;
    };
    REQUIRE(scheduler.Signal(0x10000, 3, read, release).empty());
    REQUIRE(scheduler.Signal(0x10000, 7, read, release).empty());
    REQUIRE(releases == 0);
    REQUIRE(scheduler.Signal(0x20000, 3, read, release).empty());
    REQUIRE(releases == 0);
    REQUIRE(scheduler.Signal(0x20000, 7, read, release).empty());
    REQUIRE(releases == 1);
}

TEST_CASE("Native MME dual ALUs sample registers before either writeback", "[native-gpu]") {
    Tegra::TuringMacroInterpreter interpreter;
    constexpr std::array<u32, 12> code{
        0x00000003U, 0x18c00000U, 0x31c00300U,
        0x00000003U, 0x40c7c001U, 0xf1c00300U,
        0x00380483U, 0x18c00000U, 0xf190fb01U,
        0x00000003U, 0x18c00000U, 0x318c0300U,
    };
    const std::array<u32, 2> parameters{3, 0x1000000};
    std::vector<std::pair<u32, u32>> writes;
    const auto result = interpreter.Execute(code, 0, parameters, [](u32) { return 0U; },
        [&](u32 method, u32 value) { writes.emplace_back(method, value); return true; });
    REQUIRE(result.success);
    REQUIRE(result.consumed == 2);
    REQUIRE(writes == std::vector<std::pair<u32, u32>>{{0x120, 7}});
}

TEST_CASE("Native MME taken back edges suppress fallthrough termination", "[native-gpu]") {
    Tegra::TuringMacroInterpreter interpreter;
    constexpr std::array<u32, 12> code{
        0x00000003U, 0x18c00000U, 0x31c00300U,
        0x00383403U, 0x18c00000U, 0x72000b00U,
        0x00000003U, 0x18c017ffU, 0xf00c7701U,
        0x00000003U, 0x18c00000U, 0x318c0300U,
    };
    for (const u32 count : {1U, 3U, 16U}) {
        std::vector<u32> writes;
        const std::array<u32, 1> parameters{count};
        const auto result = interpreter.Execute(code, 0, parameters, [](u32) { return 0U; },
            [&](u32 method, u32 value) { REQUIRE(method == 0xd00); writes.push_back(value); return true; });
        REQUIRE(result.success);
        REQUIRE(result.consumed == 1);
        REQUIRE(writes.size() == count);
        REQUIRE(writes.front() == count - 1);
        REQUIRE(writes.back() == 0);
    }
}

TEST_CASE("Native compute QMD releases follow successful dispatch and retain reduction flags", "[native-gpu]") {
    Tegra::NativeQmdScheduler scheduler;
    Tegra::NativeQmdScheduler::Descriptor descriptor{};
    descriptor[18] = 0x30;
    descriptor[12] = descriptor[13] = descriptor[14] = 1;
    descriptor[24] = 0x10000;
    descriptor[25] = 0x44800021;
    descriptor[26] = 1;
    std::vector<int> order;
    const auto read = [&](u64, auto& out) { out = descriptor; return true; };
    const auto release = [&](u64 address, u32 value, u32 flags) {
        REQUIRE(address == 0x2100010000ULL);
        REQUIRE(value == 1);
        REQUIRE(flags == 0x44800021);
        order.push_back(2);
        return true;
    };
    REQUIRE(scheduler.Signal(0x20000, 3, read, release,
        [&](const auto&) { order.push_back(1); return false; }) == "native shader dispatch required");
    REQUIRE(order == std::vector<int>{1});
    order.clear();
    REQUIRE(scheduler.Signal(0x20000, 3, read, release,
        [&](const auto&) { order.push_back(1); return true; }).empty());
    REQUIRE(order == std::vector<int>{1, 2});
}

TEST_CASE("Native vertex stream sizes use byte counts and dirty the selected stream", "[native-gpu]") {
    using Maxwell3D = Tegra::Engines::Maxwell3D;
    auto regs = std::make_unique<Maxwell3D::Regs>();
    regs->reg_array.fill(0);
    regs->reg_array[0x600 / 4 + 1] = 0x1b4e8;
    REQUIRE(regs->NativeVertexStreamSize(0) == 111848);
    REQUIRE(regs->vertex_stream_limits[0].Address() == 0);
    regs->reg_array[0x600 / 4 + 31 * 2] = 0xffffff81;
    regs->reg_array[0x600 / 4 + 31 * 2 + 1] = 0x76543210;
    REQUIRE(regs->NativeVertexStreamSize(31) == 0x8176543210ULL);
    REQUIRE(regs->NativeVertexStreamSize(1) == 0);
    Maxwell3D::DirtyState::Tables tables{};
    VideoCommon::Dirty::SetupDirtyFlags(tables);
    for (u32 stream = 0; stream < 32; ++stream) {
        for (u32 word = 0; word < 2; ++word) {
            const u32 method = 0x600 / 4 + stream * 2 + word;
            REQUIRE(tables[0][method] == VideoCommon::Dirty::VertexBuffer0 + stream);
            REQUIRE(tables[1][method] == VideoCommon::Dirty::VertexBuffers);
        }
    }
}
