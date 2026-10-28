// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "dynarmic/tests/A64/testenv.h"

namespace {
class ReturnTestEnv final : public A64TestEnv {
public:
    Dynarmic::A64::Jit* jit{};
    std::optional<Dynarmic::A64::Exception> exception;

    void ExceptionRaised(u64, Dynarmic::A64::Exception reason) override {
        exception = reason;
        jit->HaltExecution(Dynarmic::HaltReason::UserDefined1);
    }
};
} // namespace

TEST_CASE("A64: authenticated returns with disabled authentication keys", "[a64][pauth]") {
    const u32 instruction = GENERATE(0xD65F0BFFU, 0xD65F0FFFU); // RETAA, RETAB
    const bool supported = GENERATE(false, true);
    ReturnTestEnv env;
    env.code_mem = {instruction};
    Dynarmic::A64::UserConfig config;
    config.callbacks = &env;
    config.enable_pauth_returns = supported;
    config.code_cache_size = 16 * 1024 * 1024;
    Dynarmic::A64::Jit jit{config};
    env.jit = &jit;
    jit.SetPC(0);
    jit.SetRegister(30, 0x123400);
    jit.SetSP(0x567800);
    env.ticks_left = 1;
    jit.Step();
    if (supported) {
        REQUIRE_FALSE(env.exception.has_value());
        REQUIRE(jit.GetPC() == 0x123400);
    } else {
        REQUIRE(env.exception == Dynarmic::A64::Exception::UnallocatedEncoding);
    }
    REQUIRE(jit.GetRegister(30) == 0x123400);
    REQUIRE(jit.GetSP() == 0x567800);
}

TEST_CASE("A64: PAC hint and return agree on disabled keys", "[a64][pauth]") {
    ReturnTestEnv env;
    env.code_mem = {0xD503237FU, 0xD65F0FFFU}; // PACIBSP; RETAB
    Dynarmic::A64::UserConfig config;
    config.callbacks = &env;
    config.enable_pauth_returns = true;
    config.code_cache_size = 16 * 1024 * 1024;
    Dynarmic::A64::Jit jit{config};
    env.jit = &jit;
    jit.SetPC(0);
    jit.SetRegister(30, 0x123400);
    jit.SetSP(0x567800);
    env.ticks_left = 2;
    jit.Step();
    REQUIRE(jit.GetPC() == 4);
    REQUIRE(jit.GetRegister(30) == 0x123400);
    jit.Step();
    REQUIRE_FALSE(env.exception.has_value());
    REQUIRE(jit.GetPC() == 0x123400);
    REQUIRE(jit.GetRegister(30) == 0x123400);
    REQUIRE(jit.GetSP() == 0x567800);
}

TEST_CASE("A64: PAC stripping uses the configured address width", "[a64][pauth]") {
    const u32 instruction = GENERATE(0xDAC143F4U, 0xDAC147F4U, 0xD50320FFU);
    const u32 bits = GENERATE(32U, 36U, 39U);
    const bool negative = GENERATE(false, true);
    const u32 reg = instruction == 0xD50320FFU ? 30 : 20;
    const u64 mask = (u64{1} << bits) - 1;
    const u64 address = 0x123456789ULL & mask;
    const u64 pointer = (0xAAULL << 56) | ((negative ? 0xD5ULL : 0x55ULL) << 48) | address;
    ReturnTestEnv env;
    env.code_mem = {instruction};
    Dynarmic::A64::UserConfig config;
    config.callbacks = &env;
    config.enable_pauth_returns = true;
    config.page_table_address_space_bits = bits;
    config.code_cache_size = 16 * 1024 * 1024;
    Dynarmic::A64::Jit jit{config};
    env.jit = &jit;
    jit.SetRegister(reg, pointer);
    jit.SetPstate(0xA0000000);
    env.ticks_left = 1;
    jit.Step();
    REQUIRE_FALSE(env.exception.has_value());
    REQUIRE(jit.GetRegister(reg) == (negative ? address | ~mask : address));
    REQUIRE(jit.GetPstate() == 0xA0000000);
}
