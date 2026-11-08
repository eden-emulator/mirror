// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "dynarmic/tests/A32/testenv.h"
#include "dynarmic/interface/A32/a32.h"

using namespace Dynarmic;

namespace {

// Halts execution with MemoryAbort when a watched address is read.
class WatchpointEnv final : public ArmTestEnv {
public:
    Dynarmic::A32::Jit* jit = nullptr;
    u32 watch_addr = 0xffffffff;
    bool aborted = false;

    u64 MemoryRead(u32 vaddr, size_t size) override {
        if (vaddr == watch_addr && jit != nullptr) {
            aborted = true;
            jit->HaltExecution(static_cast<Dynarmic::HaltReason>(HaltReason::MemoryAbort));
            return 0;
        }
        return ArmTestEnv::MemoryRead(vaddr, size);
    }
};

A32::UserConfig GetUserConfig(WatchpointEnv* env) {
    A32::UserConfig user_config;
    user_config.callbacks = env;
    user_config.check_halt_on_memory_access = true;
    return user_config;
}

}

TEST_CASE("arm: mid-block watchpoint abort yields instruction-accurate context", "[arm][A32]") {
    WatchpointEnv test_env;
    A32::Jit jit{GetUserConfig(&test_env)};
    test_env.jit = &jit;

    test_env.code_mem = {
        0xe3a01011,  // mov r1, #0x11
        0xe5904000,  // ldr r4, [r0]  (watched load)
        0xe3a01022,  // mov r1, #0x22 (overrides r1 with check_halt_on_memory_access)
        0xeafffffe,  // b .
    };

    constexpr u32 kWatch = 0x00100000;
    test_env.watch_addr = kWatch;

    jit.Regs() = {};
    jit.Regs()[0] = kWatch;      // r0 = watched address
    jit.Regs()[1] = 0xdeadbeef;
    jit.Regs()[4] = 0xdeadbeef;
    jit.Regs()[15] = 0;
    jit.SetCpsr(0x000001d0);

    test_env.ticks_left = 16;
    const HaltReason hr = jit.Run();

    REQUIRE(test_env.aborted);
    REQUIRE(Has(hr, HaltReason::MemoryAbort));

    // r1 must contain 0x11 from first mov
    REQUIRE(jit.Regs()[1] == 0x11);

    // The faulting load must NOT have committed.
    REQUIRE(jit.Regs()[4] == 0xdeadbeef);

    // PC must point at the faulting instruction.
    REQUIRE(jit.Regs()[15] == 0x04);
}
