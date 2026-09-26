// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/hle/service/btm/btm_debug.h"

namespace Service::BTM {

IBtmDebug::IBtmDebug(Core::System& system_) : ServiceFramework{system_, "btm:dbg"} {
}

IBtmDebug::~IBtmDebug() = default;

} // namespace Service::BTM
