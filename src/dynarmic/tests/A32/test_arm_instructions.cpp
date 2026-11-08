// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include <array>

#include <catch2/catch_test_macros.hpp>

#include "dynarmic/tests/A32/testenv.h"
#include "dynarmic/tests/native/testenv.h"
#include "dynarmic/frontend/A32/a32_location_descriptor.h"
#include "dynarmic/interface/A32/a32.h"

using namespace Dynarmic;

static A32::UserConfig GetUserConfig(ArmTestEnv* testenv) {
    A32::UserConfig user_config;
    user_config.optimizations &= ~OptimizationFlag::FastDispatch;
    user_config.callbacks = testenv;
    return user_config;
}

TEST_CASE("arm: Opt Failure: Const folding in MostSignificantWord", "[arm][A32]") {
    // This was a randomized test-case that was failing.
    // This was due to constant folding for MostSignificantWord
    // failing to take into account an associated GetCarryFromOp
    // pseudoinstruction.

    ArmTestEnv test_env;
    A32::Jit jit{GetUserConfig(&test_env)};
    test_env.code_mem = {
        0xe30ad071,  // movw, sp, #41073
        0xe75efd3d,  // smmulr lr, sp, sp
        0xa637af1e,  // shadd16ge r10, r7, lr
        0xf57ff01f,  // clrex
        0x86b98879,  // sxtahhi r8, r9, r9, ror #16
        0xeafffffe,  // b +#0
    };

    jit.SetCpsr(0x000001d0);  // User-mode

    test_env.ticks_left = 6;
    CheckedRun([&]() { jit.Run(); });

    // If we don't trigger the GetCarryFromOp ASSERT, we're fine.
}

TEST_CASE("arm: Unintended modification in SetCFlag", "[arm][A32]") {
    // This was a randomized test-case that was failing.
    //
    // IR produced for location {12, !T, !E} was:
    // %0     = GetRegister r1
    // %1     = SubWithCarry %0, #0x3e80000, #1
    // %2     = GetCarryFromOp %1
    // %3     = GetOverflowFromOp %1
    // %4     = MostSignificantBit %1
    //          SetNFlag %4
    // %6     = IsZero %1
    //          SetZFlag %6
    //          SetCFlag %2
    //          SetVFlag %3
    // %10    = GetRegister r5
    // %11    = AddWithCarry %10, #0x8a00, %2
    //          SetRegister r4, %11
    //
    // The reference to %2 in instruction %11 was the issue, because instruction %8
    // told the register allocator it was a Use but then modified the value.
    // Changing the EmitSet*Flag instruction to declare their arguments as UseScratch
    // solved this bug.

    ArmTestEnv test_env;
    A32::Jit jit{GetUserConfig(&test_env)};
    test_env.code_mem = {
        0xe35f0cd9,  // cmp pc, #55552
        0xe11c0474,  // tst r12, r4, ror r4
        0xe1a006a7,  // mov r0, r7, lsr #13
        0xe35107fa,  // cmp r1, #0x3E80000
        0xe2a54c8a,  // adc r4, r5, #35328
        0xeafffffe,  // b +#0
    };

    jit.Regs() = {
        0x6973b6bb, 0x267ea626, 0x69debf49, 0x8f976895, 0x4ecd2d0d, 0xcf89b8c7, 0xb6713f85, 0x15e2aa5,
        0xcd14336a, 0xafca0f3e, 0xace2efd9, 0x68fb82cd, 0x775447c0, 0xc9e1f8cd, 0xebe0e626, 0x0};
    jit.SetCpsr(0x000001d0);  // User-mode

    test_env.ticks_left = 6;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.Regs()[0] == 0x00000af1);
    REQUIRE(jit.Regs()[1] == 0x267ea626);
    REQUIRE(jit.Regs()[2] == 0x69debf49);
    REQUIRE(jit.Regs()[3] == 0x8f976895);
    REQUIRE(jit.Regs()[4] == 0xcf8a42c8);
    REQUIRE(jit.Regs()[5] == 0xcf89b8c7);
    REQUIRE(jit.Regs()[6] == 0xb6713f85);
    REQUIRE(jit.Regs()[7] == 0x015e2aa5);
    REQUIRE(jit.Regs()[8] == 0xcd14336a);
    REQUIRE(jit.Regs()[9] == 0xafca0f3e);
    REQUIRE(jit.Regs()[10] == 0xace2efd9);
    REQUIRE(jit.Regs()[11] == 0x68fb82cd);
    REQUIRE(jit.Regs()[12] == 0x775447c0);
    REQUIRE(jit.Regs()[13] == 0xc9e1f8cd);
    REQUIRE(jit.Regs()[14] == 0xebe0e626);
    REQUIRE(jit.Regs()[15] == 0x00000014);
    REQUIRE(jit.Cpsr() == 0x200001d0);
}

