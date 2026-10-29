// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <cstring>
#include <catch2/catch_test_macros.hpp>
#include "core/hle/service/am/am_types.h"
#include "core/hle/service/am/frontend/applet_software_keyboard_types.h"

TEST_CASE("Native keyboard applets select the software keyboard frontend", "[applet]") {
    using namespace Service::AM;
    REQUIRE(GetFrontendAppletId(static_cast<AppletId>(0x01020011)) == AppletId::SoftwareKeyboard);
    REQUIRE(GetFrontendAppletId(AppletId::SoftwareKeyboard) == AppletId::SoftwareKeyboard);
}

TEST_CASE("Frontend applet routing preserves unrelated and unknown IDs", "[applet]") {
    using namespace Service::AM;
    for (const u32 value : {0U, 1U, 0x10U, 0x12U, 0x01000011U, 0x01010011U, 0x01030011U, 0x02020011U,
                            0xffffffffU}) {
        const auto id = static_cast<AppletId>(value);
        REQUIRE(GetFrontendAppletId(id) == id);
    }
}

TEST_CASE("Native keyboard configuration preserves text encoding and validation", "[applet]") {
    using namespace Service::AM::Frontend;
    std::array<u8, 0x530> data{};
    const auto store = [&]<typename T>(size_t offset, const T& value) {
        std::memcpy(data.data() + offset, &value, sizeof(value));
    };
    store(0, u32{4});
    store(0x24, u'W');
    data[0x1B] = 1;
    data[0x3AA] = 1;
    data[0x3AB] = 1;
    store(0x3AC, u32{12});
    store(0x3B0, u32{1});
    store(0x3B4, u32{1});
    store(0x3B8, u32{1});
    store(0x3BC, u32{0x40});
    store(0x3C0, u32{3});
    store(0x3C4, u32{0x80});
    store(0x3C8, u32{2});
    store(0x3CC, u64{0x123456789abcdef0ULL});
    data[0x48C] = 1;
    data[0x48D] = 1;
    data[0x48E] = 1;
    store(0x490, u32{0xffffffff});
    SwkbdConfigCommon common{};
    SwkbdConfigNew config{};
    REQUIRE(DecodeNativeSwkbdConfig(data, common, config));
    REQUIRE(common.type == SwkbdType::Latin);
    REQUIRE(common.header_text[0] == u'W');
    REQUIRE(common.enable_return_button);
    REQUIRE(common.use_utf8);
    REQUIRE(common.use_blur_background);
    REQUIRE(common.max_text_length == 12);
    REQUIRE(common.min_text_length == 1);
    REQUIRE(common.password_mode == SwkbdPasswordMode::Enabled);
    REQUIRE(common.text_draw_type == SwkbdTextDrawType::Box);
    REQUIRE(common.initial_string_offset == 0x40);
    REQUIRE(common.initial_string_length == 3);
    REQUIRE(common.user_dictionary_offset == 0x80);
    REQUIRE(common.user_dictionary_entries == 2);
    REQUIRE(common.use_text_check);
    REQUIRE(config.customized_dictionary_set_entries[0] == 0x123456789abcdef0ULL);
    REQUIRE(config.total_customized_dictionary_set_entries == 1);
    REQUIRE(config.disable_cancel_button);
    REQUIRE(config.text_grouping[0] == 0xffffffff);
    data[0x1B] = data[0x3AA] = data[0x3AB] = data[0x48D] = data[0x48E] = 0;
    REQUIRE(DecodeNativeSwkbdConfig(data, common, config));
    REQUIRE_FALSE(common.enable_return_button);
    REQUIRE_FALSE(common.use_utf8);
    REQUIRE_FALSE(common.use_blur_background);
    REQUIRE_FALSE(common.use_text_check);
    REQUIRE_FALSE(config.disable_cancel_button);
}

TEST_CASE("Native keyboard decoder rejects incomplete and unrelated layouts", "[applet]") {
    using namespace Service::AM::Frontend;
    std::array<u8, 0x531> data{};
    SwkbdConfigCommon common{};
    SwkbdConfigNew config{};
    common.max_text_length = 12;
    for (const size_t size : {0U, 0x3D4U, 0x4C8U, 0x52FU, 0x531U}) {
        REQUIRE_FALSE(DecodeNativeSwkbdConfig(std::span{data}.first(size), common, config));
        REQUIRE(common.max_text_length == 12);
    }
}
