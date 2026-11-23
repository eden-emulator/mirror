// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/hid/resources/controller_base.h"
#include "core/hle/service/hid/resources/debug_pad/debug_pad_types.h"

namespace Core::HID {
class HIDCore;
class EmulatedController;
} // namespace Core::HID

namespace Core::Timing {
class CoreTiming;
}

namespace Service::HID {
class DebugPad final : public ControllerBase {
public:
    explicit DebugPad(Core::HID::HIDCore& hid_core_);
    ~DebugPad() override;

    // Called when the controller is initialized
    void OnInit() override;

    // When the controller is released
    void OnRelease() override;

    // When the controller is requesting an update for the shared memory
    void OnUpdate(const Core::Timing::CoreTiming& core_timing) override;

private:
    DebugPadState next_state{};
    Core::HID::EmulatedController* controller = nullptr;
};
} // namespace Service::HID