TEST_CASE("arm: shsax (Edge-case)", "[arm][A32]") {
    // This was a randomized test-case that was failing.
    //
    // The issue here was one of the words to be subtracted was 0x8000.
    // When the 2s complement was calculated by (~a + 1), it was 0x8000.

    ArmTestEnv test_env;
    A32::Jit jit{GetUserConfig(&test_env)};
    test_env.code_mem = {
        0xe63dbf59,  // shsax r11, sp, r9
        0xeafffffe,  // b +#0
    };

    jit.Regs() = {
        0x3a3b8b18, 0x96156555, 0xffef039f, 0xafb946f2, 0x2030a69a, 0xafe09b2a, 0x896823c8, 0xabde0ded,
        0x9825d6a6, 0x17498000, 0x999d2c95, 0x8b812a59, 0x209bdb58, 0x2f7fb1d4, 0x0f378107, 0x00000000};
    jit.SetCpsr(0x000001d0);  // User-mode

    test_env.ticks_left = 2;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.Regs()[0] == 0x3a3b8b18);
    REQUIRE(jit.Regs()[1] == 0x96156555);
    REQUIRE(jit.Regs()[2] == 0xffef039f);
    REQUIRE(jit.Regs()[3] == 0xafb946f2);
    REQUIRE(jit.Regs()[4] == 0x2030a69a);
    REQUIRE(jit.Regs()[5] == 0xafe09b2a);
    REQUIRE(jit.Regs()[6] == 0x896823c8);
    REQUIRE(jit.Regs()[7] == 0xabde0ded);
    REQUIRE(jit.Regs()[8] == 0x9825d6a6);
    REQUIRE(jit.Regs()[9] == 0x17498000);
    REQUIRE(jit.Regs()[10] == 0x999d2c95);
    REQUIRE(jit.Regs()[11] == 0x57bfe48e);
    REQUIRE(jit.Regs()[12] == 0x209bdb58);
    REQUIRE(jit.Regs()[13] == 0x2f7fb1d4);
    REQUIRE(jit.Regs()[14] == 0x0f378107);
    REQUIRE(jit.Regs()[15] == 0x00000004);
    REQUIRE(jit.Cpsr() == 0x000001d0);
}

TEST_CASE("arm: uasx (Edge-case)", "[arm][A32]") {
    // UASX's Rm<31:16> == 0x0000.
    // An implementation that depends on addition overflow to detect
    // if diff >= 0 will fail this testcase.

    ArmTestEnv test_env;
    A32::Jit jit{GetUserConfig(&test_env)};
    test_env.code_mem = {
        0xe6549f35,  // uasx r9, r4, r5
        0xeafffffe,  // b +#0
    };

    jit.Regs()[4] = 0x8ed38f4c;
    jit.Regs()[5] = 0x0000261d;
    jit.Regs()[15] = 0x00000000;
    jit.SetCpsr(0x000001d0);  // User-mode

    test_env.ticks_left = 2;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.Regs()[4] == 0x8ed38f4c);
    REQUIRE(jit.Regs()[5] == 0x0000261d);
    REQUIRE(jit.Regs()[9] == 0xb4f08f4c);
    REQUIRE(jit.Regs()[15] == 0x00000004);
    REQUIRE(jit.Cpsr() == 0x000301d0);
}

TEST_CASE("arm: smuad (Edge-case)", "[arm][A32]") {
    ArmTestEnv test_env;
    A32::Jit jit{GetUserConfig(&test_env)};
    test_env.code_mem = {
        0xE700F211,  // smuad r0, r1, r2
        0xeafffffe,  // b +#0
    };

    jit.Regs() = {
        0,           // Rd
        0x80008000,  // Rn
        0x80008000,  // Rm
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
    };
    jit.SetCpsr(0x000001d0);  // User-mode

    test_env.ticks_left = 2;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.Regs()[0] == 0x80000000);
    REQUIRE(jit.Regs()[1] == 0x80008000);
    REQUIRE(jit.Regs()[2] == 0x80008000);
    REQUIRE(jit.Cpsr() == 0x080001d0);
}

TEST_CASE("arm: Test InvalidateCacheRange", "[arm][A32]") {
    ArmTestEnv test_env;
    A32::Jit jit{GetUserConfig(&test_env)};
    test_env.code_mem = {
        0xe3a00005,  // mov r0, #5
        0xe3a0100D,  // mov r1, #13
        0xe0812000,  // add r2, r1, r0
        0xeafffffe,  // b +#0 (infinite loop)
    };

    jit.Regs() = {};
    jit.SetCpsr(0x000001d0);  // User-mode

    test_env.ticks_left = 4;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.Regs()[0] == 5);
    REQUIRE(jit.Regs()[1] == 13);
    REQUIRE(jit.Regs()[2] == 18);
    REQUIRE(jit.Regs()[15] == 0x0000000c);
    REQUIRE(jit.Cpsr() == 0x000001d0);

    // Change the code
    test_env.code_mem[1] = 0xe3a01007;  // mov r1, #7
    jit.InvalidateCacheRange(/*start_memory_location = */ 4, /* length_in_bytes = */ 4);

    // Reset position of PC
    jit.Regs()[15] = 0;

    test_env.ticks_left = 4;
    CheckedRun([&]() { jit.Run(); });
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.Regs()[0] == 5);
    REQUIRE(jit.Regs()[1] == 7);
    REQUIRE(jit.Regs()[2] == 12);
    REQUIRE(jit.Regs()[15] == 0x0000000c);
    REQUIRE(jit.Cpsr() == 0x000001d0);
}

