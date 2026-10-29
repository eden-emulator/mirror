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

TEST_CASE("A64: address authentication instructions preserve pointers with disabled keys", "[a64][pauth]") {
    const u32 operation = GENERATE(0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U);
    const bool zero = GENERATE(false, true);
    const bool valid = GENERATE(false, true);
    const bool enabled = GENERATE(false, true);
    const u32 modifier = valid ? 31U : 7U;
    ReturnTestEnv env;
    env.code_mem = {0xdac10000U | (u32{zero} << 13) | (operation << 10) | (modifier << 5) | 30U};
    Dynarmic::A64::UserConfig config;
    config.callbacks = &env;
    config.enable_pauth_returns = enabled;
    config.code_cache_size = 16 * 1024 * 1024;
    Dynarmic::A64::Jit jit{config};
    env.jit = &jit;
    jit.SetRegister(30, 0xab55ffff87654321ULL);
    jit.SetRegister(7, 0x123456789abcdef0ULL);
    jit.SetSP(0x123400);
    jit.SetPstate(0xa0000000);
    env.ticks_left = 1;
    jit.Step();
    REQUIRE(env.exception.has_value() == (!enabled || (zero && !valid)));
    REQUIRE(jit.GetRegister(30) == 0xab55ffff87654321ULL);
    REQUIRE(jit.GetRegister(7) == 0x123456789abcdef0ULL);
    REQUIRE(jit.GetSP() == 0x123400);
    REQUIRE(jit.GetPstate() == 0xa0000000);
}

TEST_CASE("A64: generic authentication uses QARMA5 with reset generic key", "[a64][pauth]") {
    struct Vector { u64 data; u64 modifier; u64 result; };
    const Vector vector = GENERATE(
        Vector{0x0000000000000000ULL, 0x0000000000000000ULL, 0x76243b9500000000ULL},
        Vector{0xffffffffffffffffULL, 0xffffffffffffffffULL, 0x464bb6c800000000ULL},
        Vector{0x00000019a9b521d8ULL, 0x8000000087a18800ULL, 0x2f8205a000000000ULL},
        Vector{0x123456789abcdef0ULL, 0xfedcba9876543210ULL, 0xadb135fe00000000ULL},
        Vector{0x0000000000000001ULL, 0x0000000000000000ULL, 0x6caca44200000000ULL},
        Vector{0x0000000000000000ULL, 0x0000000000000001ULL, 0xa66efb5700000000ULL}
    );
    const u32 destination = GENERATE(0U, 1U, 2U, 31U);
    const bool stack_modifier = GENERATE(false, true);
    const bool enabled = GENERATE(false, true);
    ReturnTestEnv env;
    const u32 modifier = stack_modifier ? 31U : 1U;
    env.code_mem = {0x9ac03000U | (modifier << 16) | destination};
    Dynarmic::A64::UserConfig config;
    config.callbacks = &env;
    config.enable_pauth_returns = enabled;
    config.code_cache_size = 16 * 1024 * 1024;
    Dynarmic::A64::Jit jit{config};
    env.jit = &jit;
    jit.SetRegister(0, vector.data);
    jit.SetRegister(1, vector.modifier);
    jit.SetRegister(2, 0xabcdef);
    jit.SetRegister(3, 0x123456);
    jit.SetSP(vector.modifier);
    jit.SetPstate(0xa0000000);
    env.ticks_left = 1;
    jit.Step();
    REQUIRE(env.exception.has_value() == !enabled);
    if (enabled && destination != 31) REQUIRE(jit.GetRegister(destination) == vector.result);
    REQUIRE(jit.GetRegister(3) == 0x123456);
    REQUIRE(jit.GetSP() == vector.modifier);
    REQUIRE(jit.GetPstate() == 0xa0000000);
    if (enabled) REQUIRE(jit.GetPC() == 4);
}
