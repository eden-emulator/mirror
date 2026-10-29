// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>
#include "common/logging.h"
#include "audio_core/common/feature_support.h"
#include "audio_core/renderer/behavior/behavior_info.h"

TEST_CASE("Audio revisions retain the platform marker through renderer replies", "[audio-revision]") {
    using namespace AudioCore;
    constexpr u32 legacy = 0x3f564552;
    constexpr u32 native = 0x40764552;
    REQUIRE(GetRevisionNum(legacy) == 15);
    REQUIRE(GetRevisionNum(native) == 16);
    REQUIRE(CheckValidRevision(legacy));
    REQUIRE(CheckValidRevision(native));
    REQUIRE_FALSE(CheckValidRevision(0x41764552));
    REQUIRE_FALSE(CheckValidRevision(0x40564552));
    REQUIRE_FALSE(CheckValidRevision(0x40764553));
    REQUIRE_FALSE(CheckValidRevision(0x2f764552));
    REQUIRE_FALSE(CheckFeatureSupported(SupportTags::VoiceInParameterV2, 0x40764553));
    REQUIRE(CheckFeatureSupported(SupportTags::VoiceInParameterV2, native));
    REQUIRE_FALSE(CheckFeatureSupported(SupportTags::VoiceInParameterV2, 14));
    Renderer::BehaviorInfo behavior;
    behavior.SetUserLibRevision(native);
    REQUIRE(behavior.GetUserRevisionNum() == 16);
    REQUIRE(behavior.GetUserRevision() == native);
    REQUIRE(behavior.GetProcessRevision() == native);
    behavior.SetUserLibRevision(legacy);
    REQUIRE(behavior.GetUserRevision() == legacy);
    REQUIRE(behavior.GetProcessRevision() == legacy);
}
