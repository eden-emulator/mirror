// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>

#include "core/frontend/emu_window.h"
#include "yuzu_cmd/emu_window/emu_window_sdl3.h"

namespace Core {
class System;
}

namespace InputCommon {
class InputSubsystem;
}

class EmuWindow_SDL3_MTL final : public EmuWindow_SDL3 {
public:
    explicit EmuWindow_SDL3_MTL(InputCommon::InputSubsystem* input_subsystem_, Core::System& system,
                                bool fullscreen);
    ~EmuWindow_SDL3_MTL() override;

    std::unique_ptr<Core::Frontend::GraphicsContext> CreateSharedContext() const override;

private:
    void* metal_view{nullptr};
};