TEST_CASE("arm: Step blx", "[arm]") {
    ArmTestEnv test_env;
    A32::UserConfig config = GetUserConfig(&test_env);
    config.optimizations |= OptimizationFlag::FastDispatch;
    Dynarmic::A32::Jit jit{config};
    test_env.code_mem = {
        0xe12fff30,  // blx r0
        0xe320f000,  // nop
        0xe320f000,  // nop
        0xe320f000,  // nop
        0xe320f000,  // nop
        0xe320f000,  // nop
        0xeafffffe,  // b +#0 (infinite loop)
    };

    jit.Regs()[0] = 8;
    jit.Regs()[15] = 0;       // PC = 0
    jit.SetCpsr(0x000001d0);  // User-mode

    test_env.ticks_left = 10;
    jit.Step();

    REQUIRE(jit.Regs()[0] == 8);
    REQUIRE(jit.Regs()[14] == 4);
    REQUIRE(jit.Regs()[15] == 8);
    REQUIRE(jit.Cpsr() == 0x000001d0);
}

TEST_CASE("arm: Step bx", "[arm]") {
    ArmTestEnv test_env;
    A32::UserConfig config = GetUserConfig(&test_env);
    config.optimizations |= OptimizationFlag::FastDispatch;
    Dynarmic::A32::Jit jit{config};
    test_env.code_mem = {
        0xe12fff10,  // bx r0
        0xe320f000,  // nop
        0xe320f000,  // nop
        0xe320f000,  // nop
        0xe320f000,  // nop
        0xe320f000,  // nop
        0xeafffffe,  // b +#0 (infinite loop)
    };

    jit.Regs()[0] = 8;
    jit.Regs()[15] = 0;       // PC = 0
    jit.SetCpsr(0x000001d0);  // User-mode

    test_env.ticks_left = 10;
    jit.Step();

    REQUIRE(jit.Regs()[0] == 8);
    REQUIRE(jit.Regs()[15] == 8);
    REQUIRE(jit.Cpsr() == 0x000001d0);
}

TEST_CASE("arm: Test stepping", "[arm]") {
    ArmTestEnv test_env;
    Dynarmic::A32::Jit jit{GetUserConfig(&test_env)};
    test_env.code_mem = {
        0xe320f000,  // nop
        0xe320f000,  // nop
        0xe320f000,  // nop
        0xe320f000,  // nop
        0xe320f000,  // nop

        0xe320f000,  // nop
        0xe320f000,  // nop
        0xe320f000,  // nop
        0xe320f000,  // nop
        0xe320f000,  // nop

        0xe320f000,  // nop
        0xe320f000,  // nop
        0xe320f000,  // nop
        0xe320f000,  // nop
        0xe320f000,  // nop

        0xe320f000,  // nop
        0xe320f000,  // nop
        0xe320f000,  // nop
        0xe320f000,  // nop
        0xe320f000,  // nop

        0xeafffffe,  // b +#0 (infinite loop)
    };

    jit.Regs()[0] = 8;
    jit.Regs()[15] = 0;       // PC = 0
    jit.SetCpsr(0x000001d0);  // User-mode

    for (size_t i = 0; i < 5; ++i) {
        test_env.ticks_left = 10;
        jit.Step();

        REQUIRE(jit.Regs()[15] == (i + 1) * 4);
        REQUIRE(jit.Cpsr() == 0x000001d0);
    }

    test_env.ticks_left = 20;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.Regs()[15] == 80);
    REQUIRE(jit.Cpsr() == 0x000001d0);
}

TEST_CASE("arm: Test stepping 2", "[arm]") {
    ArmTestEnv test_env;
    Dynarmic::A32::Jit jit{GetUserConfig(&test_env)};
    test_env.code_mem = {
        0xe12fff10,  // bx r0
        0xe320f000,  // nop
        0xe320f000,  // nop
        0xe320f000,  // nop
        0xe320f000,  // nop

        0xe320f000,  // nop
        0xe320f000,  // nop
        0xe320f000,  // nop
        0xe320f000,  // nop
        0xe320f000,  // nop

        0xe320f000,  // nop
        0xe320f000,  // nop
        0xe320f000,  // nop
        0xe320f000,  // nop
        0xe320f000,  // nop

        0xe320f000,  // nop
        0xe320f000,  // nop
        0xe320f000,  // nop
        0xe320f000,  // nop
        0xe320f000,  // nop

        0xeafffffe,  // b +#0 (infinite loop)
    };

    jit.Regs()[0] = 4;
    jit.Regs()[15] = 0;       // PC = 0
    jit.SetCpsr(0x000001d0);  // User-mode

    for (size_t i = 0; i < 5; ++i) {
        test_env.ticks_left = 10;
        jit.Step();

        REQUIRE(jit.Regs()[15] == (i + 1) * 4);
        REQUIRE(jit.Cpsr() == 0x000001d0);
    }

    test_env.ticks_left = 20;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.Regs()[15] == 80);
    REQUIRE(jit.Cpsr() == 0x000001d0);
}

