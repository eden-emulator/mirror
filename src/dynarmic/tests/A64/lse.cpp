// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "dynarmic/tests/A64/testenv.h"
#include "dynarmic/interface/exclusive_monitor.h"

namespace {
class AtomicTestEnv final : public A64TestEnv {
public:
    Dynarmic::ExclusiveMonitor monitor{1};
    bool fail_once{};
    std::optional<u64> failure_value;
    unsigned writes{};

    bool MemoryWriteExclusive8(u64 address, u8 value, u8 expected) override {
        return WriteSmallExclusive(address, value, expected, 1);
    }

    bool MemoryWriteExclusive16(u64 address, u16 value, u16 expected) override {
        return WriteSmallExclusive(address, value, expected, 2);
    }

    bool WriteSmallExclusive(u64 address, u64 value, u64 expected, size_t bytes) {
        ++writes;
        if (fail_once) {
            fail_once = false;
            if (failure_value) {
                MemoryWrite(address, *failure_value, bytes);
            }
            return false;
        }
        if (MemoryRead(address, bytes) != expected) {
            return false;
        }
        MemoryWrite(address, value, bytes);
        return true;
    }

    bool MemoryWriteExclusive32(u64 address, u32 value, u32 expected) override {
        ++writes;
        if (fail_once) {
            fail_once = false;
            if (failure_value) {
                MemoryWrite(address, *failure_value, 4);
            }
            return false;
        }
        if (MemoryRead(address, 4) != expected) {
            return false;
        }
        MemoryWrite(address, value, 4);
        return true;
    }

    bool MemoryWriteExclusive64(u64 address, u64 value, u64 expected) override {
        ++writes;
        if (fail_once) {
            fail_once = false;
            if (failure_value) {
                MemoryWrite(address, *failure_value, 8);
            }
            return false;
        }
        if (MemoryRead(address, 8) != expected) {
            return false;
        }
        MemoryWrite(address, value, 8);
        return true;
    }
};

u32 CasInstruction(bool wide, bool acquire, bool release, u32 rs, u32 rn, u32 rt) {
    return 0x88A07C00U | (u32(wide) << 30) | (u32(acquire) << 22) |
           (rs << 16) | (u32(release) << 15) | (rn << 5) | rt;
}
} // namespace

TEST_CASE("A64: CAS variants preserve flags and return the observed value", "[a64][lse]") {
    const bool wide = GENERATE(false, true);
    const bool acquire = GENERATE(false, true);
    const bool release = GENERATE(false, true);
    const bool match = GENERATE(false, true);
    const bool retry = GENERATE(false, true);
    const size_t bytes = wide ? 8 : 4;
    const u64 observed = wide ? 0x1122334455667788ULL : 0x55667788ULL;
    const u64 desired = wide ? 0x8877665544332211ULL : 0x44332211ULL;
    AtomicTestEnv env;
    env.code_mem = {CasInstruction(wide, acquire, release, 1, 0, 2), 0x14000000};
    env.MemoryWrite(0x1000, observed, bytes);
    env.fail_once = retry;
    Dynarmic::A64::UserConfig config;
    config.callbacks = &env;
    config.global_monitor = &env.monitor;
    config.code_cache_size = 16 * 1024 * 1024;
    Dynarmic::A64::Jit jit{config};
    jit.SetRegister(0, 0x1000);
    jit.SetRegister(1, match ? observed : observed ^ 1);
    jit.SetRegister(2, desired);
    jit.SetPstate(0xA0000000);
    env.ticks_left = 4;
    jit.Run();
    REQUIRE(jit.GetPC() == 4);
    REQUIRE(jit.GetRegister(1) == observed);
    REQUIRE(jit.GetRegister(0) == 0x1000);
    REQUIRE(jit.GetRegister(2) == desired);
    REQUIRE(jit.GetPstate() == 0xA0000000);
    REQUIRE(env.MemoryRead(0x1000, bytes) == (match ? desired : observed));
    REQUIRE(env.writes == (retry ? 2U : 1U));
}

