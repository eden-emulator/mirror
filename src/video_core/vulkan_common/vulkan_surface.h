// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/frontend/emu_window.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

[[nodiscard]] vk::SurfaceKHR CreateSurface(
    const vk::Instance& instance, const Core::Frontend::EmuWindow::WindowSystemInfo& window_info);

} // namespace Vulkan