TEST_CASE("arm: Test stepping 3", "[arm]") {
    ArmTestEnv test_env;
    Dynarmic::A32::Jit jit{GetUserConfig(&test_env)};
    test_env.code_mem = {
        0xe12fff10,  // bx r0
        0xe320f000,  // nop
        0xe320f000,  // nop
        0xe320f000,  // nop
        0xe320f000,  // nop

        0xeafffffe,  // b +#0 (infinite loop)
    };

    jit.Regs()[0] = 4;
    jit.Regs()[15] = 0;       // PC = 0
    jit.SetCpsr(0x000001d0);  // User-mode

    test_env.ticks_left = 10;
    jit.Step();

    REQUIRE(jit.Regs()[15] == 4);
    REQUIRE(jit.Cpsr() == 0x000001d0);

    test_env.ticks_left = 20;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.Regs()[15] == 20);
    REQUIRE(jit.Cpsr() == 0x000001d0);
}

TEST_CASE("arm: PackedAbsDiffSumS8", "[arm][A32]") {
    // This was a randomized test-case that was failing.
    // In circumstances there were cases when the upper 32 bits of an argument to psadbw were not zero.

    ArmTestEnv test_env;
    A32::Jit jit{GetUserConfig(&test_env)};
    test_env.code_mem = {
        0x87414354,  // smlsldhi r4, r1, r4, r3
        0xe7886412,  // usad8a r8, r2, r4, r6
        0xeafffffe,  // b +#0
    };

    jit.Regs() = {
        0xea85297c,
        0x417ad918,
        0x64f8b70b,
        0xcca0373e,
        0xbc722361,
        0xc528c69e,
        0xca926de8,
        0xd665d210,
        0xb5650555,
        0x4a24b25b,
        0xaed44144,
        0xe87230b2,
        0x98e391de,
        0x126efc0c,
        0xe591fd11,
        0x00000000,
    };
    jit.SetCpsr(0xb0000010);

    test_env.ticks_left = 3;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.Regs()[0] == 0xea85297c);
    REQUIRE(jit.Regs()[1] == 0x417ad918);
    REQUIRE(jit.Regs()[2] == 0x64f8b70b);
    REQUIRE(jit.Regs()[3] == 0xcca0373e);
    REQUIRE(jit.Regs()[4] == 0xb685ec9f);
    REQUIRE(jit.Regs()[5] == 0xc528c69e);
    REQUIRE(jit.Regs()[6] == 0xca926de8);
    REQUIRE(jit.Regs()[7] == 0xd665d210);
    REQUIRE(jit.Regs()[8] == 0xca926f76);
    REQUIRE(jit.Regs()[9] == 0x4a24b25b);
    REQUIRE(jit.Regs()[10] == 0xaed44144);
    REQUIRE(jit.Regs()[11] == 0xe87230b2);
    REQUIRE(jit.Regs()[12] == 0x98e391de);
    REQUIRE(jit.Regs()[13] == 0x126efc0c);
    REQUIRE(jit.Regs()[14] == 0xe591fd11);
    REQUIRE(jit.Regs()[15] == 0x00000008);
    REQUIRE(jit.Cpsr() == 0xb0000010);
}

TEST_CASE("arm: vclt.f32 with zero", "[arm][A32]") {
    ArmTestEnv test_env;
    A32::Jit jit{GetUserConfig(&test_env)};
    test_env.code_mem = {
        0xf3b93628,  // vclt.f32 d3, d24, #0
        0xeafffffe,  // b +#0
    };

    jit.ExtRegs()[48] = 0x3a87d9f1;
    jit.ExtRegs()[49] = 0x80796dc0;

    jit.SetCpsr(0x000001d0);  // User-mode

    test_env.ticks_left = 2;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.ExtRegs()[6] == 0x00000000);
    REQUIRE(jit.ExtRegs()[7] == 0x00000000);
}

TEST_CASE("arm: vcvt.s16.f64", "[arm][A32]") {
    ArmTestEnv test_env;
    A32::Jit jit{GetUserConfig(&test_env)};
    test_env.code_mem = {
        0xeebe8b45,  // vcvt.s16.f64 d8, d8, #6
        0xeafffffe,  // b +#0
    };

    jit.ExtRegs()[16] = 0x9a7110b0;
    jit.ExtRegs()[17] = 0xcd78f4e7;

    jit.SetCpsr(0x000001d0);  // User-mode

    test_env.ticks_left = 2;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.ExtRegs()[16] == 0xffff8000);
    REQUIRE(jit.ExtRegs()[17] == 0xffffffff);
}

