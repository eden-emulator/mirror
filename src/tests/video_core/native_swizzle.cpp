// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <bit>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "video_core/textures/decoders.h"

namespace {
u64 Digest(const std::vector<u8>& bytes) {
    u64 digest = 14695981039346656037ULL;
    for (const u8 byte : bytes) digest = (digest ^ byte) * 1099511628211ULL;
    return digest;
}
}

TEST_CASE("Native Orin texture mips match independent upstream swizzle vectors", "[native-swizzle]") {
    struct Fixture { u32 width, height, levels, bytes; std::array<u64, 4> hashes; };
    const std::array fixtures{
        Fixture{32, 64, 2, 8, {0x9fcd630e81008de4ULL, 0x0b316aa26800970aULL}},
        Fixture{128, 256, 4, 16, {0x1d16743f55444bd6ULL, 0x628ec170d3dc24b6ULL, 0x9d5ce8671743eadcULL, 0x68c86243a7167475ULL}},
        Fixture{128, 128, 3, 8, {0xa3694bb8d6333e7eULL, 0xe748d5f21bd06d5dULL, 0xabfa06266de4d322ULL}},
        Fixture{256, 64, 2, 16, {0x9820bfdf5000d898ULL, 0x2177c49e446a9450ULL}},
        Fixture{64, 256, 4, 16, {0x30e15cf8e750e118ULL, 0x286e806b76ac89e4ULL, 0xa0dc28aebfbe0862ULL, 0xdca4a8b9b78cafc2ULL}},
    };
    for (const auto& fixture : fixtures) {
        u32 state = 0x12345678;
        for (u32 level = 0; level < fixture.levels; ++level) {
            const u32 width = (std::max(1U, fixture.width >> level) + 3) / 4;
            const u32 height = (std::max(1U, fixture.height >> level) + 3) / 4;
            u32 block_height = 16;
            while (block_height > 1 && height <= block_height * 4) block_height >>= 1;
            const u32 block_log2 = static_cast<u32>(std::countr_zero(block_height));
            const size_t size = Tegra::Texture::CalculateSize(true, fixture.bytes, width, height, 1, block_log2, 0);
            std::vector<u8> tiled(size);
            for (auto& byte : tiled) { state = state * 1664525U + 1013904223U; byte = static_cast<u8>(state >> 24); }
            std::vector<u8> linear(width * height * fixture.bytes);
            Tegra::Texture::UnswizzleTexture(linear, tiled, fixture.bytes, width, height, 1, block_log2, 0, 1, true);
            INFO("width=" << width << " height=" << height << " mip=" << level);
            REQUIRE(Digest(linear) == fixture.hashes[level]);
            auto restored = tiled;
            Tegra::Texture::SwizzleTexture(restored, linear, fixture.bytes, width, height, 1, block_log2, 0, 1, true);
            REQUIRE(restored == tiled);
        }
    }
}

TEST_CASE("Native and legacy GOB byte lanes remain distinct", "[native-swizzle]") {
    std::vector<u8> tiled(512);
    for (u32 i = 0; i < tiled.size(); ++i) tiled[i] = static_cast<u8>(i / 16);
    std::vector<u8> native(512), legacy(512);
    Tegra::Texture::UnswizzleTexture(native, tiled, 1, 64, 8, 1, 0, 0, 1, true);
    Tegra::Texture::UnswizzleTexture(legacy, tiled, 1, 64, 8, 1, 0, 0);
    REQUIRE(native[16] == 4);
    REQUIRE(legacy[16] == 2);
    REQUIRE(native[2 * 64] == 2);
    REQUIRE(legacy[2 * 64] == 4);
    REQUIRE(native != legacy);
    std::vector<u8> crop(16 * 3);
    Tegra::Texture::UnswizzleSubrect(crop, tiled, 1, 64, 8, 1, 16, 2, 16, 3, 0, 0, 16, true);
    for (u32 y = 0; y < 3; ++y) {
        REQUIRE(std::equal(crop.begin() + y * 16, crop.begin() + (y + 1) * 16,
                           native.begin() + (y + 2) * 64 + 16));
    }
}
