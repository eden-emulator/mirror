// SPDX-License-Identifier: GPL-3.0-or-later

#include <limits>
#include <catch2/catch_test_macros.hpp>
#include "common/literals.h"
#include "core/hle/kernel/k_address_space_info.h"

using namespace Common::Literals;
using Kernel::KAddressSpaceInfo;

TEST_CASE("39-bit heap and alias sizes scale with physical RAM", "[address-space]") {
    using Type = KAddressSpaceInfo::Type;
    for (const size_t ram : {0_GiB, 4_GiB, 6_GiB, 8_GiB}) {
        REQUIRE(KAddressSpaceInfo::GetAddressSpaceSize(39, Type::Heap, ram) == 8_GiB);
        REQUIRE(KAddressSpaceInfo::GetAddressSpaceSize(39, Type::Alias, ram) == 64_GiB);
    }
    for (const size_t ram : {10_GiB, 12_GiB, 16_GiB, 32_GiB}) {
        REQUIRE(KAddressSpaceInfo::GetAddressSpaceSize(39, Type::Heap, ram) == ram);
        REQUIRE(KAddressSpaceInfo::GetAddressSpaceSize(39, Type::Alias, ram) ==
                (ram <= 16_GiB ? ram * 8 : 128_GiB));
    }
    REQUIRE(KAddressSpaceInfo::GetAddressSpaceSize(39, Type::Heap, 8_GiB + 1) == 9_GiB);
    REQUIRE(KAddressSpaceInfo::GetAddressSpaceSize(39, Type::Alias, 8_GiB + 1) == 72_GiB);
    REQUIRE(KAddressSpaceInfo::GetAddressSpaceSize(39, Type::Heap, 16_GiB + 1) == 17_GiB);
    REQUIRE(KAddressSpaceInfo::GetAddressSpaceSize(39, Type::Alias, 16_GiB + 1) == 128_GiB);
    REQUIRE(KAddressSpaceInfo::GetAddressSpaceSize(39, Type::Heap,
            std::numeric_limits<size_t>::max()) == 32_GiB);
    REQUIRE(KAddressSpaceInfo::GetAddressSpaceSize(39, Type::Alias,
            std::numeric_limits<size_t>::max()) == 128_GiB);
}

TEST_CASE("RAM-dependent layout preserves fixed regions and older address widths", "[address-space]") {
    using Type = KAddressSpaceInfo::Type;
    for (const size_t ram : {4_GiB, 12_GiB, 32_GiB}) {
        REQUIRE(KAddressSpaceInfo::GetAddressSpaceSize(32, Type::Heap, ram) == 1_GiB);
        REQUIRE(KAddressSpaceInfo::GetAddressSpaceSize(32, Type::Alias, ram) == 1_GiB);
        REQUIRE(KAddressSpaceInfo::GetAddressSpaceSize(36, Type::Heap, ram) == 8_GiB);
        REQUIRE(KAddressSpaceInfo::GetAddressSpaceSize(36, Type::Alias, ram) == 6_GiB);
        REQUIRE(KAddressSpaceInfo::GetAddressSpaceSize(39, Type::Stack, ram) == 2_GiB);
        REQUIRE(KAddressSpaceInfo::GetAddressSpaceSize(39, Type::MapSmall, ram) == 64_GiB);
        const auto regions = KAddressSpaceInfo::GetAddressSpaceSize(39, Type::Heap, ram) +
                             KAddressSpaceInfo::GetAddressSpaceSize(39, Type::Alias, ram) +
                             2_GiB + 64_GiB;
        REQUIRE(regions < 256_GiB);
    }
}
