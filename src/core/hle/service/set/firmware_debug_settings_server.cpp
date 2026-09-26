// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/set/firmware_debug_settings_server.h"

namespace Service::Set {

IFirmwareDebugSettingsServer::IFirmwareDebugSettingsServer(Core::System& system_)
    : ServiceFramework{system_, "set:fd"} {
}

IFirmwareDebugSettingsServer::~IFirmwareDebugSettingsServer() = default;

} // namespace Service::Set
