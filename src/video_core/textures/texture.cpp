// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cmath>

#include "common/cityhash.h"
#include "common/settings.h"
#include "video_core/textures/texture.h"

using Tegra::Texture::TICEntry;
using Tegra::Texture::TSCEntry;

namespace Tegra::Texture {

namespace {

float SrgbToLinear(u32 value) {
    const float encoded = static_cast<float>(value) / 255.0f;
    if (encoded <= 0.04045f) {
        return encoded / 12.92f;
    }
    return std::pow((encoded + 0.055f) / 1.055f, 2.4f);
}

} // Anonymous namespace

std::array<float, 4> TSCEntry::BorderColor() const noexcept {
    return border_color;
}

std::array<float, 4> TSCEntry::SrgbBorderColor() const noexcept {
    return {SrgbToLinear(srgb_border_color_r), SrgbToLinear(srgb_border_color_g),
            SrgbToLinear(srgb_border_color_b), border_color[3]};
}

float TSCEntry::MaxAnisotropy() const noexcept {
    const bool is_suitable_mipmap_filter = mipmap_filter != TextureMipmapFilter::None;
    const bool has_regular_lods = min_lod_clamp == 0 && max_lod_clamp >= 256;
    const bool is_bilinear_filter = min_filter == TextureFilter::Linear &&
                                    reduction_filter == SamplerReduction::WeightedAverage;
    if (max_anisotropy == 0 && (!is_suitable_mipmap_filter || !has_regular_lods || !is_bilinear_filter || depth_compare_enabled))
        return 1.0f;

    s32 added_anisotropic{};
    auto const anisotropic_settings = Settings::values.max_anisotropy.GetValue();
    switch (anisotropic_settings) {
    case Settings::AnisotropyMode::Default:
    case Settings::AnisotropyMode::X2:
    case Settings::AnisotropyMode::X4:
    case Settings::AnisotropyMode::X8:
    case Settings::AnisotropyMode::X16:
        added_anisotropic = s32(anisotropic_settings) - 1;
        break;
    case Settings::AnisotropyMode::Automatic: {
        const u32 resolution_scale = Settings::values.resolution_info.up_scale >>
                                     Settings::values.resolution_info.down_shift;
        if (resolution_scale > 1U) {
            added_anisotropic = static_cast<s32>(resolution_scale - 1U);
        }
        break;
    }
    }
    return float(1U << (max_anisotropy + added_anisotropic));
}

} // namespace Tegra::Texture

size_t std::hash<TICEntry>::operator()(const TICEntry& tic) const noexcept {
    return Common::CityHash64(reinterpret_cast<const char*>(&tic), sizeof tic);
}

size_t std::hash<TSCEntry>::operator()(const TSCEntry& tsc) const noexcept {
    return Common::CityHash64(reinterpret_cast<const char*>(&tsc), sizeof tsc);
}
