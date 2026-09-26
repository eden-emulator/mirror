// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/omm/operation_mode_manager.h"

namespace Service::OMM {

IOperationModeManager::IOperationModeManager(Core::System& system_)
    : ServiceFramework{system_, "omm"} {
}

IOperationModeManager::~IOperationModeManager() = default;

} // namespace Service::OMM