TEST_CASE("A64: CAS handles SP and overlapping source registers", "[a64][lse]") {
    const bool use_sp = GENERATE(false, true);
    const bool overlap = GENERATE(false, true);
    AtomicTestEnv env;
    env.code_mem = {CasInstruction(true, true, true, 1, use_sp ? 31 : 0, overlap ? 1 : 2),
                    0x14000000};
    env.MemoryWrite(0x1000, 42, 8);
    Dynarmic::A64::UserConfig config;
    config.callbacks = &env;
    config.global_monitor = &env.monitor;
    config.code_cache_size = 16 * 1024 * 1024;
    Dynarmic::A64::Jit jit{config};
    jit.SetRegister(0, 0x1000);
    jit.SetSP(0x1000);
    jit.SetRegister(1, 42);
    jit.SetRegister(2, 84);
    env.ticks_left = 2;
    jit.Run();
    REQUIRE(jit.GetRegister(1) == 42);
    REQUIRE(jit.GetSP() == 0x1000);
    REQUIRE(env.MemoryRead(0x1000, 8) == (overlap ? 42 : 84));
}

TEST_CASE("A64: CAS retry preserves the full comparison register", "[a64][lse]") {
    AtomicTestEnv env;
    env.code_mem = {CasInstruction(false, true, false, 1, 0, 2), 0x14000000};
    env.MemoryWrite(0x1000, 0x55667788, 4);
    env.fail_once = true;
    Dynarmic::A64::UserConfig config;
    config.callbacks = &env;
    config.global_monitor = &env.monitor;
    config.code_cache_size = 16 * 1024 * 1024;
    Dynarmic::A64::Jit jit{config};
    jit.SetRegister(0, 0x1000);
    jit.SetRegister(1, 0xFFFFFFFF55667788ULL);
    jit.SetRegister(2, 42);
    jit.SetPstate(0xA0000000);
    env.ticks_left = 2;
    jit.Step();
    REQUIRE(jit.GetPC() == 0);
    REQUIRE(jit.GetRegister(1) == 0xFFFFFFFF55667788ULL);
    REQUIRE(env.MemoryRead(0x1000, 4) == 0x55667788);
    REQUIRE(jit.GetPstate() == 0xA0000000);
    jit.Step();
    REQUIRE(jit.GetPC() == 4);
    REQUIRE(jit.GetRegister(1) == 0x55667788);
    REQUIRE(env.MemoryRead(0x1000, 4) == 42);
    REQUIRE(jit.GetPstate() == 0xA0000000);
}

TEST_CASE("A64: CAS supports zero-register inputs", "[a64][lse]") {
    const bool wide = GENERATE(false, true);
    const bool zero_expected = GENERATE(false, true);
    const bool zero_desired = GENERATE(false, true);
    AtomicTestEnv env;
    env.code_mem = {CasInstruction(wide, false, true, zero_expected ? 31 : 1, 0,
                                   zero_desired ? 31 : 2), 0x14000000};
    const size_t bytes = wide ? 8 : 4;
    env.MemoryWrite(0x1000, zero_expected ? 0 : 42, bytes);
    Dynarmic::A64::UserConfig config;
    config.callbacks = &env;
    config.global_monitor = &env.monitor;
    config.code_cache_size = 16 * 1024 * 1024;
    Dynarmic::A64::Jit jit{config};
    jit.SetRegister(0, 0x1000);
    jit.SetRegister(1, 42);
    jit.SetRegister(2, 84);
    env.ticks_left = 2;
    jit.Run();
    REQUIRE(env.MemoryRead(0x1000, bytes) == (zero_desired ? 0 : 84));
    REQUIRE(jit.GetRegister(1) == 42);
}

TEST_CASE("A64: LDAPR zero extends all supported widths", "[a64][rcpc]") {
    const u32 size = GENERATE(0U, 1U, 2U, 3U);
    AtomicTestEnv env;
    env.code_mem = {0x38BFC001U | (size << 30), 0x14000000}; // LDAPR{B,H} R1, [X0]
    const size_t bytes = size_t{1} << size;
    const u64 value = 0xFEDCBA9876543210ULL;
    env.MemoryWrite(0x1000, value, 8);
    Dynarmic::A64::UserConfig config;
    config.callbacks = &env;
    config.global_monitor = &env.monitor;
    config.code_cache_size = 16 * 1024 * 1024;
    Dynarmic::A64::Jit jit{config};
    jit.SetRegister(0, 0x1000);
    jit.SetRegister(1, ~u64{0});
    jit.SetPstate(0x60000000);
    env.ticks_left = 2;
    jit.Run();
    const u64 mask = bytes == 8 ? ~u64{0} : (u64{1} << (bytes * 8)) - 1;
    REQUIRE(jit.GetRegister(1) == (value & mask));
    REQUIRE(jit.GetRegister(0) == 0x1000);
    REQUIRE(jit.GetPstate() == 0x60000000);
}

