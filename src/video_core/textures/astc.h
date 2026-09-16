// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

namespace Tegra::Texture::ASTC {

struct BlockLinearLayout {
    uint32_t layer_stride;
    uint32_t slice_size;
    uint32_t block_size;
    uint32_t x_shift;
    uint32_t gob_height;
    uint32_t gob_height_mask;
    uint32_t gob_depth;
    uint32_t gob_depth_mask;
};

void Decompress(std::span<const uint8_t> data, uint32_t width, uint32_t height, uint32_t depth,
                uint32_t block_width, uint32_t block_height, std::span<uint8_t> output);

void DecompressBlockLinear(std::span<const uint8_t> data, uint32_t width, uint32_t height,
                           uint32_t depth, uint32_t layers, uint32_t block_width,
                           uint32_t block_height, const BlockLinearLayout& layout,
                           std::span<uint8_t> output);

} // namespace Tegra::Texture::ASTC
