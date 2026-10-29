// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include "common/common_types.h"

namespace Dynarmic::Common::Crypto {
namespace Qarma {
constexpr std::array<u8, 16> Shuffle{13, 6, 11, 0, 7, 12, 1, 10, 8, 3, 14, 5, 2, 9, 4, 15};
constexpr std::array<u8, 16> Substitution{11, 6, 8, 15, 12, 0, 9, 14, 3, 7, 4, 5, 13, 2, 1, 10};
constexpr std::array<u8, 16> Tweak{4, 5, 6, 7, 11, 2, 3, 8, 12, 13, 14, 15, 0, 1, 10, 9};
constexpr u16 TweakRotations = 0xd894;
constexpr std::array<u64, 5> Constants{0, 0x13198a2e03707344ULL, 0xa4093822299f31d0ULL,
                                      0x082efa98ec4e6c89ULL, 0x452821e638d01377ULL};
constexpr auto Inverse(const std::array<u8, 16>& table) {
    std::array<u8, 16> result{};
    for (u8 i = 0; i < 16; ++i) result[table[i]] = i;
    return result;
}
constexpr u64 Permute(u64 value, const std::array<u8, 16>& table) {
    u64 result{};
    for (u32 i = 0; i < 16; ++i) result |= ((value >> (4 * table[i])) & 15) << (4 * i);
    return result;
}
constexpr u64 Substitute(u64 value, bool inverse) {
    const auto& table = inverse ? Inverse(Substitution) : Substitution;
    u64 result{};
    for (u32 i = 0; i < 16; ++i) result |= u64{table[(value >> (4 * i)) & 15]} << (4 * i);
    return result;
}
constexpr u64 Mix(u64 value) {
    const auto rotate = [](u64 nibble, u32 shift) {
        return ((nibble << shift) | (nibble >> (4 - shift))) & 15;
    };
    u64 result{};
    for (u32 i = 0; i < 4; ++i) {
        std::array<u64, 4> column{};
        for (u32 row = 0; row < 4; ++row) column[row] = (value >> (4 * (i + 4 * row))) & 15;
        for (u32 row = 0; row < 4; ++row) {
            const u64 mixed = rotate(column[(row + 1) % 4], 1) ^
                              rotate(column[(row + 2) % 4], 2) ^
                              rotate(column[(row + 3) % 4], 1);
            result |= mixed << (4 * (i + 4 * row));
        }
    }
    return result;
}
constexpr u64 AdvanceTweak(u64 value) {
    value = Permute(value, Tweak);
    u64 result{};
    for (u32 i = 0; i < 16; ++i) {
        u64 cell = (value >> (4 * i)) & 15;
        if (TweakRotations & (1U << i)) cell = (cell >> 1) | (((cell ^ (cell >> 1)) & 1) << 3);
        result |= cell << (4 * i);
    }
    return result;
}
} // namespace Qarma

constexpr u64 ComputeQarma5(u64 data, u64 modifier, u64 key_high, u64 key_low) {
    const u64 whitening = (key_high << 63) | ((key_high >> 1) ^ (key_high >> 63));
    std::array<u64, 6> tweaks{modifier};
    for (std::size_t i = 1; i < tweaks.size(); ++i) tweaks[i] = Qarma::AdvanceTweak(tweaks[i - 1]);
    u64 state = data ^ key_high;
    for (std::size_t i = 0; i < 5; ++i) {
        state ^= key_low ^ tweaks[i] ^ Qarma::Constants[i];
        if (i != 0) state = Qarma::Mix(Qarma::Permute(state, Qarma::Shuffle));
        state = Qarma::Substitute(state, false);
    }
    state ^= whitening ^ tweaks[5];
    state = Qarma::Substitute(Qarma::Mix(Qarma::Permute(state, Qarma::Shuffle)), false);
    state = Qarma::Mix(Qarma::Permute(state, Qarma::Shuffle)) ^ key_low;
    state = Qarma::Permute(state, Qarma::Inverse(Qarma::Shuffle));
    state = Qarma::Permute(Qarma::Mix(Qarma::Substitute(state, true)), Qarma::Inverse(Qarma::Shuffle));
    state ^= key_high ^ tweaks[5];
    for (std::size_t i = 0; i < 5; ++i) {
        state = Qarma::Substitute(state, true);
        if (i != 4) state = Qarma::Permute(Qarma::Mix(state), Qarma::Inverse(Qarma::Shuffle));
        state ^= Qarma::Constants[4 - i] ^ key_low ^ tweaks[4 - i] ^ 0xc0ac29b7c97c50ddULL;
    }
    return state ^ whitening;
}

inline u64 ComputePACGA(u64 data, u64 modifier) {
    // Generic authentication remains active with disabled address keys; the reset key is zero.
    return ComputeQarma5(data, modifier, 0, 0) & 0xffffffff00000000ULL;
}
} // namespace Dynarmic::Common::Crypto