TEST_CASE("A64: CAS retries observe an intervening write", "[a64][lse]") {
    const bool wide = GENERATE(false, true);
    AtomicTestEnv env;
    env.code_mem = {CasInstruction(wide, true, true, 1, 0, 2), 0x14000000};
    env.MemoryWrite(0x1000, 42, wide ? 8 : 4);
    env.fail_once = true;
    env.failure_value = 13;
    Dynarmic::A64::UserConfig config;
    config.callbacks = &env;
    config.global_monitor = &env.monitor;
    config.code_cache_size = 16 * 1024 * 1024;
    Dynarmic::A64::Jit jit{config};
    jit.SetRegister(0, 0x1000);
    jit.SetRegister(1, 42);
    jit.SetRegister(2, 84);
    env.ticks_left = 4;
    jit.Run();
    REQUIRE(jit.GetPC() == 4);
    REQUIRE(jit.GetRegister(1) == 13);
    REQUIRE(env.MemoryRead(0x1000, wide ? 8 : 4) == 13);
    REQUIRE(env.writes == 2);
}

TEST_CASE("A64: LDADD variants handle retries, overlap, and overflow", "[a64][lse]") {
    const bool wide = GENERATE(false, true);
    const bool acquire = GENERATE(false, true);
    const bool release = GENERATE(false, true);
    const bool retry = GENERATE(false, true);
    const bool overlap = GENERATE(false, true);
    const bool wrap = GENERATE(false, true);
    const size_t bytes = wide ? 8 : 4;
    const u64 mask = wide ? ~u64{0} : 0xFFFFFFFFULL;
    const u64 observed = wrap ? mask - 1 : 42;
    const u64 operand = wrap ? 5 : 7;
    const u32 rt = overlap ? 1 : 2;
    AtomicTestEnv env;
    const u32 instruction = 0xB8200000U | (u32(wide) << 30) | (u32(acquire) << 23) |
                            (u32(release) << 22) | (1U << 16) | rt;
    env.code_mem = {instruction, 0x14000000};
    env.MemoryWrite(0x1000, observed, bytes);
    env.fail_once = retry;
    Dynarmic::A64::UserConfig config;
    config.callbacks = &env;
    config.global_monitor = &env.monitor;
    config.code_cache_size = 16 * 1024 * 1024;
    Dynarmic::A64::Jit jit{config};
    jit.SetRegister(0, 0x1000);
    jit.SetRegister(1, operand);
    jit.SetRegister(2, 0xABCD);
    jit.SetPstate(0x60000000);
    env.ticks_left = 4;
    jit.Run();
    REQUIRE(jit.GetPC() == 4);
    REQUIRE(jit.GetRegister(rt) == observed);
    REQUIRE(jit.GetRegister(1) == (overlap ? observed : operand));
    REQUIRE(jit.GetRegister(2) == (overlap ? 0xABCD : observed));
    REQUIRE(jit.GetPstate() == 0x60000000);
    REQUIRE(env.MemoryRead(0x1000, bytes) == ((observed + operand) & mask));
    REQUIRE(env.writes == (retry ? 2U : 1U));
}

