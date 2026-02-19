// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <common/settings_common.h>
#include "common/common_types.h"
#include "common/settings_setting.h"
#include "common/settings_enums.h"

namespace IOSSettings {
    struct Values {
        Settings::Linkage linkage;
        Settings::Setting<bool> touchscreen{linkage, true, "touchscreen", Settings::Category::Overlay};
    };

    extern Values values;

} // namespace IOSSettings
