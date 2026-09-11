// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/bcat/backend/backend.h"
#include "core/hle/service/bcat/bcat.h"
#include "core/hle/service/bcat/news/service_creator.h"
#include "core/hle/service/bcat/service_creator.h"
#include "core/hle/service/server_manager.h"

namespace Service::BCAT {

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("bcat:a", std::make_shared<IServiceCreator>(system, "bcat:a"), 32);
    server_manager->RegisterNamedService("bcat:m", std::make_shared<IServiceCreator>(system, "bcat:m"), 32);
    server_manager->RegisterNamedService("bcat:u", std::make_shared<IServiceCreator>(system, "bcat:u"), 32);
    server_manager->RegisterNamedService("bcat:s", std::make_shared<IServiceCreator>(system, "bcat:s"), 32);

    server_manager->RegisterNamedService("news:a", std::make_shared<News::IServiceCreator>(system, 0xffffffff, "news:a"), 32);
    server_manager->RegisterNamedService("news:p", std::make_shared<News::IServiceCreator>(system, 0x1, "news:p"), 32);
    server_manager->RegisterNamedService("news:c", std::make_shared<News::IServiceCreator>(system, 0x2, "news:c"), 32);
    server_manager->RegisterNamedService("news:v", std::make_shared<News::IServiceCreator>(system, 0x4, "news:v"), 32);
    server_manager->RegisterNamedService("news:m", std::make_shared<News::IServiceCreator>(system, 0xd, "news:m"), 32);

    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::BCAT
