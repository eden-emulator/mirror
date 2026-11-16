// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/frontend/emu_window.h"
#include "video_core/metal_common/metal_wrapper.h"

namespace Metal {

class Device;

/// Resolves the frontend's native surface handle to a CAMetalLayer and binds it
/// to our device. Qt (qt_common.cpp) and SDL3 both already hand us a
/// CAMetalLayer on Apple platforms because MoltenVK needed one, so we take the
/// same pointer and drive it directly instead of going through a VkSurfaceKHR.
/// The returned layer is retained and the caller owns it. Throws Exception if
/// the handle is missing or is not a CAMetalLayer.
[[nodiscard]] Owned<CA::MetalLayer> CreateSurface(
    const Device& device, const Core::Frontend::EmuWindow::WindowSystemInfo& window_info);

} // namespace Metal
