// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "video_core/texture_cache/image_base.h"
#include "video_core/texture_cache/image_info.h"
#include "video_core/texture_cache/image_view_info.h"
#include "video_core/texture_cache/util.h"

namespace {
VideoCommon::ImageInfo ArrayInfo(bool native) {
    VideoCommon::ImageInfo info;
    info.type = VideoCommon::ImageType::e2D;
    info.format = VideoCore::Surface::PixelFormat::A8B8G8R8_UNORM;
    info.size = {64, 64, 1};
    info.native_tiling = native;
    info.layer_stride = VideoCommon::CalculateLayerStride(info);
    info.maybe_unaligned_layer_stride = info.layer_stride;
    return info;
}
}

TEST_CASE("Repeated views inside an existing array do not grow its storage", "[texture-overlap]") {
    const auto single = ArrayInfo(GENERATE(false, true));
    auto storage = single;
    storage.resources.layers = 3;
    constexpr GPUVAddr gpu = 0x100000;
    constexpr VAddr cpu = 0x200000;
    for (u32 frame = 0; frame < 100; ++frame) {
        const VideoCommon::ImageBase existing{storage, gpu, cpu};
        const u32 offset = (1 + frame % 2) * single.layer_stride;
        const auto joined = VideoCommon::ResolveOverlap(single, gpu + offset, cpu + offset,
                                                        existing, true, false, true);
        REQUIRE(joined);
        REQUIRE(joined->gpu_addr == gpu);
        REQUIRE(joined->cpu_addr == cpu);
        REQUIRE(joined->resources.layers == 3);
        storage.resources = joined->resources;
        REQUIRE(VideoCommon::CalculateUnswizzledSizeBytes(storage) == 64 * 64 * 4 * 3);
    }
}

TEST_CASE("Array overlap extends only to the end of the new view", "[texture-overlap]") {
    auto storage = ArrayInfo(GENERATE(false, true));
    storage.resources.layers = 3;
    auto view = storage;
    view.resources.layers = 4;
    constexpr GPUVAddr gpu = 0x100000;
    constexpr VAddr cpu = 0x200000;
    const VideoCommon::ImageBase existing{storage, gpu, cpu};
    const u32 offset = 2 * storage.layer_stride;
    const auto joined = VideoCommon::ResolveOverlap(view, gpu + offset, cpu + offset,
                                                    existing, true, false, true);
    REQUIRE(joined);
    REQUIRE(joined->resources.layers == 6);
}

TEST_CASE("Native texture views retain their direct address without a legacy array prefix", "[texture-overlap]") {
    Tegra::Texture::TICEntry raw{};
    raw.format.Assign(Tegra::Texture::TextureFormat::A8B8G8R8);
    raw.r_type.Assign(Tegra::Texture::ComponentType::UNORM);
    raw.g_type.Assign(Tegra::Texture::ComponentType::UNORM);
    raw.b_type.Assign(Tegra::Texture::ComponentType::UNORM);
    raw.a_type.Assign(Tegra::Texture::ComponentType::UNORM);
    raw.header_version.Assign(Tegra::Texture::TICHeaderVersion::BlockLinear);
    raw.block_height.Assign(4);
    raw.width_minus_one.Assign(1919);
    raw.height_minus_1.Assign(1079);
    const bool array = GENERATE(false, true);
    raw.texture_type.Assign(array ? Tegra::Texture::TextureType::Texture2DArray : Tegra::Texture::TextureType::Texture2D);
    raw.depth_minus_1.Assign(array ? 2 : 0);
    raw.normalized_coords.Assign(1);
    raw.address_high.Assign(0x81);
    raw.address_low = 0x870000;
    raw.layer_base_0_2.Assign(1);
    raw.layer_base_3_7.Assign(4);
    REQUIRE(raw.BaseLayer() == 33);
    REQUIRE(VideoCommon::TextureViewHeader(raw, false) == raw);
    const auto native = VideoCommon::TextureViewHeader(raw, true);
    REQUIRE(native.BaseLayer() == 0);
    REQUIRE(native.Address() == raw.Address());
    const VideoCommon::ImageInfo info(native);
    REQUIRE(info.size.width == 1920);
    REQUIRE(info.size.height == 1080);
    REQUIRE(info.resources.layers == (array ? 3 : 1));
    REQUIRE(VideoCommon::CalculateUnswizzledSizeBytes(info) == size_t{1920} * 1080 * 4 * (array ? 3U : 1U));
    const VideoCommon::ImageViewInfo view(native, 0);
    REQUIRE(view.range.base.layer == 0);
    REQUIRE(view.type == (array ? VideoCommon::ImageViewType::e2DArray : VideoCommon::ImageViewType::e2D));
    REQUIRE(view.range.extent.layers == (array ? 3 : 1));
    REQUIRE(VideoCommon::ImageInfo(raw).resources.layers == (array ? 36 : 34));
}
