// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>
#include "powah_emit.hpp"

TEST_CASE("ppc64: addi", "[ppc64]") {
    std::vector<uint32_t> data(64);
    powah::Context ctx(data.data(), data.size());

    ctx.ADDI(powah::R2, powah::R2, 0);
    ctx.ADDIS(powah::R2, powah::R12, 0);
    REQUIRE(data[0] == 0x00004238);
    REQUIRE(data[1] == 0x00004c3c);
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
    REQUIRE(data[0] == 0x900041fb);
    REQUIRE(data[1] == 0x78239a7c);
    REQUIRE(data[2] == 0xb000c1fb);
    REQUIRE(data[3] == 0x781b7e7c);
    REQUIRE(data[4] == 0x84c9a478);
    REQUIRE(data[5] == 0xa00081fb);
}

TEST_CASE("ppc64: sldi", "[ppc64]") {
    std::vector<uint32_t> data(64);
    powah::Context ctx(data.data(), data.size());

    ctx.SLDI(powah::R3, powah::R5, 37);
    REQUIRE(data[0] == 0x862ea378);
}

TEST_CASE("ppc64: rldicr", "[ppc64]") {
    std::vector<uint32_t> data(64);
    powah::Context ctx(data.data(), data.size());
    ctx.RLDICR(powah::R1, powah::R1, 0, 0);
    ctx.RLDICR(powah::R1, powah::R1, 1, 0);
    ctx.RLDICR(powah::R1, powah::R1, 0, 1);
    ctx.RLDICR(powah::R1, powah::R1, 1, 1);
    ctx.RLDICR(powah::R1, powah::R1, 31, 31);
    REQUIRE(data[0] == 0x04002178);
    REQUIRE(data[1] == 0x04082178);
    REQUIRE(data[2] == 0x44002178);
    REQUIRE(data[3] == 0x44082178);
    REQUIRE(data[4] == 0xc4ff2178);
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
    REQUIRE(data[0] == 0x783bfb7c);
    REQUIRE(data[1] == 0x7833dd7c);
    REQUIRE(data[2] == 0x7823637c);
    REQUIRE(data[3] == 0x78e3847f);
    REQUIRE(data[4] == 0x820b014d);
    REQUIRE(data[5] == 0x782bb97c);
    REQUIRE(data[6] == 0x78d3637c);
    REQUIRE(data[7] == 0x380061f8);
    REQUIRE(data[8] == 0x78f3c37f);
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
    REQUIRE(data[0] == 0x480081e8);
    REQUIRE(data[1] == 0x5000a180);
    REQUIRE(data[2] == 0x024a8a4e);
    REQUIRE(data[3] == 0x0400c438);
    REQUIRE(data[4] == 0xc807a474);
    REQUIRE(data[5] == 0xc007bee8);
    REQUIRE(data[6] == 0x54006188);
    REQUIRE(data[7] == 0x0002da78);
    REQUIRE(data[8] == 0x50008190);
    REQUIRE(data[9] == 0x0100a538);
    REQUIRE(data[10] == 0x480041fb);
    REQUIRE(data[11] == 0xc007bef8);
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
    REQUIRE(data[0] == 0x00806578);
    REQUIRE(data[1] == 0x00406478);
    REQUIRE(data[2] == 0x2c44a478);
    REQUIRE(data[3] == 0x00c06578);
    REQUIRE(data[4] == 0x2c82a478);
    REQUIRE(data[5] == 0x02006578);
    REQUIRE(data[6] == 0x2cc0a478);
    REQUIRE(data[7] == 0x02806578);
    REQUIRE(data[8] == 0x0e44a478);
    REQUIRE(data[9] == 0x02c06578);
    REQUIRE(data[10] == 0x0e82a478);
    REQUIRE(data[11] == 0x0ec06478);
    REQUIRE(data[12] == 0x7823837c);
}
