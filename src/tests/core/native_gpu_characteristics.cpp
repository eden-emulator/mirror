// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <memory>
#include "video_core/macro.h"
#include "video_core/native_qmd.h"
#include "video_core/native_root_table.h"
#include "video_core/dirty_flags.h"
#include <catch2/catch_test_macros.hpp>
#include "core/hle/kernel/k_memory_block.h"
#include "core/hle/service/nvdrv/devices/native_gpu_kind.h"
#include "core/hle/service/nvdrv/devices/native_sparse_remap.h"
#include "core/hle/service/nvdrv/devices/native_gpu_context.h"
#include "core/hle/service/nvdrv/devices/native_gpu_characteristics.h"
#include "core/hle/service/nvdrv/devices/gpu_address_space_layout.h"

namespace {
// Skip-and-complete is the default; these tests cover the strict (stalling) behavior.
const int native_strict_env = [] {
#ifdef _WIN32
    _putenv_s("EDEN_NATIVE_STRICT", "1");
#else
    setenv("EDEN_NATIVE_STRICT", "1", 1);
#endif
    return 0;
}();
} // namespace

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
    REQUIRE(level(0x72) == 0xb);
    u64_le flags;
    std::memcpy(&flags, native.data() + 0x50, sizeof(flags));
    REQUIRE((static_cast<u64>(flags) & (1ULL << 8)) != 0);
    REQUIRE((static_cast<u64>(flags) & ~((1ULL << 8) | (1ULL << 22) | (1ULL << 26) | (1ULL << 34) | (1ULL << 35) | (0xfULL << 40))) == (0xccccccccccccccccULL & ~((1ULL << 8) | (1ULL << 22) | (1ULL << 26) | (1ULL << 34) | (1ULL << 35) | (0xfULL << 40))));
    REQUIRE((static_cast<u64>(flags) & (0xfULL << 40)) == (0xfULL << 40));
    REQUIRE((static_cast<u64>(flags) & (1ULL << 34)) != 0);
    REQUIRE((static_cast<u64>(flags) & (1ULL << 22)) != 0);
    u32_le subcontexts;
    std::memcpy(&subcontexts, native.data() + 0xb4, sizeof(subcontexts));
    REQUIRE(static_cast<u32>(subcontexts) == Service::Nvidia::Devices::NativeGpuSubcontextCount);
    REQUIRE(level(0x74) == -1);
    REQUIRE(level(0x76) == 0x1d);
    // The captured SDK rejects the old topology value 2 here, requiring >= 6.
    REQUIRE(level(0x78) == 0x14);
    REQUIRE(std::all_of(native.begin() + 0x7a, native.begin() + 0xb4, [](u8 b) { return b == 0; }));
}