TEST_CASE("arm: Memory access (fastmem)", "[arm][A32]") {
    constexpr size_t address_width = 12;
    constexpr size_t memory_size = 1ull << address_width;  // 4K
    constexpr size_t page_size = 4 * 1024;
    constexpr size_t buffer_size = 2 * page_size;
    char buffer[buffer_size];

    void* buffer_ptr = reinterpret_cast<void*>(buffer);
    size_t buffer_size_nconst = buffer_size;
    char* backing_memory = reinterpret_cast<char*>(std::align(page_size, memory_size, buffer_ptr, buffer_size_nconst));

    A32FastmemTestEnv env{backing_memory};
    Dynarmic::A32::UserConfig config{};
    config.callbacks = &env;
    config.fastmem_pointer = reinterpret_cast<uintptr_t>(backing_memory);
    config.recompile_on_fastmem_failure = false;
    config.processor_id = 0;

    Dynarmic::A32::Jit jit{config};
    memset(backing_memory, 0, memory_size);
    memcpy(backing_memory + 0x100, "Lorem ipsum dolor sit amet, consectetur adipiscing elit.", 57);

    env.MemoryWrite(0, 0xE5904000, sizeof(u32));  // LDR R4, [R0]
    env.MemoryWrite(4, 0xE5814000, sizeof(u32));  // STR R4, [R1]
    env.MemoryWrite(8, 0xEAFFFFFE, sizeof(u32));  // B .
    jit.Regs()[0] = 0x100;
    jit.Regs()[1] = 0x1F0;
    jit.Regs()[15] = 0;       // PC = 0
    jit.SetCpsr(0x000001d0);  // User-mode
    env.ticks_left = 3;

    CheckedRun([&]() { jit.Run(); });
    REQUIRE(strncmp(backing_memory + 0x100, backing_memory + 0x1F0, 4) == 0);
}

TEST_CASE("arm: vmsr, vcmp, vmrs", "[arm][A32]") {
    ArmTestEnv test_env;
    A32::Jit jit{GetUserConfig(&test_env)};
    test_env.code_mem = {
        0xeee10a10,  // vmsr fpscr, r0
        0xeeb48a4a,  // vcmp.f32 s16, s20
        0xeef1fa10,  // vmrs apsr_nzcv, fpscr
        0xe12fff1e,  // bx lr
    };

    jit.ExtRegs()[16] = 0xFF7FFFFF;
    jit.ExtRegs()[20] = 0xFF7FFFFF;

    jit.Regs()[0] = 0x60000000;

    jit.SetFpscr(0x3ee22ac0);
    jit.SetCpsr(0x60000000);  // User-mode

    test_env.ticks_left = 4;
    CheckedRun([&]() { jit.Run(); });
}

TEST_CASE("arm: sdiv maximally", "[arm][A32]") {
    ArmTestEnv test_env;
    A32::Jit jit{GetUserConfig(&test_env)};
    test_env.code_mem = {
        0xe712f011,  // sdiv r2, r1, r0
        0xeafffffe,  // b +#0
    };

    jit.Regs()[1] = 0x80000000;
    jit.Regs()[0] = 0xffffffff;

    jit.SetCpsr(0x000001d0);  // User-mode

    test_env.ticks_left = 2;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.Regs()[2] == 0x80000000);
}

