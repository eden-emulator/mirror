// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>
#include <sys/mman.h>
#include "powah_emit.hpp"

#define EB32(X) __bswap32(X)

TEST_CASE("ppc64: addi", "[ppc64]") {
    std::vector<uint32_t> data(64);
    powah::Context ctx(data.data(), data.size());

    ctx.ADDI(powah::R2, powah::R2, 0);
    ctx.ADDIS(powah::R2, powah::R12, 0);
    REQUIRE(data[0] == EB32(0x00004238));
    REQUIRE(data[1] == EB32(0x00004c3c));
}

TEST_CASE("ppc64: std/mr", "[ppc64]") {
    std::vector<uint32_t> data(64);
    powah::Context ctx(data.data(), data.size());

    ctx.STD(powah::R26, powah::R1, 144);
    ctx.MR(powah::R26, powah::R4);
    ctx.STD(powah::R30, powah::R1, 176);
    ctx.MR(powah::R30, powah::R3);
    ctx.RLDICR(powah::R4, powah::R5, 25, 6);
    ctx.STD(powah::R28, powah::R1, 160);
    REQUIRE(data[0] == EB32(0x900041fb));
    REQUIRE(data[1] == EB32(0x78239a7c));
    REQUIRE(data[2] == EB32(0xb000c1fb));
    REQUIRE(data[3] == EB32(0x781b7e7c));
    REQUIRE(data[4] == EB32(0x84c9a478));
    REQUIRE(data[5] == EB32(0xa00081fb));
}

TEST_CASE("ppc64: sldi", "[ppc64]") {
    std::vector<uint32_t> data(64);
    powah::Context ctx(data.data(), data.size());

    ctx.SLDI(powah::R3, powah::R5, 37);
    REQUIRE(data[0] == EB32(0x862ea378));
}

TEST_CASE("ppc64: rldicr", "[ppc64]") {
    std::vector<uint32_t> data(64);
    powah::Context ctx(data.data(), data.size());
    ctx.RLDICR(powah::R1, powah::R1, 0, 0);
    ctx.RLDICR(powah::R1, powah::R1, 1, 0);
    ctx.RLDICR(powah::R1, powah::R1, 0, 1);
    ctx.RLDICR(powah::R1, powah::R1, 1, 1);
    ctx.RLDICR(powah::R1, powah::R1, 31, 31);
    REQUIRE(data[0] == EB32(0x04002178));
    REQUIRE(data[1] == EB32(0x04082178));
    REQUIRE(data[2] == EB32(0x44002178));
    REQUIRE(data[3] == EB32(0x44082178));
    REQUIRE(data[4] == EB32(0xc4ff2178));
}

TEST_CASE("ppc64: mr/or/std", "[ppc64]") {
    std::vector<uint32_t> data(64);
    powah::Context ctx(data.data(), data.size());
    ctx.MR(powah::R27, powah::R7);
    ctx.MR(powah::R29, powah::R6);
    ctx.OR(powah::R3, powah::R3, powah::R4);
    ctx.MR(powah::R4, powah::R28);
    ctx.CRMOVE(powah::CPR{8}, powah::CPR{1});
    ctx.MR(powah::R25, powah::R5);
    ctx.OR(powah::R3, powah::R3, powah::R26);
    ctx.STD(powah::R3, powah::R1, 56);
    ctx.MR(powah::R3, powah::R30);
    REQUIRE(data[0] == EB32(0x783bfb7c));
    REQUIRE(data[1] == EB32(0x7833dd7c));
    REQUIRE(data[2] == EB32(0x7823637c));
    REQUIRE(data[3] == EB32(0x78e3847f));
    REQUIRE(data[4] == EB32(0x820b014d));
    REQUIRE(data[5] == EB32(0x782bb97c));
    REQUIRE(data[6] == EB32(0x78d3637c));
    REQUIRE(data[7] == EB32(0x380061f8));
    REQUIRE(data[8] == EB32(0x78f3c37f));
}

