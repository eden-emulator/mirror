// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <mutex>
#include <ankerl/unordered_dense.h>

#include "common/container_hash.h"
#include "video_core/surface.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

struct RenderPassKey {
    bool operator==(const RenderPassKey&) const noexcept = default;

    std::array<VideoCore::Surface::PixelFormat, 8> color_formats;
    VideoCore::Surface::PixelFormat depth_format;
    VkSampleCountFlagBits samples;
    bool resolve_color;
    bool resolve_depth_stencil;
    u32 color_clear_mask;
    bool depth_stencil_clear;
    u32 color_discard_mask;
    bool depth_stencil_discard;
};

} // namespace Vulkan

namespace std {
template <>
struct hash<Vulkan::RenderPassKey> {
    static_assert(std::tuple_size_v<decltype(Vulkan::RenderPassKey::color_formats)> <= 8);
    static_assert(static_cast<u32>(VideoCore::Surface::PixelFormat::Invalid) <= 0xFF);
    static_assert(static_cast<u32>(VideoCore::Surface::PixelFormat::Max) <= 0xFF);
    static_assert(VK_SAMPLE_COUNT_64_BIT <= 0xFF);

    [[nodiscard]] size_t operator()(const Vulkan::RenderPassKey& key) const noexcept {
        u64 formats = 0;
        for (size_t index = 0; index < key.color_formats.size(); ++index) {
            formats |= static_cast<u64>(key.color_formats[index]) << (index * 8);
        }
        const u64 state = static_cast<u64>(key.depth_format) |
                          (static_cast<u64>(key.samples) << 8) |
                          (static_cast<u64>(key.color_clear_mask) << 16) |
                          (static_cast<u64>(key.color_discard_mask) << 24) |
                          (static_cast<u64>(key.resolve_color) << 32) |
                          (static_cast<u64>(key.depth_stencil_clear) << 33) |
                          (static_cast<u64>(key.resolve_depth_stencil) << 34) |
                          (static_cast<u64>(key.depth_stencil_discard) << 35);
        size_t seed = 0;
        Common::HashCombine(seed, formats);
        Common::HashCombine(seed, state);
        return seed;
    }
};
} // namespace std

namespace Vulkan {

class Device;

[[nodiscard]] bool SupportsDepthStencilResolve(const Device& device,
                                               VideoCore::Surface::PixelFormat depth_format);

class RenderPassCache {
public:
    explicit RenderPassCache(const Device& device_);

    VkRenderPass Get(const RenderPassKey& key);

private:
    const Device* device{};
    ankerl::unordered_dense::map<RenderPassKey, vk::RenderPass> cache;
    std::mutex mutex;
};

} // namespace Vulkan
