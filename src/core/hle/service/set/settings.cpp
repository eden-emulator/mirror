// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/server_manager.h"
#include "core/hle/service/set/factory_settings_server.h"
#include "core/hle/service/set/firmware_debug_settings_server.h"
#include "core/hle/service/set/settings.h"
#include "core/hle/service/set/settings_server.h"
#include "core/hle/service/set/system_settings_server.h"

namespace Service::Set {

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("set", std::make_shared<ISettingsServer>(system), 60);
    server_manager->RegisterNamedService("set:cal", std::make_shared<IFactorySettingsServer>(system), 60);
    server_manager->RegisterNamedService("set:fd", std::make_shared<IFirmwareDebugSettingsServer>(system), 60);
    server_manager->RegisterNamedService("set:sys", std::make_shared<ISystemSettingsServer>(system), 60);
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::Set
