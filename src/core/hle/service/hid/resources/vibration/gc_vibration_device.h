// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <mutex>

#include "common/common_types.h"
#include "common/result.h"
#include "hid_core/hid_types.h"
#include "core/hle/service/hid/resources/npad/npad_types.h"
#include "core/hle/service/hid/resources/vibration/vibration_base.h"

namespace Service::HID {
class NpadVibration;

/// Handles Npad request from HID interfaces
class NpadGcVibrationDevice final : public NpadVibrationBase {
public:
    explicit NpadGcVibrationDevice();

    Result Activate() override;
    Result Deactivate() override;

    Result Mount(IAbstractedPad& abstracted_pad, u32 slot, NpadVibration* handler);
    Result Unmount();

    Result SendVibrationGcErmCommand(Core::HID::VibrationGcErmCommand command);

    Result GetActualVibrationGcErmCommand(Core::HID::VibrationGcErmCommand& out_command);
    Result SendVibrationNotificationPattern(Core::HID::VibrationGcErmCommand command);

private:
    u32 adapter_slot;
};
} // namespace Service::HID
