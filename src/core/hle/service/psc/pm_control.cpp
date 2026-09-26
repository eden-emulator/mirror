// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/psc/pm_control.h"

namespace Service::PSC {

IPmControl::IPmControl(Core::System& system_) : ServiceFramework{system_, "psc:c"} {
}

IPmControl::~IPmControl() = default;

} // namespace Service::PSC