TEST_CASE("A64: small CAS truncates operands and preserves adjacent memory", "[a64][lse]") {
    const bool half = GENERATE(false, true);
    const bool acquire = GENERATE(false, true);
    const bool release = GENERATE(false, true);
    const bool match = GENERATE(false, true);
    const bool retry = GENERATE(false, true);
    const size_t bytes = half ? 2 : 1;
    const u64 mask = half ? 0xffff : 0xff;
    const u64 observed = 0x8765 & mask;
    const u64 desired = 0xabcd & mask;
    AtomicTestEnv env;
    env.code_mem = {0x08A07C00U | (u32(half) << 30) | (u32(acquire) << 22) |
                        (1U << 16) | (u32(release) << 15) | 2U,
                    0x14000000};
    env.MemoryWrite(0x1000, 0x1122334455667788ULL, 8);
    env.MemoryWrite(0x1002, observed, bytes);
    const u64 before = env.MemoryRead(0x1000, 8);
    env.fail_once = retry;
    Dynarmic::A64::UserConfig config;
    config.callbacks = &env;
    config.global_monitor = &env.monitor;
    config.code_cache_size = 16 * 1024 * 1024;
    Dynarmic::A64::Jit jit{config};
    jit.SetRegister(0, 0x1002);
    const u64 expected = 0xfedcba9876540000ULL | (match ? observed : observed ^ 1);
    jit.SetRegister(1, expected);
    jit.SetRegister(2, 0x9876543210000000ULL | desired);
    jit.SetPstate(0xA0000000);
    if (retry) {
        env.ticks_left = 1;
        jit.Step();
        REQUIRE(jit.GetPC() == 0);
        REQUIRE(jit.GetRegister(1) == expected);
        REQUIRE(jit.GetPstate() == 0xA0000000);
    }
    env.ticks_left = 4;
    jit.Run();
    REQUIRE(jit.GetPC() == 4);
    REQUIRE(jit.GetRegister(1) == observed);
    REQUIRE(jit.GetRegister(2) == (0x9876543210000000ULL | desired));
    REQUIRE(jit.GetPstate() == 0xA0000000);
    const u64 after = (before & ~(mask << 16)) | ((match ? desired : observed) << 16);
    REQUIRE(env.MemoryRead(0x1000, 8) == after);
    REQUIRE(env.writes == (retry ? 2U : 1U));
}

TEST_CASE("A64: LDSET variants handle retries, overlap, and existing bits", "[a64][lse]") {
    const bool wide = GENERATE(false, true);
    const bool acquire = GENERATE(false, true);
    const bool release = GENERATE(false, true);
    const bool retry = GENERATE(false, true);
    const bool overlap = GENERATE(false, true);
    const bool existing_bits = GENERATE(false, true);
    const size_t bytes = wide ? 8 : 4;
    const u64 mask = wide ? ~u64{0} : 0xFFFFFFFFULL;
    const u64 observed = existing_bits ? mask - 1 : 42;
    const u64 operand = existing_bits ? 5 : 7;
    const u32 rt = overlap ? 1 : 2;
    AtomicTestEnv env;
    const u32 instruction = 0xB8203000U | (u32(wide) << 30) | (u32(acquire) << 23) |
                            (u32(release) << 22) | (1U << 16) | rt;
    env.code_mem = {instruction, 0x14000000};
    env.MemoryWrite(0x1000, observed, bytes);
    env.fail_once = retry;
    Dynarmic::A64::UserConfig config;
    config.callbacks = &env;
    config.global_monitor = &env.monitor;
    config.code_cache_size = 16 * 1024 * 1024;
    Dynarmic::A64::Jit jit{config};
    jit.SetRegister(0, 0x1000);
    jit.SetRegister(1, operand);
    jit.SetRegister(2, 0xABCD);
    jit.SetPstate(0x60000000);
    env.ticks_left = 4;
    jit.Run();
    REQUIRE(jit.GetPC() == 4);
    REQUIRE(jit.GetRegister(rt) == observed);
    REQUIRE(jit.GetRegister(1) == (overlap ? observed : operand));
    REQUIRE(jit.GetRegister(2) == (overlap ? 0xABCD : observed));
    REQUIRE(jit.GetPstate() == 0x60000000);
    REQUIRE(env.MemoryRead(0x1000, bytes) == ((observed | operand) & mask));
    REQUIRE(env.writes == (retry ? 2U : 1U));
}


