// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <span>
#include <bc_decoder.h>

#include "common/assert.h"
#include "common/common_types.h"
#include "video_core/texture_cache/decode_bc.h"

namespace VideoCommon {

namespace {
constexpr u32 BLOCK_SIZE = 4;

using VideoCore::Surface::PixelFormat;

constexpr bool IsSigned(PixelFormat pixel_format) {
    switch (pixel_format) {
    case PixelFormat::BC4_SNORM:
    case PixelFormat::BC5_SNORM:
    case PixelFormat::BC6H_SFLOAT:
        return true;
    default:
        return false;
    }
}

constexpr u32 BlockSize(PixelFormat pixel_format) {
    switch (pixel_format) {
    case PixelFormat::BC1_RGBA_SRGB:
    case PixelFormat::BC1_RGBA_UNORM:
    case PixelFormat::BC4_SNORM:
    case PixelFormat::BC4_UNORM:
        return 8;
    default:
        return 16;
    }
}
} // Anonymous namespace

u32 ConvertedBytesPerBlock(VideoCore::Surface::PixelFormat pixel_format) {
    switch (pixel_format) {
    case PixelFormat::BC4_SNORM:
    case PixelFormat::BC4_UNORM:
        return 1;
    case PixelFormat::BC5_SNORM:
    case PixelFormat::BC5_UNORM:
        return 2;
    case PixelFormat::BC6H_SFLOAT:
    case PixelFormat::BC6H_UFLOAT:
        return 8;
    default:
        return 4;
    }
}

void DecompressBCn(std::span<const u8> input, std::span<u8> output, BufferImageCopy& copy, VideoCore::Surface::PixelFormat pixel_format) {
    auto const f = [pixel_format]{
        switch (pixel_format) {
        case PixelFormat::BC1_RGBA_UNORM:
        case PixelFormat::BC1_RGBA_SRGB: return &bcn::DecodeBc1;
        case PixelFormat::BC2_UNORM:
        case PixelFormat::BC2_SRGB: return &bcn::DecodeBc2;
        case PixelFormat::BC3_UNORM:
        case PixelFormat::BC3_SRGB: return &bcn::DecodeBc3;
        case PixelFormat::BC4_SNORM:
        case PixelFormat::BC4_UNORM: return &bcn::DecodeBc4;
        case PixelFormat::BC5_SNORM:
        case PixelFormat::BC5_UNORM: return &bcn::DecodeBc5;
        case PixelFormat::BC6H_SFLOAT:
        case PixelFormat::BC6H_UFLOAT: return &bcn::DecodeBc6;
        case PixelFormat::BC7_SRGB:
        case PixelFormat::BC7_UNORM: return &bcn::DecodeBc7;
        default:
            UNREACHABLE_MSG("Unimplemented BCn decompression {}", pixel_format);
            return &bcn::DecodeBc1;
        }
    }();
    const u32 out_bpp = ConvertedBytesPerBlock(pixel_format);
    const u32 block_size = BlockSize(pixel_format);
    const u32 width = copy.image_extent.width;
    const u32 height = copy.image_extent.height * copy.image_subresource.num_layers;
    const u32 depth = copy.image_extent.depth;
    const u32 block_width = (std::min)(width, BLOCK_SIZE);
    const u32 block_height = (std::min)(height, BLOCK_SIZE);
    const u32 pitch = width * out_bpp;
    size_t input_offset = 0;
    size_t output_offset = 0;
    for (u32 slice = 0; slice < depth; ++slice) {
        for (u32 y = 0; y < height; y += block_height) {
            size_t src_offset = input_offset;
            size_t dst_offset = output_offset;
            for (u32 x = 0; x < width; x += block_width) {
                const u8* src = input.data() + src_offset;
                u8* const dst = output.data() + dst_offset;
                f(src, dst, x, y, width, height, IsSigned(pixel_format));
                src_offset += block_size;
                dst_offset += block_width * out_bpp;
            }
            input_offset += copy.buffer_row_length * block_size / block_width;
            output_offset += block_height * pitch;
        }
    }
}

} // namespace VideoCommon