TEST_CASE("arm: tbl", "[arm][A32]") {
    ArmTestEnv test_env;
    A32::Jit jit{GetUserConfig(&test_env)};

    test_env.code_mem.emplace_back(0xf3f408a0);  // vtbl.8  d16, {d20               }, d16
    test_env.code_mem.emplace_back(0xf3f419a1);  // vtbl.8  d17, {d20, d21          }, d17
    test_env.code_mem.emplace_back(0xf3f42aa2);  // vtbl.8  d18, {d20, d21, d22     }, d18
    test_env.code_mem.emplace_back(0xf3f43ba3);  // vtbl.8  d19, {d20, d21, d22, d23}, d19
    test_env.code_mem.emplace_back(0xeafffffe);  // b +#0

    // Indices
    jit.ExtRegs()[16 * 2 + 0] = 0x05'02'01'00;
    jit.ExtRegs()[16 * 2 + 1] = 0x20'1F'10'0F;

    jit.ExtRegs()[17 * 2 + 0] = 0x05'02'01'00;
    jit.ExtRegs()[17 * 2 + 1] = 0x20'1F'10'0F;

    jit.ExtRegs()[18 * 2 + 0] = 0x05'02'01'00;
    jit.ExtRegs()[18 * 2 + 1] = 0x20'1F'10'0F;

    jit.ExtRegs()[19 * 2 + 0] = 0x05'02'01'00;
    jit.ExtRegs()[19 * 2 + 1] = 0x20'1F'10'0F;

    // Table
    jit.ExtRegs()[20 * 2 + 0] = 0x03'02'01'00;
    jit.ExtRegs()[20 * 2 + 1] = 0x07'06'05'04;
    jit.ExtRegs()[21 * 2 + 0] = 0x0B'0A'09'08;
    jit.ExtRegs()[21 * 2 + 1] = 0x0F'0E'0D'0C;
    jit.ExtRegs()[22 * 2 + 0] = 0x13'12'11'10;
    jit.ExtRegs()[22 * 2 + 1] = 0x17'16'15'14;
    jit.ExtRegs()[23 * 2 + 0] = 0x1B'1A'19'18;
    jit.ExtRegs()[23 * 2 + 1] = 0x1F'1E'1D'1C;

    test_env.ticks_left = 5;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.ExtRegs()[16 * 2 + 0] == 0x05'02'01'00);
    REQUIRE(jit.ExtRegs()[16 * 2 + 1] == 0x00'00'00'00);

    REQUIRE(jit.ExtRegs()[17 * 2 + 0] == 0x05'02'01'00);
    REQUIRE(jit.ExtRegs()[17 * 2 + 1] == 0x00'00'00'0F);

    REQUIRE(jit.ExtRegs()[18 * 2 + 0] == 0x05'02'01'00);
    REQUIRE(jit.ExtRegs()[18 * 2 + 1] == 0x00'00'10'0F);

    REQUIRE(jit.ExtRegs()[19 * 2 + 0] == 0x05'02'01'00);
    REQUIRE(jit.ExtRegs()[19 * 2 + 1] == 0x00'1F'10'0F);
}

TEST_CASE("arm: tbx", "[arm][A32]") {
    ArmTestEnv test_env;
    A32::Jit jit{GetUserConfig(&test_env)};

    test_env.code_mem.emplace_back(0xf3f408e0);  // vtbx.8  d16, {d20               }, d16
    test_env.code_mem.emplace_back(0xf3f419e1);  // vtbx.8  d17, {d20, d21          }, d17
    test_env.code_mem.emplace_back(0xf3f42ae2);  // vtbx.8  d18, {d20, d21, d22     }, d18
    test_env.code_mem.emplace_back(0xf3f43be3);  // vtbx.8  d19, {d20, d21, d22, d23}, d19
    test_env.code_mem.emplace_back(0xeafffffe);  // b +#0

    // Indices
    jit.ExtRegs()[16 * 2 + 0] = 0x05'02'01'00;
    jit.ExtRegs()[16 * 2 + 1] = 0x20'1F'10'0F;

    jit.ExtRegs()[17 * 2 + 0] = 0x05'02'01'00;
    jit.ExtRegs()[17 * 2 + 1] = 0x20'1F'10'0F;

    jit.ExtRegs()[18 * 2 + 0] = 0x05'02'01'00;
    jit.ExtRegs()[18 * 2 + 1] = 0x20'1F'10'0F;

    jit.ExtRegs()[19 * 2 + 0] = 0x05'02'01'00;
    jit.ExtRegs()[19 * 2 + 1] = 0x20'1F'10'0F;

    // Table
    jit.ExtRegs()[20 * 2 + 0] = 0x03'02'01'00;
    jit.ExtRegs()[20 * 2 + 1] = 0x07'06'05'04;

    jit.ExtRegs()[21 * 2 + 0] = 0x0B'0A'09'08;
    jit.ExtRegs()[21 * 2 + 1] = 0x0F'0E'0D'0C;

    jit.ExtRegs()[22 * 2 + 0] = 0x13'12'11'10;
    jit.ExtRegs()[22 * 2 + 1] = 0x17'16'15'14;

    jit.ExtRegs()[23 * 2 + 0] = 0x1B'1A'19'18;
    jit.ExtRegs()[23 * 2 + 1] = 0x1F'1E'1D'1C;

    test_env.ticks_left = 5;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.ExtRegs()[16 * 2 + 0] == 0x05'02'01'00);
    REQUIRE(jit.ExtRegs()[16 * 2 + 1] == 0x20'1F'10'0F);

    REQUIRE(jit.ExtRegs()[17 * 2 + 0] == 0x05'02'01'00);
    REQUIRE(jit.ExtRegs()[17 * 2 + 1] == 0x20'1F'10'0F);

    REQUIRE(jit.ExtRegs()[18 * 2 + 0] == 0x05'02'01'00);
    REQUIRE(jit.ExtRegs()[18 * 2 + 1] == 0x20'1F'10'0F);

    REQUIRE(jit.ExtRegs()[19 * 2 + 0] == 0x05'02'01'00);
    REQUIRE(jit.ExtRegs()[19 * 2 + 1] == 0x20'1F'10'0F);
}


TEST_CASE("arm: SHA-1 of one block with the crypto extension", "[arm][A32]") {
    // SHA-1("abc"), one padded block, through SHA1C/P/M, SHA1H, SHA1SU0 and SHA1SU1: the state in
    // q0 and s4, a copy in q2 and s12, the message in q8-q11, the round constants in q12-q15.
    ArmTestEnv test_env;
    A32::Jit jit{GetUserConfig(&test_env)};
    test_env.code_mem = {
        0xf22088e8,  // vadd.i32 q4, q8, q12
        0xf2720ce4,  // sha1su0.32 q8, q9, q10
        0xf3fa03a6,  // sha1su1.32 q8, q11
        0xf3b9a2c0,  // sha1h.32 q5, q0
        0xf2020c48,  // sha1c.32 q0, q1, q4
        0xf22288e8,  // vadd.i32 q4, q9, q12
        0xf2742ce6,  // sha1su0.32 q9, q10, q11
        0xf3fa23a0,  // sha1su1.32 q9, q8
        0xf3b922c0,  // sha1h.32 q1, q0
        0xf20a0c48,  // sha1c.32 q0, q5, q4
        0xf22488e8,  // vadd.i32 q4, q10, q12
        0xf2764ce0,  // sha1su0.32 q10, q11, q8
        0xf3fa43a2,  // sha1su1.32 q10, q9
        0xf3b9a2c0,  // sha1h.32 q5, q0
        0xf2020c48,  // sha1c.32 q0, q1, q4
        0xf22688e8,  // vadd.i32 q4, q11, q12
        0xf2706ce2,  // sha1su0.32 q11, q8, q9
        0xf3fa63a4,  // sha1su1.32 q11, q10
        0xf3b922c0,  // sha1h.32 q1, q0
        0xf20a0c48,  // sha1c.32 q0, q5, q4
        0xf22088e8,  // vadd.i32 q4, q8, q12
        0xf2720ce4,  // sha1su0.32 q8, q9, q10
        0xf3fa03a6,  // sha1su1.32 q8, q11
        0xf3b9a2c0,  // sha1h.32 q5, q0
        0xf2020c48,  // sha1c.32 q0, q1, q4
        0xf22288ea,  // vadd.i32 q4, q9, q13
        0xf2742ce6,  // sha1su0.32 q9, q10, q11
        0xf3fa23a0,  // sha1su1.32 q9, q8
        0xf3b922c0,  // sha1h.32 q1, q0
        0xf21a0c48,  // sha1p.32 q0, q5, q4
        0xf22488ea,  // vadd.i32 q4, q10, q13
        0xf2764ce0,  // sha1su0.32 q10, q11, q8
        0xf3fa43a2,  // sha1su1.32 q10, q9
        0xf3b9a2c0,  // sha1h.32 q5, q0
        0xf2120c48,  // sha1p.32 q0, q1, q4
        0xf22688ea,  // vadd.i32 q4, q11, q13
        0xf2706ce2,  // sha1su0.32 q11, q8, q9
        0xf3fa63a4,  // sha1su1.32 q11, q10
        0xf3b922c0,  // sha1h.32 q1, q0
        0xf21a0c48,  // sha1p.32 q0, q5, q4
        0xf22088ea,  // vadd.i32 q4, q8, q13
        0xf2720ce4,  // sha1su0.32 q8, q9, q10
        0xf3fa03a6,  // sha1su1.32 q8, q11
        0xf3b9a2c0,  // sha1h.32 q5, q0
        0xf2120c48,  // sha1p.32 q0, q1, q4
        0xf22288ea,  // vadd.i32 q4, q9, q13
        0xf2742ce6,  // sha1su0.32 q9, q10, q11
        0xf3fa23a0,  // sha1su1.32 q9, q8
        0xf3b922c0,  // sha1h.32 q1, q0
        0xf21a0c48,  // sha1p.32 q0, q5, q4
        0xf22488ec,  // vadd.i32 q4, q10, q14
        0xf2764ce0,  // sha1su0.32 q10, q11, q8
        0xf3fa43a2,  // sha1su1.32 q10, q9
        0xf3b9a2c0,  // sha1h.32 q5, q0
        0xf2220c48,  // sha1m.32 q0, q1, q4
        0xf22688ec,  // vadd.i32 q4, q11, q14
        0xf2706ce2,  // sha1su0.32 q11, q8, q9
        0xf3fa63a4,  // sha1su1.32 q11, q10
        0xf3b922c0,  // sha1h.32 q1, q0
        0xf22a0c48,  // sha1m.32 q0, q5, q4
        0xf22088ec,  // vadd.i32 q4, q8, q14
        0xf2720ce4,  // sha1su0.32 q8, q9, q10
        0xf3fa03a6,  // sha1su1.32 q8, q11
        0xf3b9a2c0,  // sha1h.32 q5, q0
        0xf2220c48,  // sha1m.32 q0, q1, q4
        0xf22288ec,  // vadd.i32 q4, q9, q14
        0xf2742ce6,  // sha1su0.32 q9, q10, q11
        0xf3fa23a0,  // sha1su1.32 q9, q8
        0xf3b922c0,  // sha1h.32 q1, q0
        0xf22a0c48,  // sha1m.32 q0, q5, q4
        0xf22488ec,  // vadd.i32 q4, q10, q14
        0xf2764ce0,  // sha1su0.32 q10, q11, q8
        0xf3fa43a2,  // sha1su1.32 q10, q9
        0xf3b9a2c0,  // sha1h.32 q5, q0
        0xf2220c48,  // sha1m.32 q0, q1, q4
        0xf22688ee,  // vadd.i32 q4, q11, q15
        0xf2706ce2,  // sha1su0.32 q11, q8, q9
        0xf3fa63a4,  // sha1su1.32 q11, q10
        0xf3b922c0,  // sha1h.32 q1, q0
        0xf21a0c48,  // sha1p.32 q0, q5, q4
        0xf22088ee,  // vadd.i32 q4, q8, q15
        0xf3b9a2c0,  // sha1h.32 q5, q0
        0xf2120c48,  // sha1p.32 q0, q1, q4
        0xf22288ee,  // vadd.i32 q4, q9, q15
        0xf3b922c0,  // sha1h.32 q1, q0
        0xf21a0c48,  // sha1p.32 q0, q5, q4
        0xf22488ee,  // vadd.i32 q4, q10, q15
        0xf3b9a2c0,  // sha1h.32 q5, q0
        0xf2120c48,  // sha1p.32 q0, q1, q4
        0xf22688ee,  // vadd.i32 q4, q11, q15
        0xf3b922c0,  // sha1h.32 q1, q0
        0xf21a0c48,  // sha1p.32 q0, q5, q4
        0xf2200844,  // vadd.i32 q0, q0, q2
        0xf2222846,  // vadd.i32 q1, q1, q3
        0xeafffffe,  // b +#0
    };

    const std::array<u32, 16> message{0x61626380, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x18};
    const std::array<u32, 4> constants{0x5a827999, 0x6ed9eba1, 0x8f1bbcdc, 0xca62c1d6};
    const std::array<u32, 4> abcd{0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
    const u32 e = 0xc3d2e1f0;
    for (size_t i = 0; i < message.size(); i++) {
        jit.ExtRegs()[32 + i] = message[i];
    }
    for (size_t i = 0; i < 16; i++) {
        jit.ExtRegs()[48 + i] = constants[i / 4];
    }
    for (size_t i = 0; i < 4; i++) {
        jit.ExtRegs()[0 + i] = abcd[i];
        jit.ExtRegs()[8 + i] = abcd[i];
    }
    jit.ExtRegs()[4] = e;
    jit.ExtRegs()[12] = e;

    jit.SetCpsr(0x000001d0);  // User-mode

    test_env.ticks_left = test_env.code_mem.size();
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.ExtRegs()[0] == 0xa9993e36);
    REQUIRE(jit.ExtRegs()[1] == 0x4706816a);
    REQUIRE(jit.ExtRegs()[2] == 0xba3e2571);
    REQUIRE(jit.ExtRegs()[3] == 0x7850c26c);
    REQUIRE(jit.ExtRegs()[4] == 0x9cd0d89d);
}

TEST_CASE("arm: SHA1H clears the rest of the destination", "[arm][A32]") {
    ArmTestEnv test_env;
    A32::Jit jit{GetUserConfig(&test_env)};
    test_env.code_mem = {
        0xf3b9a2c0,  // sha1h.32 q5, q0
        0xeafffffe,  // b +#0
    };

    jit.ExtRegs()[0] = 0x80000001;
    for (size_t i = 20; i < 24; i++) {
        jit.ExtRegs()[i] = 0xdeadbeef;
    }

    jit.SetCpsr(0x000001d0);  // User-mode

    test_env.ticks_left = 2;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.ExtRegs()[20] == 0x60000000);
    REQUIRE(jit.ExtRegs()[21] == 0);
    REQUIRE(jit.ExtRegs()[22] == 0);
    REQUIRE(jit.ExtRegs()[23] == 0);
}

TEST_CASE("arm: sdiv and udiv by zero", "[arm][A32]") {
    ArmTestEnv test_env;
    A32::Jit jit{GetUserConfig(&test_env)};
    test_env.code_mem = {
        0xe712f011,  // sdiv r2, r1, r0
        0xe733f011,  // udiv r3, r1, r0
        0xeafffffe,  // b +#0
    };

    jit.Regs()[0] = 0;
    jit.Regs()[1] = 0x12345678;
    jit.Regs()[2] = 0xdeadbeef;
    jit.Regs()[3] = 0xdeadbeef;

    jit.SetCpsr(0x000001d0);  // User-mode

    test_env.ticks_left = 3;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.Regs()[2] == 0);
    REQUIRE(jit.Regs()[3] == 0);
}

TEST_CASE("arm: vrintx and vrinta through the soft-float fallback", "[arm][A32]") {
    ArmTestEnv test_env;
    A32::Jit jit{GetUserConfig(&test_env)};
    test_env.code_mem = {
        0xeeb70b41,  // vrintx.f64 d0, d1
        0xfeb82b43,  // vrinta.f64 d2, d3
        0xeafffffe,  // b +#0
    };

    jit.ExtRegs()[2] = 0x33333333;  // d1 = 0.3
    jit.ExtRegs()[3] = 0x3fd33333;
    jit.ExtRegs()[6] = 0x00000000;  // d3 = 2.5
    jit.ExtRegs()[7] = 0x40040000;
    jit.SetFpscr(0);  // round to nearest

    jit.SetCpsr(0x000001d0);  // User-mode

    test_env.ticks_left = 3;
    CheckedRun([&]() { jit.Run(); });

    REQUIRE(jit.ExtRegs()[0] == 0);  // 0.0
    REQUIRE(jit.ExtRegs()[1] == 0);
    REQUIRE(jit.ExtRegs()[4] == 0);  // 3.0, ties away from zero
    REQUIRE(jit.ExtRegs()[5] == 0x40080000);
}