TEST_CASE("A64: observed LDSETL retries with a destination that is also its address", "[a64][lse]") {
    const bool interference = GENERATE(false, true);
    AtomicTestEnv env;
    env.code_mem = {0xF8733108, 0x14000000};
    env.MemoryWrite(0x1000, 2, 8);
    env.fail_once = interference;
    env.failure_value = 0x10;
    Dynarmic::A64::UserConfig config;
    config.callbacks = &env;
    config.global_monitor = &env.monitor;
    config.code_cache_size = 16 * 1024 * 1024;
    Dynarmic::A64::Jit jit{config};
    jit.SetRegister(8, 0x1000);
    jit.SetRegister(19, 8);
    jit.SetPstate(0xA0000000);
    if (interference) {
        env.ticks_left = 1;
        jit.Step();
        REQUIRE(jit.GetRegister(8) == 0x1000);
        REQUIRE(jit.GetPC() == 0);
        REQUIRE(jit.GetPstate() == 0xA0000000);
    }
    env.ticks_left = 4;
    jit.Run();
    REQUIRE(jit.GetPC() == 4);
    REQUIRE(jit.GetRegister(8) == (interference ? 0x10 : 2));
    REQUIRE(jit.GetRegister(19) == 8);
    REQUIRE(env.MemoryRead(0x1000, 8) == (interference ? 0x18 : 0xa));
    REQUIRE(jit.GetPstate() == 0xA0000000);
}

TEST_CASE("A64: SWP variants handle retries and overlapping source registers", "[a64][lse]") {
    const unsigned size = GENERATE(0U, 1U, 2U, 3U);
    const bool acquire = GENERATE(false, true);
    const bool release = GENERATE(false, true);
    const bool retry = GENERATE(false, true);
    const bool overlap = GENERATE(false, true);
    const bool existing_bits = GENERATE(false, true);
    const size_t bytes = size_t{1} << size;
    const u64 mask = size == 3 ? ~u64{0} : (u64{1} << (8 * bytes)) - 1;
    const u64 observed = existing_bits ? mask - 1 : 42;
    const u64 operand = existing_bits ? 5 : 7;
    const u32 rt = overlap ? 1 : 2;
    AtomicTestEnv env;
    const u32 instruction = 0x38208000U | (size << 30) | (u32(acquire) << 23) |
                            (u32(release) << 22) | (1U << 16) | rt;
    env.code_mem = {instruction, 0x14000000};
    env.MemoryWrite(0x1000, observed, bytes);
    env.fail_once = retry;
    env.MemoryWrite(0x1000 + bytes, 0xA5, 1);
    Dynarmic::A64::UserConfig config;
    config.callbacks = &env;
    config.global_monitor = &env.monitor;
    config.code_cache_size = 16 * 1024 * 1024;
    Dynarmic::A64::Jit jit{config};
    jit.SetRegister(0, 0x1000);
    jit.SetRegister(1, operand);
    jit.SetRegister(2, 0xABCD);
    jit.SetPstate(0x60000000);
    env.ticks_left = 4;
    jit.Run();
    REQUIRE(jit.GetPC() == 4);
    REQUIRE(jit.GetRegister(rt) == observed);
    REQUIRE(jit.GetRegister(1) == (overlap ? observed : operand));
    REQUIRE(jit.GetRegister(2) == (overlap ? 0xABCD : observed));
    REQUIRE(jit.GetPstate() == 0x60000000);
    REQUIRE(env.MemoryRead(0x1000, bytes) == (operand & mask));
    REQUIRE(env.writes == (retry ? 2U : 1U));
    REQUIRE(env.MemoryRead(0x1000 + bytes, 1) == 0xA5);
}


TEST_CASE("A64: observed SWPAL retries with a destination that is also its address", "[a64][lse]") {
    const bool interference = GENERATE(false, true);
    AtomicTestEnv env;
    env.code_mem = {0xB8EA8129, 0x14000000};
    env.MemoryWrite(0x1000, 2, 4);
    env.fail_once = interference;
    env.failure_value = 0x10;
    Dynarmic::A64::UserConfig config;
    config.callbacks = &env;
    config.global_monitor = &env.monitor;
    config.code_cache_size = 16 * 1024 * 1024;
    Dynarmic::A64::Jit jit{config};
    jit.SetRegister(9, 0x1000);
    jit.SetRegister(10, 8);
    jit.SetPstate(0xA0000000);
    if (interference) {
        env.ticks_left = 1;
        jit.Step();
        REQUIRE(jit.GetRegister(9) == 0x1000);
        REQUIRE(jit.GetPC() == 0);
        REQUIRE(jit.GetPstate() == 0xA0000000);
    }
    env.ticks_left = 4;
    jit.Run();
    REQUIRE(jit.GetPC() == 4);
    REQUIRE(jit.GetRegister(9) == (interference ? 0x10 : 2));
    REQUIRE(jit.GetRegister(10) == 8);
    REQUIRE(env.MemoryRead(0x1000, 4) == 8);
    REQUIRE(jit.GetPstate() == 0xA0000000);
}

