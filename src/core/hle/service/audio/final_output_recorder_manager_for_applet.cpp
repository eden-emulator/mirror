// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/audio/final_output_recorder_manager_for_applet.h"

namespace Service::Audio {

IFinalOutputRecorderManagerForApplet::IFinalOutputRecorderManagerForApplet(Core::System& system_)
    : ServiceFramework{system_, "audrec:a"} {
}

IFinalOutputRecorderManagerForApplet::~IFinalOutputRecorderManagerForApplet() = default;

} // namespace Service::Audio
