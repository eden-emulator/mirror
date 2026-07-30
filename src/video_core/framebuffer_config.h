// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <span>
#include <vector>

#include "common/common_types.h"
#include "common/math_util.h"
#include "core/hle/service/nvnflinger/buffer_transform_flags.h"
#include "core/hle/service/nvnflinger/hwc_layer.h"
#include "core/hle/service/nvnflinger/pixel_format.h"
#include "core/hle/service/nvnflinger/ui/fence.h"

namespace Tegra {

enum class BlendMode {
    Opaque,
    Premultiplied,
    Coverage,
};

/**
 * Struct describing framebuffer configuration
 */
struct FramebufferConfig {
    DAddr address{};
    u32 offset{};
    u32 width{};
    u32 height{};
    u32 stride{};
    Service::android::PixelFormat pixel_format{};
    Service::android::BufferTransformFlags transform_flags{};
    Common::Rectangle<int> crop_rect{};
    BlendMode blending{};
    u32 layer_stack_mask{Service::Nvnflinger::DefaultLayerStackMask};
};

Common::Rectangle<f32> NormalizeCrop(const FramebufferConfig& framebuffer, u32 texture_width,
                                     u32 texture_height);

/**
 * Returns the subset of layers belonging to a stack.
 */
std::span<const FramebufferConfig> FilterLayerStack(std::span<const FramebufferConfig> layers,
                                                    Service::Nvnflinger::LayerStackId stack,
                                                    std::vector<FramebufferConfig>& scratch);

} // namespace Tegra
