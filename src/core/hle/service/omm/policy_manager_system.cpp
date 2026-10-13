// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/omm/policy_manager_system.h"

namespace Service::OMM {

IPolicyManagerSystem::IPolicyManagerSystem(Core::System& system_)
    : ServiceFramework{system_, "idle:sys"} {
}

IPolicyManagerSystem::~IPolicyManagerSystem() = default;

} // namespace Service::OMM
