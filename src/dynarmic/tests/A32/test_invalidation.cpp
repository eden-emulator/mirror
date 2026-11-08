// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "dynarmic/tests/A32/testenv.h"
#include "dynarmic/tests/native/testenv.h"
#include "dynarmic/interface/A32/a32.h"

using namespace Dynarmic;

TEST_CASE("arm: ensure fast dispatch entry is cleared even when a block does not have any patching requirements", "[arm][A32]") {
    ArmTestEnv env;
    A32::UserConfig conf{};
    conf.callbacks = &env;
    A32::Jit jit{conf};

    REQUIRE(conf.HasOptimization(OptimizationFlag::FastDispatch));

    env.code_mem.clear();
    env.code_mem.emplace_back(0xe3a0000c);  // mov r0, #12
    env.code_mem.emplace_back(0xe12fff10);  // bx r0
    env.code_mem.emplace_back(0xe3a0002a);  // mov r0, #42
    env.code_mem.emplace_back(0xe3a0002a);  // mov r0, #42
    env.code_mem.emplace_back(0xeafffffe);  // b .

    jit.SetCpsr(0x000001d0);

    jit.Regs()[15] = 0;
    env.ticks_left = 4;
    CheckedRun([&]() { jit.Run(); });
    REQUIRE(jit.Regs()[0] == 42);

    jit.Regs()[15] = 0;
    env.ticks_left = 4;
    CheckedRun([&]() { jit.Run(); });
    REQUIRE(jit.Regs()[0] == 42);

    jit.InvalidateCacheRange(0x0c, 4);

    jit.Regs()[15] = 0;
    env.ticks_left = 4;
    CheckedRun([&]() { jit.Run(); });
    REQUIRE(jit.Regs()[0] == 42);

    env.code_mem[3] = 0xe3a00045;  // mov r0, #69

    jit.Regs()[15] = 0;
    env.ticks_left = 4;
    CheckedRun([&]() { jit.Run(); });
    REQUIRE(jit.Regs()[0] == 42);

    jit.InvalidateCacheRange(0x0c, 4);

    jit.Regs()[15] = 0;
    env.ticks_left = 4;
    CheckedRun([&]() { jit.Run(); });
    REQUIRE(jit.Regs()[0] == 69);

    jit.Regs()[15] = 0;
    env.ticks_left = 4;
    CheckedRun([&]() { jit.Run(); });
    REQUIRE(jit.Regs()[0] == 69);
}

TEST_CASE("arm: ensure fast dispatch entry is cleared even when a block does not have any patching requirements 2", "[arm][A32]") {
    ArmTestEnv env;
    A32::UserConfig conf{};
    conf.callbacks = &env;
    A32::Jit jit{conf};

    REQUIRE(conf.HasOptimization(OptimizationFlag::FastDispatch));

    env.code_mem.emplace_back(0xe3a00008);  // mov r0, #8
    env.code_mem.emplace_back(0xe12fff10);  // bx r0
    env.code_mem.emplace_back(0xe3a0002a);  // mov r0, #42
    env.code_mem.emplace_back(0xeafffffe);  // b .

    jit.SetCpsr(0x000001d0);

    jit.Regs()[15] = 0;
    env.ticks_left = 4;
    CheckedRun([&]() { jit.Run(); });
    REQUIRE(jit.Regs()[0] == 42);

    jit.Regs()[15] = 0;
    env.ticks_left = 4;
    CheckedRun([&]() { jit.Run(); });
    REQUIRE(jit.Regs()[0] == 42);

    jit.InvalidateCacheRange(0x08, 4);

    jit.Regs()[15] = 0;
    env.ticks_left = 4;
    CheckedRun([&]() { jit.Run(); });
    REQUIRE(jit.Regs()[0] == 42);

    env.code_mem[2] = 0xe3a00045;  // mov r0, #69

    jit.Regs()[15] = 0;
    env.ticks_left = 4;
    CheckedRun([&]() { jit.Run(); });
    REQUIRE(jit.Regs()[0] == 42);

    jit.InvalidateCacheRange(0x08, 4);

    jit.Regs()[15] = 0;
    env.ticks_left = 4;
    CheckedRun([&]() { jit.Run(); });
    REQUIRE(jit.Regs()[0] == 69);

    jit.Regs()[15] = 0;
    env.ticks_left = 4;
    CheckedRun([&]() { jit.Run(); });
    REQUIRE(jit.Regs()[0] == 69);
}
