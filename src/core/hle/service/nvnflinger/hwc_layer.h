// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/math_util.h"
#include "core/hle/service/nvdrv/nvdata.h"
#include "core/hle/service/nvnflinger/buffer_transform_flags.h"
#include "core/hle/service/nvnflinger/pixel_format.h"
#include "core/hle/service/nvnflinger/ui/fence.h"

namespace Service::Nvnflinger {

// hwc_layer_t::blending values
enum class LayerBlending : u32 {
    // No blending
    None = 0x100,

    // ONE / ONE_MINUS_SRC_ALPHA
    Premultiplied = 0x105,

    // SRC_ALPHA / ONE_MINUS_SRC_ALPHA
    Coverage = 0x405,
};

enum class LayerStackId : u32 {
    Default = 0,
    Lcd = 1,
    Screenshot = 2,
    Recording = 3,
    LastFrame = 4,
    Arbitrary = 5,
    ApplicationForDebug = 6,
    Null = 10,
};

constexpr u32 LayerStackBit(LayerStackId id) {
    return 1U << static_cast<u32>(id);
}

constexpr u32 DefaultLayerStackMask =
    LayerStackBit(LayerStackId::Default) | LayerStackBit(LayerStackId::Screenshot) |
    LayerStackBit(LayerStackId::Recording) | LayerStackBit(LayerStackId::LastFrame);

struct HwcLayer {
    u32 buffer_handle;
    u32 offset;
    android::PixelFormat format;
    u32 width;
    u32 height;
    u32 stride;
    s32 z_index;
    LayerBlending blending;
    android::BufferTransformFlags transform;
    Common::Rectangle<int> crop_rect;
    android::Fence acquire_fence;
    u32 layer_stack_mask;
};

} // namespace Service::Nvnflinger
