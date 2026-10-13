// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/set/factory_settings_server.h"

namespace Service::Set {

IFactorySettingsServer::IFactorySettingsServer(Core::System& system_)
    : ServiceFramework{system_, "set:cal"} {
}

IFactorySettingsServer::~IFactorySettingsServer() = default;

} // namespace Service::Set
