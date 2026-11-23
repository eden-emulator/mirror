// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2014 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once
#include "video_core/host.h"

#include <memory>
#include <optional>

namespace Core::Frontend {
class EmuWindow;
}

namespace Tegra {
class GPU;
}

namespace VideoCore {

class RendererBase;
void CreateGPU(std::optional<Tegra::GPU>& gpu, Core::Frontend::EmuWindow& emu_window, VideoCore::Host& system);

} // namespace VideoCore
