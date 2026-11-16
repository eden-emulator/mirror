// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <random>
#include <frozen/string.h>
#include "common/settings.h"
#include "common/random.h"
#include "settings_generator.h"

namespace FrontendCommon {

void GenerateSettings() {
    auto gen = Common::Random::GetMT19937();

    // Randomly generated number because, well, we fill the rest automagically ;)
    // Other serial parts are filled by Region_Index
    std::uniform_int_distribution<u32> distribution(1, (std::numeric_limits<u32>::max)());
    if (Settings::values.serial_unit.GetValue() == 0)
        Settings::values.serial_unit.SetValue(distribution(gen));
    if (Settings::values.serial_battery.GetValue() == 0)
        Settings::values.serial_battery.SetValue(distribution(gen));
}

}
