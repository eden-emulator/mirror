// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/caps/caps_sc.h"

namespace Service::Capture {

IScreenShotControlService::IScreenShotControlService(Core::System& system_)
    : ServiceFramework{system_, "caps:sc"} {
}

IScreenShotControlService::~IScreenShotControlService() = default;

} // namespace Service::Capture
