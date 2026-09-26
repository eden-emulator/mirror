// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ns/system_update_control.h"

namespace Service::NS {

ISystemUpdateControl::ISystemUpdateControl(Core::System& system_)
    : ServiceFramework{system_, "ISystemUpdateControl"} {
}

ISystemUpdateControl::~ISystemUpdateControl() = default;

} // namespace Service::NS