TEST_CASE("Extended native GPU characteristics select the newer shader profile", "[native-gpu]") {
    using namespace Service::Nvidia::Devices;
    std::array<u8, 0xa0> legacy{};
    const auto original = MakeNativeGpuCharacteristics(legacy);
    const auto extended = MakeNativeGpuCharacteristics<ExtendedNativeGpuCharacteristicsSize>(legacy);
    REQUIRE(extended.size() == 0xd8);
    REQUIRE(std::equal(original.begin(), original.begin() + 0x3c, extended.begin()));
    REQUIRE(std::equal(original.begin() + 0x44, original.begin() + 0x76, extended.begin() + 0x44));
    REQUIRE(std::equal(original.begin() + 0x78, original.end(), extended.begin() + 0x78));
    u32_le sm_version, spa_version;
    std::memcpy(&sm_version, extended.data() + 0x3c, sizeof(sm_version));
    std::memcpy(&spa_version, extended.data() + 0x40, sizeof(spa_version));
    REQUIRE(static_cast<u32>(sm_version) == 0x808);
    REQUIRE(static_cast<u32>(spa_version) == 0x808);
    u16_le channel_level;
    std::memcpy(&channel_level, extended.data() + 0x76, sizeof(channel_level));
    REQUIRE(static_cast<u16>(channel_level) == 0x1f);
    REQUIRE(std::all_of(extended.begin() + original.size(), extended.end(),
                        [](u8 value) { return value == 0; }));
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

TEST_CASE("Native address space views validate both small-page request formats", "[native-gpu]") {
    using namespace Service::Nvidia::Devices;
    constexpr auto parent = GetGpuAddressSpaceLayout(true);
    constexpr u64 end = 0xa000000000;
    REQUIRE(IsSupportedNativeAddressSpaceView(5, 0x1000, 0, {parent.start, 0, end}, parent));
    REQUIRE(IsSupportedNativeAddressSpaceView(0, 0x1000, 0, {parent.start, end, end}, parent));
    REQUIRE_FALSE(IsSupportedNativeAddressSpaceView(1, 0x1000, 0, {parent.start, end, end}, parent));
    REQUIRE_FALSE(IsSupportedNativeAddressSpaceView(0, 0x10000, 0, {parent.start, end, end}, parent));
    REQUIRE_FALSE(IsSupportedNativeAddressSpaceView(0, 0x1000, 1, {parent.start, end, end}, parent));
    REQUIRE_FALSE(IsSupportedNativeAddressSpaceView(0, 0x1000, 0, {parent.start, 0, end}, parent));
    REQUIRE_FALSE(IsSupportedNativeAddressSpaceView(5, 0x1000, 0, {parent.start, end, end}, parent));
    REQUIRE_FALSE(IsSupportedNativeAddressSpaceView(0, 0x1000, 0, {0, end, end}, parent));
    REQUIRE_FALSE(IsSupportedNativeAddressSpaceView(0, 0x1000, 0, {end, end, end}, parent));
    REQUIRE_FALSE(IsSupportedNativeAddressSpaceView(
        0, 0x1000, 0, {parent.start, parent.end, parent.end}, parent));
    REQUIRE_FALSE(IsSupportedNativeAddressSpaceView(0, 0x1000, 0, {parent.start + 1, end, end}, parent));
    REQUIRE_FALSE(IsSupportedNativeAddressSpaceView(0, 0x1000, 0, {parent.start, end + 1, end + 1}, parent));
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
    REQUIRE(SelectNativeMemoryKind(9, 6)->backend == fallback->backend);
    REQUIRE(SelectNativeMemoryKind(9, 6)->selected == 6);
    REQUIRE(SelectNativeMemoryKind(0xd, 4)->backend ==
            static_cast<u8>(Tegra::PTEKind::ZF32_X24S8));
    REQUIRE(SelectNativeMemoryKind(0xd, 4)->selected == 4);
    REQUIRE(SelectNativeMemoryKind(4, 4)->backend == SelectNativeMemoryKind(0xd, 4)->backend);
    REQUIRE_FALSE(SelectNativeMemoryKind(0xd, 6));
    const auto generic_no_plc = SelectNativeMemoryKind(9, 6);
    REQUIRE(generic_no_plc);
    REQUIRE(generic_no_plc->selected == 6);
    REQUIRE(generic_no_plc->backend == normal->backend);
    REQUIRE_FALSE(SelectNativeMemoryKind(9, 9));
    REQUIRE_FALSE(SelectNativeMemoryKind(9, 0));
    REQUIRE_FALSE(SelectNativeMemoryKind(9, 3));
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

TEST_CASE("TSG channels combine QMD dependencies while other TSGs stay isolated", "[native-gpu]") {
    auto group = std::make_unique<Tegra::NativeMacroContext>();
    auto other_group = std::make_unique<Tegra::NativeMacroContext>();
    auto& channel_a = group->GetQmdScheduler(1);
    auto& channel_b = group->GetQmdScheduler(1);
    Tegra::NativeQmdScheduler::Descriptor parent{}, child{};
    parent[4] = 2U << 14;
    parent[18] = 0x30;
    parent[15] = 0x200;
    parent[16] = 9;
    child[4] = 2U << 14;
    child[18] = 0x30;
    child[24] = 0x30000;
    child[25] = 0x40880000;
    std::vector<u32> releases;
    const auto read = [&](u64 address, auto& out) {
        out = address == 0x10000 ? parent : child;
        return true;
    };
    const auto release = [&](u64 address, u32 payload) {
        REQUIRE(address == 0x30000);
        releases.push_back(payload);
        return true;
    };
    for (u32 cycle = 1; cycle <= 8; ++cycle) {
        child[26] = cycle;
        REQUIRE(channel_a.Signal(0x10000, 3, read, release).empty());
        REQUIRE(channel_b.Signal(0x20000, 3, read, release).empty());
        REQUIRE(channel_a.Signal(0x10000, 7, read, release).empty());
        REQUIRE(channel_b.Signal(0x10000, 7, read, release).empty());
        // The parent contributes one dependency to the child. A different TSG
        // signaling the same virtual address must not supply its final signal.
        REQUIRE(other_group->GetQmdScheduler(1).Signal(0x20000, 7, read, release).empty());
        REQUIRE(group->GetQmdScheduler(2).Signal(0x20000, 7, read, release).empty());
        REQUIRE(releases.size() == cycle - 1);
        REQUIRE(channel_a.Signal(0x20000, 7, read, release).empty());
        REQUIRE(releases.size() == cycle);
        REQUIRE(releases.back() == cycle);
    }
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

TEST_CASE("Native shader profile does not depend on characteristics reply size", "[native-gpu]") {
    using namespace Service::Nvidia::Devices;
    for (const size_t size : {NativeGpuCharacteristicsSize, ExtendedNativeGpuCharacteristicsSize}) {
        for (const u32 version : {0x807U, 0x808U}) {
            std::vector<u8> data(size, 0xcc);
            REQUIRE(ApplyNativeGpuProfile(data, version));
            u32_le sm, spa;
            std::memcpy(&sm, data.data() + 0x3c, sizeof(sm));
            std::memcpy(&spa, data.data() + 0x40, sizeof(spa));
            REQUIRE(static_cast<u32>(sm) == version);
            REQUIRE(static_cast<u32>(spa) == version);
            u16_le channel_level;
            std::memcpy(&channel_level, data.data() + 0x76, sizeof(channel_level));
            REQUIRE(static_cast<u16>(channel_level) == (version == 0x808 ? 0x1f : 0x1d));
            REQUIRE(std::all_of(data.begin() + 0x78, data.end(),
                                [](u8 value) { return value == 0xcc; }));
            REQUIRE(std::all_of(data.begin(), data.begin() + 0x3c,
                                [](u8 value) { return value == 0xcc; }));
            REQUIRE(std::all_of(data.begin() + 0x44, data.begin() + 0x76,
                                [](u8 value) { return value == 0xcc; }));
        }
    }
    std::array<u8, 0xd0> data{};
    for (const u32 version : {0U, 0x503U, 0x809U}) {
        REQUIRE_FALSE(ApplyNativeGpuProfile(data, version));
        REQUIRE(data == std::array<u8, 0xd0>{});
    }
    REQUIRE_FALSE(ApplyNativeGpuProfile(std::span{data}.first(0x77), 0x808));
    REQUIRE(data == std::array<u8, 0xd0>{});
}

TEST_CASE("Native TSG bindings validate subcontext bounds and reserved bytes", "[native-gpu]") {
    using namespace Service::Nvidia::Devices;
    std::array<u8, 24> packet{};
    packet[0] = 7;
    for (u32 id = 0; id < NativeGpuSubcontextCount; ++id) {
        packet[4] = static_cast<u8>(id);
        const auto binding = DecodeNativeTsgBinding(packet, true);
        REQUIRE(binding);
        REQUIRE(binding->channel_fd == 7);
        REQUIRE(binding->subcontext_id == id);
    }
    packet[4] = NativeGpuSubcontextCount;
    REQUIRE_FALSE(DecodeNativeTsgBinding(packet, true));
    packet[4] = 0;
    for (size_t i = 8; i < packet.size(); ++i) {
        packet[i] = 1;
        REQUIRE_FALSE(DecodeNativeTsgBinding(packet, true));
        packet[i] = 0;
    }
    REQUIRE_FALSE(DecodeNativeTsgBinding(packet, false));
    REQUIRE_FALSE(DecodeNativeTsgBinding(std::span{packet}.first(8), true));
    const auto legacy = DecodeNativeTsgBinding(std::span{packet}.first(4), false);
    REQUIRE(legacy);
    REQUIRE(legacy->subcontext_id == 0);
    packet[3] = 0x80;
    REQUIRE_FALSE(DecodeNativeTsgBinding(packet, true));
}

TEST_CASE("Native MME FIFO barrier reads the captured memory comparison program", "[native-gpu]") {
    constexpr std::array<u32, 27> code{
        0x00940003U, 0x18c00455U, 0x31c10300U,
        0x8a600003U, 0x18c01856U, 0x318c0300U,
        0x00000007U, 0x18c00400U, 0x359c1f00U,
        0x00000003U, 0x1d000000U, 0x31c08300U,
        0x00000003U, 0x18c00000U, 0x3bcc0300U,
        0x00000003U, 0xa003c000U, 0x3810bf00U,
        0x00000003U, 0x18c00000U, 0x0010bb00U,
        0x0018155bU, 0x18c00000U, 0x72213c29U,
        0x00200003U, 0x18c00000U, 0x302c0300U,
    };
    const std::array<u32, 3> params{0x55, 0x12, 0x34560000};
    for (const bool decoded : {false, true}) {
        Tegra::TuringMacroInterpreter interpreter;
        std::array<Tegra::TuringMacroInterpreter::Instruction, 9> program;
        for (size_t i = 0; i < program.size(); ++i) {
            program[i] = Tegra::TuringMacroInterpreter::Decode(std::span<const u32, 3>{code.data() + 3*i, 3});
        }
        std::vector<std::pair<u32, u32>> writes;
        u32 reads{};
        auto memory = [&](u64 address, std::span<u32> output) {
            ++reads;
            REQUIRE(address == 0x1234560000ULL);
            REQUIRE(output.size() == 6);
            const std::array<u32, 6> data{0x1234, 0x5678, 0xdead, 0xbeef, 0x1234, 0x5678};
            std::copy(data.begin(), data.end(), output.begin());
            return true;
        };
        auto emit = [&](u32 method, u32 value) { writes.emplace_back(method, value); return true; };
        const auto result = decoded
            ? interpreter.ExecuteDecoded(program, 0, params, [](u32) { return 0U; }, emit, memory)
            : interpreter.Execute(code, 0, params, [](u32) { return 0U; }, emit, memory);
        REQUIRE(result.success);
        REQUIRE(result.consumed == 9);
        REQUIRE(reads == 1);
        REQUIRE(writes == std::vector<std::pair<u32, u32>>{{0x556, 0x55}});
        const auto failed = interpreter.Execute(code, 0, params, [](u32) { return 0U; }, emit,
                                                [](u64, std::span<u32>) { return false; });
        REQUIRE_FALSE(failed.success);
        REQUIRE(failed.error == "FIFO DMA memory read failed");
    }
}

TEST_CASE("Native MME rejects oversized FIFO DMA and unknown extended operations", "[native-gpu]") {
    using Interpreter = Tegra::TuringMacroInterpreter;
    std::array<Interpreter::Instruction, 3> program{};
    for (auto& inst : program) {
        inst.src_a.fill(24); inst.src_b.fill(24); inst.dst.fill(24);
    }
    program[0].imm[0] = 0x1158;
    program[0].out_method[0] = 5;
    program[0].out_emit[0] = 3;
    program[0].load_mask = 1;
    Interpreter interpreter;
    const std::array<u32, 1> count{1025};
    auto result = interpreter.ExecuteDecoded(program, 0, count, [](u32) { return 0U; }, [](u32, u32) { return true; });
    REQUIRE_FALSE(result.success);
    REQUIRE(result.error == "FIFO DMA length out of bounds");
    program[0].out_method[0] = program[0].out_emit[0] = program[0].load_mask = 0;
    program[0].op[0] = 7;
    result = interpreter.ExecuteDecoded(program, 0, {}, [](u32) { return 0U; }, [](u32, u32) { return true; });
    REQUIRE_FALSE(result.success);
    REQUIRE(result.error == "unsupported extended operation");
}

TEST_CASE("Shared MME programs preserve independent persistent data RAM", "[native-gpu]") {
    using Interpreter = Tegra::TuringMacroInterpreter;
    Interpreter first, second;
    std::array<Interpreter::Instruction, 2> store{};
    std::array<Interpreter::Instruction, 3> load{};
    for (auto* program : {&store}) {
        for (auto& instruction : *program) {
            instruction.src_a.fill(24);
            instruction.src_b.fill(24);
            instruction.dst.fill(24);
        }
    }
    for (auto& instruction : load) {
        instruction.src_a.fill(24);
        instruction.src_b.fill(24);
        instruction.dst.fill(24);
    }
    store[0].op[0] = 31;
    store[0].src_a[0] = 25;
    store[0].src_b[0] = 28;
    store[0].load_mask = 1;
    store[0].end_next = true;
    load[0].op[0] = 30;
    load[0].src_a[0] = 25;
    load[0].dst[0] = 0;
    load[1].src_a[0] = 0;
    load[1].imm[0] = 0xd00;
    load[1].out_method[0] = 5;
    load[1].out_emit[0] = 1;
    load[1].end_next = true;
    const auto read = [](u32) { return 0U; };
    for (u32 index : {0U, 17U, 0xbffU}) {
        store[0].imm[0] = load[0].imm[0] = index;
        const std::array<u32, 1> a{0x29160000 + index}, b{0x29270000 + index};
        REQUIRE(first.ExecuteDecoded(store, 0, a, read, [](u32, u32) { return true; }).success);
        REQUIRE(second.ExecuteDecoded(store, 0, b, read, [](u32, u32) { return true; }).success);
        for (u32 iteration = 0; iteration < 3; ++iteration) {
            const auto verify = [&](Interpreter& interpreter, u32 expected) {
                u32 writes{};
                const auto result = interpreter.ExecuteDecoded(load, 0, {}, read,
                    [&](u32 method, u32 value) {
                        REQUIRE(method == 0xd00);
                        REQUIRE(value == expected);
                        ++writes;
                        return true;
                    });
                REQUIRE(result.success);
                REQUIRE(writes == 1);
            };
            verify(first, a[0]);
            verify(second, b[0]);
        }
    }
}

TEST_CASE("Native sparse remap decodes wide page indices without truncation", "[native-gpu]") {
    using namespace Service::Nvidia::Devices;
    NativeSparseRemapEntry entry{0x9004, 8, 6, 52, 0, 0, 0x8100000, 0x12c00};
    const auto range = DecodeNativeSparseRemap(entry);
    REQUIRE(range);
    REQUIRE(range->address == 0x8100000000ULL);
    REQUIRE(range->size == 300ULL * 1024 * 1024);
    REQUIRE(range->source_offset == 0);
    REQUIRE(range->handle == 52);
    entry.flags = 0x2000;
    entry.destination_pages = 0x810000;
    entry.pages = 0x12c0;
    REQUIRE(DecodeNativeSparseRemap(entry)->address == range->address);
    REQUIRE(DecodeNativeSparseRemap(entry)->size == range->size);
    for (u32 flags : {0U, 0x1008U, 0x1080U, 0x3000U}) {
        entry.flags = flags;
        REQUIRE_FALSE(DecodeNativeSparseRemap(entry));
    }
    entry.flags = 0x1000;
    entry.pages = 0;
    REQUIRE_FALSE(DecodeNativeSparseRemap(entry));
    entry.pages = 1;
    entry.destination_pages = ~0ULL >> 12;
    REQUIRE_FALSE(DecodeNativeSparseRemap(entry));
    entry.destination_pages = 0x8100000;
    entry.source_pages = ~0ULL;
    REQUIRE_FALSE(DecodeNativeSparseRemap(entry));
    entry.source_pages = 1;
    entry.handle = 0;
    REQUIRE_FALSE(DecodeNativeSparseRemap(entry));
    entry.source_pages = 0;
    REQUIRE(DecodeNativeSparseRemap(entry));
    entry.reserved = 1;
    REQUIRE_FALSE(DecodeNativeSparseRemap(entry));
    const auto native = MakeNativeGpuCharacteristics(std::array<u8, 0xa0>{});
    u64_le flags;
    std::memcpy(&flags, native.data() + 0x50, sizeof(flags));
    REQUIRE((u64{flags} & (1ULL << 35)) != 0);
}

TEST_CASE("Native sparse replacement preserves source offsets and only retains live handles", "[native-gpu]") {
    using namespace Service::Nvidia::Devices;
    NativeSparseMappingTable table;
    table.Replace({0x1000, 0x6000, 0x2000, 7});
    table.Replace({0x3000, 0x1000, 0x9000, 8});
    REQUIRE(table.Ranges().size() == 3);
    REQUIRE(table.Ranges().at(0x1000).size == 0x2000);
    REQUIRE(table.Ranges().at(0x4000).source_offset == 0x5000);
    REQUIRE(table.ReferencedHandles() == std::set<u32>{7, 8});
    table.Replace({0x2000, 0x4000, 0, 0});
    REQUIRE(table.Ranges().size() == 2);
    REQUIRE(table.Ranges().at(0x1000).size == 0x1000);
    REQUIRE(table.Ranges().at(0x6000).source_offset == 0x7000);
    REQUIRE(table.ReferencedHandles() == std::set<u32>{7});
    table.Replace({0x1000, 0x6000, 0, 9});
    REQUIRE(table.Ranges().size() == 1);
    REQUIRE(table.ReferencedHandles() == std::set<u32>{9});
    table.Replace({0x1000, 0x6000, 0, 0});
    REQUIRE(table.Ranges().empty());
    REQUIRE(table.ReferencedHandles().empty());
}

TEST_CASE("Native sparse overlapping batches agree with an independent per-page model", "[native-gpu]") {
    using namespace Service::Nvidia::Devices;
    NativeSparseMappingTable table;
    std::array<std::pair<u32, u64>, 64> pages{};
    u32 random = 0x12345678;
    for (u32 step = 0; step < 120; ++step) {
        random = random * 1664525U + 1013904223U;
        const u32 first = (random >> 16) % 64;
        const u32 count = 1 + (random >> 24) % (64 - first);
        const u32 handle = (random >> 8) % 5;
        const u64 source = (random & 255) * 0x1000ULL;
        table.Replace({0x1000ULL * first, 0x1000ULL * count, source, handle});
        for (u32 i = 0; i < count; ++i) pages[first + i] = {handle, handle ? source + i * 0x1000ULL : 0};
        std::set<u32> expected_handles;
        for (u32 i = 0; i < 64; ++i) {
            const u64 address = i * 0x1000ULL;
            const auto it = table.Ranges().upper_bound(address);
            u32 found_handle{};
            u64 found_offset{};
            if (it != table.Ranges().begin()) {
                const auto& range = std::prev(it)->second;
                if (address < range.address + range.size) {
                    found_handle = range.handle;
                    found_offset = range.source_offset + address - range.address;
                }
            }
            REQUIRE(found_handle == pages[i].first);
            REQUIRE(found_offset == pages[i].second);
            if (pages[i].first) expected_handles.insert(pages[i].first);
        }
        REQUIRE(table.ReferencedHandles() == expected_handles);
    }
}

TEST_CASE("Native root-table FIFO preserves uploaded color constants and partial updates", "[native-gpu]") {
    Tegra::NativeRootTables tables;
    constexpr std::array<u32, 8> matrix{
        0x3f209d8c, 0x3ea897a6, 0x3d31699a, 0x3f800000,
        0x3d8d82ba, 0x3f6b66f9, 0x3c3a2454, 0};
    tables.Select(0);
    for (const auto word : matrix) REQUIRE(tables.Load(word));
    for (u32 i = 0; i < matrix.size(); ++i) REQUIRE(tables.Read(24, i * 4) == matrix[i]);
    // A repeated selector write restarts the FIFO even though its register value is unchanged.
    tables.Select(0);
    REQUIRE(tables.Load(0x3f800000));
    REQUIRE(tables.Read(24, 0) == 0x3f800000);
    REQUIRE(tables.Read(24, 4) == matrix[1]);
    tables.Select((28 << 8) | 7);
    REQUIRE(tables.Load(0x12345678));
    REQUIRE(tables.Read(31, 28) == 0x12345678);
    REQUIRE(tables.Read(24, 28) == matrix[7]);
    Tegra::NativeRootTables other_engine;
    REQUIRE(other_engine.Read(24, 0) == 0);
    REQUIRE(other_engine.Read(31, 28) == 0);
}

TEST_CASE("Native root-table uploads validate byte offsets and table boundaries", "[native-gpu]") {
    Tegra::NativeRootTables tables;
    tables.Select((252 << 8) | 3);
    REQUIRE(tables.Load(0x87654321));
    REQUIRE_FALSE(tables.Load(0xdeadbeef));
    REQUIRE(tables.Read(27, 252) == 0x87654321);
    REQUIRE(tables.Read(28, 0) == 0);
    tables.Select(1 << 8);
    REQUIRE_FALSE(tables.Load(42));
    REQUIRE(tables.Read(24, 0) == 0);
    REQUIRE(tables.Read(24, 256) == 0);
    REQUIRE(tables.Read(23, 0) == 0);
    REQUIRE(tables.Read(32, 0) == 0);
    REQUIRE(Tegra::NativeRootTables::IsBank(24));
    REQUIRE(Tegra::NativeRootTables::IsBank(31));
    REQUIRE_FALSE(Tegra::NativeRootTables::IsBank(23));
    REQUIRE_FALSE(Tegra::NativeRootTables::IsBank(32));
}
