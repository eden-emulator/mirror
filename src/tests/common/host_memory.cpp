// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "common/host_memory.h"
#include "common/literals.h"
#include "common/scope_exit.h"
#include "common/settings.h"

using Common::HostMemory;
using namespace Common::Literals;

static constexpr size_t VIRTUAL_SIZE = 1ULL << 39;
static constexpr size_t BACKING_SIZE = 4_GiB;
static constexpr auto PERMS = Common::MemoryPermission::ReadWrite;
static constexpr auto HEAP = false;

static size_t MappingOffset(size_t offset) {
    constexpr size_t GuestPage = 0x1000;
    const size_t native_page = HostMemory::GetMappingAlignment();
    return (offset / GuestPage) * native_page + (offset % GuestPage);
}

TEST_CASE("HostMemory: Initialize and deinitialize", "[common]") {
    {
        HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
        REQUIRE(mem.BackingBasePointer() != nullptr);
    }
    {
        HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
        REQUIRE(mem.BackingBasePointer() != nullptr);
    }
}

TEST_CASE("HostMemory: Larger host pages disable guest fastmem for every preset", "[common]") {
    if (HostMemory::GetMappingAlignment() == 0x1000) {
        SUCCEED("The host supports the guest mapping granularity");
        return;
    }
    const auto accuracy = Settings::values.cpu_accuracy.GetValue();
    const auto fastmem = Settings::values.cpuopt_fastmem.GetValue();
    const auto unsafe_mmu = Settings::values.cpuopt_unsafe_host_mmu.GetValue();
    SCOPE_EXIT {
        Settings::values.cpu_accuracy.SetValue(accuracy);
        Settings::values.cpuopt_fastmem.SetValue(fastmem);
        Settings::values.cpuopt_unsafe_host_mmu.SetValue(unsafe_mmu);
    };
    Settings::values.cpuopt_fastmem.SetValue(true);
    Settings::values.cpuopt_unsafe_host_mmu.SetValue(true);
    for (const auto preset : {Settings::CpuAccuracy::Auto, Settings::CpuAccuracy::Debugging,
                              Settings::CpuAccuracy::Unsafe}) {
        Settings::values.cpu_accuracy.SetValue(preset);
        REQUIRE_FALSE(Settings::IsFastmemEnabled());
    }
}

TEST_CASE("HostMemory: Simple map", "[common]") {
    HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
    REQUIRE(mem.BackingBasePointer() != nullptr);
    mem.Map(MappingOffset(0x5000), MappingOffset(0x8000), MappingOffset(0x1000), PERMS, HEAP);

    volatile u8* const data = mem.VirtualBasePointer() + MappingOffset(0x5000);
    data[0] = 50;
    REQUIRE(data[0] == 50);
}

TEST_CASE("HostMemory: Simple mirror map", "[common]") {
    HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
    REQUIRE(mem.BackingBasePointer() != nullptr);
    mem.Map(MappingOffset(0x5000), MappingOffset(0x3000), MappingOffset(0x2000), PERMS, HEAP);
    mem.Map(MappingOffset(0x8000), MappingOffset(0x4000), MappingOffset(0x1000), PERMS, HEAP);

    volatile u8* const mirror_a = mem.VirtualBasePointer() + MappingOffset(0x5000);
    volatile u8* const mirror_b = mem.VirtualBasePointer() + MappingOffset(0x8000);
    mirror_b[0] = 76;
    REQUIRE(mirror_a[MappingOffset(0x1000)] == 76);
}

TEST_CASE("HostMemory: Simple unmap", "[common]") {
    HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
    REQUIRE(mem.BackingBasePointer() != nullptr);
    mem.Map(MappingOffset(0x5000), MappingOffset(0x3000), MappingOffset(0x2000), PERMS, HEAP);

    volatile u8* const data = mem.VirtualBasePointer() + MappingOffset(0x5000);
    data[75] = 50;
    REQUIRE(data[75] == 50);

    mem.Unmap(MappingOffset(0x5000), MappingOffset(0x2000), HEAP);
}

