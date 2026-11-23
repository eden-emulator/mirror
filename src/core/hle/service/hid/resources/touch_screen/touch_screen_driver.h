// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/common_types.h"
#include "common/result.h"
#include "hid_core/frontend/emulated_console.h"
#include "hid_core/hid_types.h"
#include "core/hle/service/hid/resources/touch_screen/touch_types.h"

namespace Core::HID {
class HIDCore;
} // namespace Core::HID

namespace Service::HID {

/// This handles all request to Ftm3bd56(TouchPanel) hardware
class TouchDriver {
public:
    explicit TouchDriver(Core::HID::HIDCore& hid_core);
    ~TouchDriver();

    Result StartTouchSensor();
    Result StopTouchSensor();
    bool IsRunning() const;

    void ProcessTouchScreenAutoTune() const;

    Result WaitForDummyInput();
    Result WaitForInput();

    void GetNextTouchState(TouchScreenState& out_state) const;

    void SetTouchMode(Core::HID::TouchScreenModeForNx mode);
    Core::HID::TouchScreenModeForNx GetTouchMode() const;

private:
    bool is_running{};
    TouchScreenState touch_status{};
    Core::HID::TouchFingerState fingers{};
    Core::HID::TouchScreenModeForNx touch_mode{};

    Core::HID::EmulatedConsole* console = nullptr;
};

} // namespace Service::HID