TEST_CASE("A64: LDCLR variants handle retries, overlap, and existing bits", "[a64][lse]") {
    const bool wide = GENERATE(false, true);
    const bool acquire = GENERATE(false, true);
    const bool release = GENERATE(false, true);
    const bool retry = GENERATE(false, true);
    const bool overlap = GENERATE(false, true);
    const bool existing_bits = GENERATE(false, true);
    const size_t bytes = wide ? 8 : 4;
    const u64 mask = wide ? ~u64{0} : 0xFFFFFFFFULL;
    const u64 observed = existing_bits ? mask - 1 : 42;
    const u64 operand = existing_bits ? 5 : 7;
    const u32 rt = overlap ? 1 : 2;
    AtomicTestEnv env;
    const u32 instruction = 0xB8201000U | (u32(wide) << 30) | (u32(acquire) << 23) |
                            (u32(release) << 22) | (1U << 16) | rt;
    env.code_mem = {instruction, 0x14000000};
    env.MemoryWrite(0x1000, observed, bytes);
    env.fail_once = retry;
    Dynarmic::A64::UserConfig config;
    config.callbacks = &env;
    config.global_monitor = &env.monitor;
    config.code_cache_size = 16 * 1024 * 1024;
    Dynarmic::A64::Jit jit{config};
    jit.SetRegister(0, 0x1000);
    jit.SetRegister(1, operand);
    jit.SetRegister(2, 0xABCD);
    jit.SetPstate(0x60000000);
    env.ticks_left = 4;
    jit.Run();
    REQUIRE(jit.GetPC() == 4);
    REQUIRE(jit.GetRegister(rt) == observed);
    REQUIRE(jit.GetRegister(1) == (overlap ? observed : operand));
    REQUIRE(jit.GetRegister(2) == (overlap ? 0xABCD : observed));
    REQUIRE(jit.GetPstate() == 0x60000000);
    REQUIRE(env.MemoryRead(0x1000, bytes) == ((observed & ~operand) & mask));
    REQUIRE(env.writes == (retry ? 2U : 1U));
}


TEST_CASE("A64: observed LDCLRL retries with a destination that is also its address", "[a64][lse]") {
    const bool interference = GENERATE(false, true);
    AtomicTestEnv env;
    env.code_mem = {0xF8691108, 0x14000000};
    env.MemoryWrite(0x1000, 0xf, 8);
    env.fail_once = interference;
    env.failure_value = 0x10;
    Dynarmic::A64::UserConfig config;
    config.callbacks = &env;
    config.global_monitor = &env.monitor;
    config.code_cache_size = 16 * 1024 * 1024;
    Dynarmic::A64::Jit jit{config};
    jit.SetRegister(8, 0x1000);
    jit.SetRegister(9, 1);
    jit.SetPstate(0xA0000000);
    if (interference) {
        env.ticks_left = 1;
        jit.Step();
        REQUIRE(jit.GetRegister(8) == 0x1000);
        REQUIRE(jit.GetPC() == 0);
        REQUIRE(jit.GetPstate() == 0xA0000000);
    }
    env.ticks_left = 4;
    jit.Run();
    REQUIRE(jit.GetPC() == 4);
    REQUIRE(jit.GetRegister(8) == (interference ? 0x10 : 0xf));
    REQUIRE(jit.GetRegister(9) == 1);
    REQUIRE(env.MemoryRead(0x1000, 8) == (interference ? 0x10 : 0xe));
    REQUIRE(jit.GetPstate() == 0xA0000000);
}

