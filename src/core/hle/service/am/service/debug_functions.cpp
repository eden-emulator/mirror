// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/am/service/debug_functions.h"

namespace Service::AM {

IDebugFunctions::IDebugFunctions(Core::System& system_)
    : ServiceFramework{system_, "IDebugFunctions"} {
}

IDebugFunctions::~IDebugFunctions() = default;

} // namespace Service::AM