TEST_CASE("HostMemory: Simple unmap and remap", "[common]") {
    HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
    REQUIRE(mem.BackingBasePointer() != nullptr);
    mem.Map(MappingOffset(0x5000), MappingOffset(0x3000), MappingOffset(0x2000), PERMS, HEAP);

    volatile u8* const data = mem.VirtualBasePointer() + MappingOffset(0x5000);
    data[0] = 50;
    REQUIRE(data[0] == 50);

    mem.Unmap(MappingOffset(0x5000), MappingOffset(0x2000), HEAP);

    mem.Map(MappingOffset(0x5000), MappingOffset(0x3000), MappingOffset(0x2000), PERMS, HEAP);
    REQUIRE(data[0] == 50);

    mem.Map(MappingOffset(0x7000), MappingOffset(0x2000), MappingOffset(0x5000), PERMS, HEAP);
    REQUIRE(data[MappingOffset(0x3000)] == 50);
}

TEST_CASE("HostMemory: Nieche allocation", "[common]") {
    HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
    REQUIRE(mem.BackingBasePointer() != nullptr);
    mem.Map(MappingOffset(0x0000), 0, MappingOffset(0x20000), PERMS, HEAP);
    mem.Unmap(MappingOffset(0x0000), MappingOffset(0x4000), HEAP);
    mem.Map(MappingOffset(0x1000), 0, MappingOffset(0x2000), PERMS, HEAP);
    mem.Map(MappingOffset(0x3000), 0, MappingOffset(0x1000), PERMS, HEAP);
    mem.Map(0, 0, MappingOffset(0x1000), PERMS, HEAP);
}

TEST_CASE("HostMemory: Full unmap", "[common]") {
    HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
    REQUIRE(mem.BackingBasePointer() != nullptr);
    mem.Map(MappingOffset(0x8000), 0, MappingOffset(0x4000), PERMS, HEAP);
    mem.Unmap(MappingOffset(0x8000), MappingOffset(0x4000), HEAP);
    mem.Map(MappingOffset(0x6000), 0, MappingOffset(0x16000), PERMS, HEAP);
}

TEST_CASE("HostMemory: Right out of bounds unmap", "[common]") {
    HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
    REQUIRE(mem.BackingBasePointer() != nullptr);
    mem.Map(MappingOffset(0x0000), 0, MappingOffset(0x4000), PERMS, HEAP);
    mem.Unmap(MappingOffset(0x2000), MappingOffset(0x4000), HEAP);
    mem.Map(MappingOffset(0x2000), MappingOffset(0x80000), MappingOffset(0x4000), PERMS, HEAP);
}

TEST_CASE("HostMemory: Left out of bounds unmap", "[common]") {
    HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);

    mem.Map(MappingOffset(0x8000), 0, MappingOffset(0x4000), PERMS, HEAP);
    mem.Unmap(MappingOffset(0x6000), MappingOffset(0x4000), HEAP);
    mem.Map(MappingOffset(0x8000), 0, MappingOffset(0x2000), PERMS, HEAP);
}

TEST_CASE("HostMemory: Multiple placeholder unmap", "[common]") {
    HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
    REQUIRE(mem.BackingBasePointer() != nullptr);
    mem.Map(MappingOffset(0x0000), 0, MappingOffset(0x4000), PERMS, HEAP);
    mem.Map(MappingOffset(0x4000), 0, MappingOffset(0x1b000), PERMS, HEAP);
    mem.Unmap(MappingOffset(0x3000), MappingOffset(0x1c000), HEAP);
    mem.Map(MappingOffset(0x3000), 0, MappingOffset(0x20000), PERMS, HEAP);
}

TEST_CASE("HostMemory: Unmap between placeholders", "[common]") {
    HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
    REQUIRE(mem.BackingBasePointer() != nullptr);
    mem.Map(MappingOffset(0x0000), 0, MappingOffset(0x4000), PERMS, HEAP);
    mem.Map(MappingOffset(0x4000), 0, MappingOffset(0x4000), PERMS, HEAP);
    mem.Unmap(MappingOffset(0x2000), MappingOffset(0x4000), HEAP);
    mem.Map(MappingOffset(0x2000), 0, MappingOffset(0x4000), PERMS, HEAP);
}