TEST_CASE("ppc64: ld/crand+addi", "[ppc64]") {
    std::vector<uint32_t> data(64);
    powah::Context ctx(data.data(), data.size());
    ctx.LD(powah::R4, powah::R1, 72);
    ctx.LWZ(powah::R5, powah::R1, 80);
    ctx.CRAND(powah::CPR{20}, powah::CPR{10}, powah::CPR{9});
    ctx.ADDI(powah::R6, powah::R4, 4);
    ctx.ANDIS_(powah::R4, powah::R5, 1992);
    ctx.LD(powah::R5, powah::R30, 1984);
    ctx.LBZ(powah::R3, powah::R1, 84);
    ctx.CLRLDI(powah::R26, powah::R6, 8);
    ctx.STW(powah::R4, powah::R1, 80);
    ctx.ADDI(powah::R5, powah::R5, 1);
    ctx.STD(powah::R26, powah::R1, 72);
    ctx.STD(powah::R5, powah::R30, 1984);
    REQUIRE(data[0] == EB32(0x480081e8));
    REQUIRE(data[1] == EB32(0x5000a180));
    REQUIRE(data[2] == EB32(0x024a8a4e));
    REQUIRE(data[3] == EB32(0x0400c438));
    REQUIRE(data[4] == EB32(0xc807a474));
    REQUIRE(data[5] == EB32(0xc007bee8));
    REQUIRE(data[6] == EB32(0x54006188));
    REQUIRE(data[7] == EB32(0x0002da78));
    REQUIRE(data[8] == EB32(0x50008190));
    REQUIRE(data[9] == EB32(0x0100a538));
    REQUIRE(data[10] == EB32(0x480041fb));
    REQUIRE(data[11] == EB32(0xc007bef8));
}

TEST_CASE("ppc64: rotldi", "[ppc64]") {
    std::vector<uint32_t> data(64);
    powah::Context ctx(data.data(), data.size());
    ctx.ROTLDI(powah::R5, powah::R3, 16);
    ctx.ROTLDI(powah::R4, powah::R3, 8);
    ctx.RLDIMI(powah::R4, powah::R5, 8, 48);
    ctx.ROTLDI(powah::R5, powah::R3, 24);
    ctx.RLDIMI(powah::R4, powah::R5, 16, 40);
    ctx.ROTLDI(powah::R5, powah::R3, 32);
    ctx.RLDIMI(powah::R4, powah::R5, 24, 32);
    ctx.ROTLDI(powah::R5, powah::R3, 48);
    ctx.RLDIMI(powah::R4, powah::R5, 40, 16);
    ctx.ROTLDI(powah::R5, powah::R3, 56);
    ctx.RLDIMI(powah::R4, powah::R5, 48, 8);
    ctx.RLDIMI(powah::R4, powah::R3, 56, 0);
    ctx.MR(powah::R3, powah::R4);
    REQUIRE(data[0] == EB32(0x00806578));
    REQUIRE(data[1] == EB32(0x00406478));
    REQUIRE(data[2] == EB32(0x2c44a478));
    REQUIRE(data[3] == EB32(0x00c06578));
    REQUIRE(data[4] == EB32(0x2c82a478));
    REQUIRE(data[5] == EB32(0x02006578));
    REQUIRE(data[6] == EB32(0x2cc0a478));
    REQUIRE(data[7] == EB32(0x02806578));
    REQUIRE(data[8] == EB32(0x0e44a478));
    REQUIRE(data[9] == EB32(0x02c06578));
    REQUIRE(data[10] == EB32(0x0e82a478));
    REQUIRE(data[11] == EB32(0x0ec06478));
    REQUIRE(data[12] == EB32(0x7823837c));
}

TEST_CASE("ppc64: branch-backwards", "[ppc64]") {
    std::vector<uint32_t> data(64);
    powah::Context ctx(data.data(), data.size());
    powah::Label l_3 = ctx.DefineLabel();
    ctx.LABEL(l_3);
    ctx.ADD(powah::R1, powah::R2, powah::R3);
    ctx.B(l_3);
    ctx.ApplyRelocs();
    REQUIRE(data[0] == EB32(0x141a227c));
    REQUIRE(data[1] == EB32(0xfcffff4b));
}

