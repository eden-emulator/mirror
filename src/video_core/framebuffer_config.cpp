// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>

#include "common/assert.h"
#include "video_core/framebuffer_config.h"

namespace Tegra {

std::span<const FramebufferConfig> FilterLayerStack(
    std::span<const FramebufferConfig> layers, Service::Nvnflinger::LayerStackId stack,
    std::vector<FramebufferConfig>& scratch) {
    const u32 bit = Service::Nvnflinger::LayerStackBit(stack);

    if (std::ranges::all_of(layers,
                            [bit](const auto& layer) { return (layer.layer_stack_mask & bit) != 0; }))
        return layers;

    scratch.clear();
    for (const auto& layer : layers) {
        if ((layer.layer_stack_mask & bit) != 0) {
            scratch.push_back(layer);
        }
    }

    return scratch;
}

Common::Rectangle<f32> NormalizeCrop(const FramebufferConfig& framebuffer, u32 texture_width,
                                     u32 texture_height) {
    f32 left, top, right, bottom;

    if (!framebuffer.crop_rect.IsEmpty()) {
        // If crop rectangle is not empty, apply properties from rectangle.
        left = static_cast<f32>(framebuffer.crop_rect.left);
        top = static_cast<f32>(framebuffer.crop_rect.top);
        right = static_cast<f32>(framebuffer.crop_rect.right);
        bottom = static_cast<f32>(framebuffer.crop_rect.bottom);
    } else {
        // Otherwise, fall back to framebuffer dimensions.
        left = 0;
        top = 0;
        right = static_cast<f32>(framebuffer.width);
        bottom = static_cast<f32>(framebuffer.height);
    }

    // Apply transformation flags.
    auto framebuffer_transform_flags = framebuffer.transform_flags;

    if (True(framebuffer_transform_flags & Service::android::BufferTransformFlags::FlipH)) {
        // Switch left and right.
        std::swap(left, right);
    }
    if (True(framebuffer_transform_flags & Service::android::BufferTransformFlags::FlipV)) {
        // Switch top and bottom.
        std::swap(top, bottom);
    }

    framebuffer_transform_flags &= ~Service::android::BufferTransformFlags::FlipH;
    framebuffer_transform_flags &= ~Service::android::BufferTransformFlags::FlipV;
    if (True(framebuffer_transform_flags)) {
        UNIMPLEMENTED_MSG("Unsupported framebuffer_transform_flags={}",
                          static_cast<u32>(framebuffer_transform_flags));
    }

    // Normalize coordinate space.
    left /= static_cast<f32>(texture_width);
    top /= static_cast<f32>(texture_height);
    right /= static_cast<f32>(texture_width);
    bottom /= static_cast<f32>(texture_height);

    return Common::Rectangle<f32>(left, top, right, bottom);
}

} // namespace Tegra
