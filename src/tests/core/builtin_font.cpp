// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <cstring>
#include <catch2/catch_test_macros.hpp>
#include "core/hle/service/ns/builtin_font.h"

TEST_CASE("Native font records expose bounded TTF data in the SDK wire layout", "[builtin-font]") {
    using namespace Service::NS;
    BuiltInFontDescriptor descriptor{};
    REQUIRE(DescribeBuiltInFont(descriptor, 5, 0x112238, 0x1234, 0x1100000));
    const std::array<u8, 12> expected{5, 0, 0, 0, 0x38, 0x22, 0x11, 0, 0x34, 0x12, 0, 0};
    std::array<u8, 12> wire{};
    std::memcpy(wire.data(), &descriptor, wire.size());
    REQUIRE(wire == expected);
    REQUIRE(DescribeBuiltInFont(descriptor, 0, 8, 0xff8, 0x1000));
    REQUIRE(descriptor.offset == 8);
    REQUIRE(descriptor.size == 0xff8);
}

TEST_CASE("Native fonts do not advertise missing or out-of-range data", "[builtin-font]") {
    using namespace Service::NS;
    for (const auto& range : {std::array<u32, 2>{0, 0}, {0x1001, 1}, {0xff0, 0x11},
                              {0xfffffff0, 0x100}, {0x1000, 1}}) {
        BuiltInFontDescriptor descriptor{5, 1, 7, 8, 12};
        REQUIRE_FALSE(DescribeBuiltInFont(descriptor, 5, range[0], range[1], 0x1000));
        REQUIRE(descriptor.type == 0);
        REQUIRE(descriptor.flags == 0);
        REQUIRE(descriptor.reserved == 0);
        REQUIRE(descriptor.offset == 0);
        REQUIRE(descriptor.size == 0);
    }
}
