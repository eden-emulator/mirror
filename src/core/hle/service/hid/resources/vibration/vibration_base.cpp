// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "hid_core/hid_result.h"
#include "core/hle/service/hid/resources/npad/npad_types.h"
#include "core/hle/service/hid/resources/npad/npad_vibration.h"
#include "core/hle/service/hid/resources/vibration/vibration_base.h"

namespace Service::HID {

NpadVibrationBase::NpadVibrationBase() {}

Result NpadVibrationBase::Activate() {
    ref_counter++;
    return ResultSuccess;
}

Result NpadVibrationBase::Deactivate() {
    if (ref_counter > 0) {
        ref_counter--;
    }

    return ResultSuccess;
}

bool NpadVibrationBase::IsActive() const {
    return ref_counter > 0;
}

bool NpadVibrationBase::IsVibrationMounted() const {
    return is_mounted;
}

} // namespace Service::HID