#if defined(ARCHITECTURE_ppc64)
/*
    0: d619637c  	mullw 3, 3, 3
    4: 20006378  	clrldi	3, 3, 32
    8: 2000804e  	blr
*/
TEST_CASE("ppc64: live-exec square", "[ppc64]") {
    uint32_t* data = (uint32_t*)mmap(nullptr, 4096, PROT_WRITE | PROT_READ | PROT_EXEC, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    REQUIRE(data != nullptr);

    powah::Context ctx((void*)data, 4096);
    ctx.MULLW(powah::R3, powah::R3, powah::R3);
    ctx.CLRLDI(powah::R3, powah::R3, 32);
    ctx.BLR();
    REQUIRE(data[0] == EB32(0xd619637c));
    REQUIRE(data[1] == EB32(0x20006378));
    REQUIRE(data[2] == EB32(0x2000804e));

    auto* fn = (int (*)(int))data;
    REQUIRE(fn(4) == 4 * 4);
    REQUIRE(fn(8) == 8 * 8);

    munmap((void*)data, 4096);
}

/*
int f(int a, int b, int c) {
    a += (a > b) ? b - 0xffff : c + 402, b += 2, c &= b*c;
    return a * a + b ^ c & b;
}
*/
TEST_CASE("ppc64: live-exec xoralu", "[ppc64]") {
    uint32_t* data = (uint32_t*)mmap(nullptr, 4096, PROT_WRITE | PROT_READ | PROT_EXEC, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    REQUIRE(data != nullptr);

    powah::Context ctx((void*)data, 4096);
    powah::Label l_2 = ctx.DefineLabel();
    powah::Label l_3 = ctx.DefineLabel();
    ctx.CMPW(powah::R3, powah::R4);
    ctx.BGT(powah::CR0, l_2);
    ctx.ADDI(powah::R6, powah::R5, 402);
    ctx.B(l_3);
    ctx.LABEL(l_2);
    ctx.ADDI(powah::R6, powah::R4, 1);
    ctx.ADDIS(powah::R6, powah::R6, -1);
    ctx.LABEL(l_3);
    ctx.ADDI(powah::R4, powah::R4, 2);
    ctx.ADD(powah::R3, powah::R6, powah::R3);
    ctx.MULLW(powah::R6, powah::R4, powah::R5);
    ctx.MULLW(powah::R3, powah::R3, powah::R3);
    ctx.AND(powah::R5, powah::R5, powah::R6);
    ctx.ADD(powah::R3, powah::R3, powah::R4);
    ctx.AND(powah::R4, powah::R5, powah::R4);
    ctx.XOR(powah::R3, powah::R3, powah::R4);
    ctx.EXTSW(powah::R3, powah::R3);
    ctx.BLR();
    ctx.ApplyRelocs();
    REQUIRE(data[0] == EB32(0x0020037c));
    REQUIRE(data[1] == EB32(0x0c008141));
    REQUIRE(data[2] == EB32(0x9201c538));
    REQUIRE(data[3] == EB32(0x0c000048));
    REQUIRE(data[4] == EB32(0x0100c438));
    REQUIRE(data[5] == EB32(0xffffc63c));
    REQUIRE(data[6] == EB32(0x02008438));
    REQUIRE(data[7] == EB32(0x141a667c));
    REQUIRE(data[8] == EB32(0xd629c47c));
    REQUIRE(data[9] == EB32(0xd619637c));
    REQUIRE(data[10] == EB32(0x3830a57c));
    REQUIRE(data[11] == EB32(0x1422637c));
    REQUIRE(data[12] == EB32(0x3820a47c));
    REQUIRE(data[13] == EB32(0x7822637c));
    REQUIRE(data[14] == EB32(0xb407637c));
    REQUIRE(data[15] == EB32(0x2000804e));

    auto const orig = [](int a, int b, int c) {
        a += (a > b) ? b - 0xffff : c + 402, b += 2, c &= b*c;
        return a * a + b ^ c & b;
    };
    auto* fn = (int (*)(int, int, int))data;
    REQUIRE(fn(0, 1, 2) == orig(0, 1, 2));
    REQUIRE(fn(6456, 4564, 4564561) == orig(6456, 4564, 4564561));

    munmap((void*)data, 4096);
}
#endif