TEST_CASE("HostMemory: Unmap to origin", "[common]") {
    HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
    REQUIRE(mem.BackingBasePointer() != nullptr);
    mem.Map(MappingOffset(0x4000), 0, MappingOffset(0x4000), PERMS, HEAP);
    mem.Map(MappingOffset(0x8000), 0, MappingOffset(0x4000), PERMS, HEAP);
    mem.Unmap(MappingOffset(0x4000), MappingOffset(0x4000), HEAP);
    mem.Map(0, 0, MappingOffset(0x4000), PERMS, HEAP);
    mem.Map(MappingOffset(0x4000), 0, MappingOffset(0x4000), PERMS, HEAP);
}

TEST_CASE("HostMemory: Unmap to right", "[common]") {
    HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
    REQUIRE(mem.BackingBasePointer() != nullptr);
    mem.Map(MappingOffset(0x4000), 0, MappingOffset(0x4000), PERMS, HEAP);
    mem.Map(MappingOffset(0x8000), 0, MappingOffset(0x4000), PERMS, HEAP);
    mem.Unmap(MappingOffset(0x8000), MappingOffset(0x4000), HEAP);
    mem.Map(MappingOffset(0x8000), 0, MappingOffset(0x4000), PERMS, HEAP);
}

TEST_CASE("HostMemory: Partial right unmap check bindings", "[common]") {
    HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
    REQUIRE(mem.BackingBasePointer() != nullptr);
    mem.Map(MappingOffset(0x4000), MappingOffset(0x10000), MappingOffset(0x4000), PERMS, HEAP);

    volatile u8* const ptr = mem.VirtualBasePointer() + MappingOffset(0x4000);
    ptr[MappingOffset(0x1000)] = 17;

    mem.Unmap(MappingOffset(0x6000), MappingOffset(0x2000), HEAP);

    REQUIRE(ptr[MappingOffset(0x1000)] == 17);
}

TEST_CASE("HostMemory: Partial left unmap check bindings", "[common]") {
    HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
    REQUIRE(mem.BackingBasePointer() != nullptr);
    mem.Map(MappingOffset(0x4000), MappingOffset(0x10000), MappingOffset(0x4000), PERMS, HEAP);

    volatile u8* const ptr = mem.VirtualBasePointer() + MappingOffset(0x4000);
    ptr[MappingOffset(0x3000)] = 19;
    ptr[(MappingOffset(0x4000) - 1)] = 12;

    mem.Unmap(MappingOffset(0x4000), MappingOffset(0x2000), HEAP);

    REQUIRE(ptr[MappingOffset(0x3000)] == 19);
    REQUIRE(ptr[(MappingOffset(0x4000) - 1)] == 12);
}

TEST_CASE("HostMemory: Partial middle unmap check bindings", "[common]") {
    HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
    REQUIRE(mem.BackingBasePointer() != nullptr);
    mem.Map(MappingOffset(0x4000), MappingOffset(0x10000), MappingOffset(0x4000), PERMS, HEAP);

    volatile u8* const ptr = mem.VirtualBasePointer() + MappingOffset(0x4000);
    ptr[MappingOffset(0x0000)] = 19;
    ptr[(MappingOffset(0x4000) - 1)] = 12;

    mem.Unmap(MappingOffset(0x1000), MappingOffset(0x2000), HEAP);

    REQUIRE(ptr[MappingOffset(0x0000)] == 19);
    REQUIRE(ptr[(MappingOffset(0x4000) - 1)] == 12);
}

TEST_CASE("HostMemory: Partial sparse middle unmap and check bindings", "[common]") {
    HostMemory mem(BACKING_SIZE, VIRTUAL_SIZE);
    REQUIRE(mem.BackingBasePointer() != nullptr);
    mem.Map(MappingOffset(0x4000), MappingOffset(0x10000), MappingOffset(0x2000), PERMS, HEAP);
    mem.Map(MappingOffset(0x6000), MappingOffset(0x20000), MappingOffset(0x2000), PERMS, HEAP);

    volatile u8* const ptr = mem.VirtualBasePointer() + MappingOffset(0x4000);
    ptr[MappingOffset(0x0000)] = 19;
    ptr[(MappingOffset(0x4000) - 1)] = 12;

    mem.Unmap(MappingOffset(0x5000), MappingOffset(0x2000), HEAP);

    REQUIRE(ptr[MappingOffset(0x0000)] == 19);
    REQUIRE(ptr[(MappingOffset(0x4000) - 1)] == 12);
}
