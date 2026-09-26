// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/omm/power_state_interface.h"

namespace Service::OMM {

IPowerStateInterface::IPowerStateInterface(Core::System& system_)
    : ServiceFramework{system_, "spsm"} {
}

IPowerStateInterface::~IPowerStateInterface() = default;

} // namespace Service::OMM
