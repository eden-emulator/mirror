// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>
#include "common/logging.h"
#include "video_core/native_diagnostics.h"
#include "video_core/memory_manager.h"
#include "video_core/texture_cache/image_base.h"
#include "video_core/texture_cache/image_view_base.h"
#include "video_core/texture_cache/util.h"

namespace VideoCommon {
namespace NativeTextureCapture {
struct State {
    u32 epoch{};
    u32 remaining_draws{};
    u32 image_count{};
    size_t bytes{};
    bool active{};
    std::set<std::array<u64, 4>> images;
    std::set<u32> samplers;
};
inline State& GetState() {
    static thread_local State state;
    return state;
}
inline void Write(const std::string& path, const void* data, size_t size) {
    std::ofstream file(path, std::ios::binary);
    file.write(static_cast<const char*>(data), size);
}
} // namespace NativeTextureCapture

// Opt-in, bounded snapshots of the textures used by a short sequence of draws.
inline bool BeginNativeTextureCapture() {
    if (!NativeDrawDiagnosticsEnabled()) return false;
    auto& state = NativeTextureCapture::GetState();
    std::error_code error;
    if (std::filesystem::remove("native-textures.request", error)) {
        ++state.epoch;
        state.remaining_draws = 64;
        state.image_count = 0;
        state.bytes = 0;
        state.images.clear();
        state.samplers.clear();
    }
    state.active = state.remaining_draws != 0;
    if (state.active) --state.remaining_draws;
    return state.active;
}

inline void CaptureNativeTexture(Tegra::MemoryManager& memory, u32 index,
                                 const Tegra::Texture::TICEntry& descriptor,
                                 const ImageViewBase& view, const ImageBase& image) {
    auto& state = NativeTextureCapture::GetState();
    if (!state.active || state.images.size() >= 64 || !state.images.insert(descriptor.raw).second)
        return;
    const std::string prefix = "native-texture-" + std::to_string(state.epoch) + "-" +
                               std::to_string(state.image_count++);
    NativeTextureCapture::Write(prefix + ".tic.bin", &descriptor, sizeof(descriptor));
    const auto& info = image.info;
    std::ofstream metadata(prefix + ".txt");
    metadata << "index " << index << "\ngpu " << std::hex << image.gpu_addr << std::dec
             << "\nview_gpu " << std::hex << view.gpu_addr << std::dec
             << "\npixel_format " << static_cast<u32>(info.format)
             << "\nguest_format " << static_cast<u32>(descriptor.format.Value())
             << "\nimage_type " << static_cast<u32>(info.type)
             << "\nview_type " << static_cast<u32>(view.type)
             << "\nsize " << info.size.width << ' ' << info.size.height << ' ' << info.size.depth
             << "\nview_size " << view.size.width << ' ' << view.size.height << ' ' << view.size.depth
             << "\nresources " << info.resources.levels << ' ' << info.resources.layers
             << "\nview_range " << view.range.base.level << ' ' << view.range.base.layer << ' '
             << view.range.extent.levels << ' ' << view.range.extent.layers
             << "\nblock " << info.block.width << ' ' << info.block.height << ' ' << info.block.depth
             << "\npitch " << info.pitch << "\nlayer_stride " << info.layer_stride
             << "\ntile_width_spacing " << info.tile_width_spacing
             << "\nformat_block " << VideoCore::Surface::DefaultBlockWidth(info.format) << ' '
             << VideoCore::Surface::DefaultBlockHeight(info.format) << ' '
             << VideoCore::Surface::BytesPerBlock(info.format)
             << "\nnative_tiling " << info.native_tiling
             << "\nflags " << static_cast<u32>(image.flags)
             << "\nguest_bytes " << image.guest_size_bytes
             << "\nlinear_bytes " << image.unswizzled_size_bytes
             << "\nconverted_bytes " << image.converted_size_bytes << '\n';
    if (True(image.flags & ImageFlagBits::GpuModified)) {
        metadata << "skipped gpu_modified_source\n";
        return;
    }
    constexpr size_t budget = 128 * 1024 * 1024;
    const size_t size = size_t{image.guest_size_bytes} + image.unswizzled_size_bytes + image.converted_size_bytes;
    if (!image.guest_size_bytes || image.guest_size_bytes > 16 * 1024 * 1024 ||
        image.unswizzled_size_bytes > 32 * 1024 * 1024 || image.converted_size_bytes > 64 * 1024 * 1024 ||
        size > budget - state.bytes || !memory.IsFullyMappedRange(image.gpu_addr, image.guest_size_bytes)) {
        metadata << "skipped size_budget_or_mapping\n";
        return;
    }
    state.bytes += size;
    std::vector<u8> raw(image.guest_size_bytes);
    memory.ReadBlockUnsafe(image.gpu_addr, raw.data(), raw.size());
    NativeTextureCapture::Write(prefix + ".raw.bin", raw.data(), raw.size());
    std::vector<u8> linear(image.unswizzled_size_bytes);
    auto copies = UnswizzleImage(memory, image.gpu_addr, info, raw, linear);
    NativeTextureCapture::Write(prefix + ".linear.bin", linear.data(), linear.size());
    for (const auto& copy : copies) {
        metadata << "copy " << copy.buffer_offset << ' ' << copy.buffer_size << ' '
                 << copy.buffer_row_length << ' ' << copy.buffer_image_height << ' '
                 << copy.image_subresource.base_level << ' ' << copy.image_subresource.base_layer << ' '
                 << copy.image_subresource.num_layers << ' ' << copy.image_extent.width << ' '
                 << copy.image_extent.height << ' ' << copy.image_extent.depth << '\n';
    }
    if (True(image.flags & ImageFlagBits::Converted) && image.converted_size_bytes) {
        std::vector<u8> converted(image.converted_size_bytes);
        ConvertImage(linear, info, converted, copies);
        NativeTextureCapture::Write(prefix + ".converted.bin", converted.data(), converted.size());
    }
    LOG_WARNING(HW_GPU, "Native texture capture {}: index={}, address={:#x}, dimensions={}x{}, format={}",
                prefix, index, image.gpu_addr, info.size.width, info.size.height, static_cast<u32>(info.format));
}

inline void CaptureNativeSampler(u32 index, const Tegra::Texture::TSCEntry& descriptor) {
    auto& state = NativeTextureCapture::GetState();
    if (!state.active || state.samplers.size() >= 64 || !state.samplers.insert(index).second) return;
    const std::string prefix = "native-sampler-" + std::to_string(state.epoch) + "-" + std::to_string(index);
    NativeTextureCapture::Write(prefix + ".tsc.bin", &descriptor, sizeof(descriptor));
    std::ofstream metadata(prefix + ".txt");
    metadata << "index " << index << "\nwrap " << static_cast<u32>(descriptor.wrap_u.Value()) << ' '
             << static_cast<u32>(descriptor.wrap_v.Value()) << ' ' << static_cast<u32>(descriptor.wrap_p.Value())
             << "\nfilter " << static_cast<u32>(descriptor.mag_filter.Value()) << ' '
             << static_cast<u32>(descriptor.min_filter.Value()) << ' '
             << static_cast<u32>(descriptor.mipmap_filter.Value())
             << "\nlod_bias " << descriptor.LodBias()
             << "\nlod_clamp " << descriptor.min_lod_clamp.Value() << ' ' << descriptor.max_lod_clamp.Value()
             << "\nnormalized " << descriptor.float_coord_normalization.Value() << '\n';
}
} // namespace VideoCommon
