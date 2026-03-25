// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <memory>
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/ulsf/ulsf.h"
#include "core/hle/service/server_manager.h"

namespace Service::ULSF {

ULSF_U::ULSF_U(Core::System& system_) : ServiceFramework{system_, "ulsf:u"} {
    static const FunctionInfo functions[] = {
        {0, &ULSF_U::GetVersion, "GetVersion"},
    };
    RegisterHandlers(functions);
}
ULSF_U::~ULSF_U() = default;

// Result ULSF_U::GetVersion(Out<ULauncherVersion> out_version) {
//     LOG_WARNING(Service_SM, "(stubbed)");
//     *out_version = {};
//     R_SUCCEED();
// }

void ULSF_U::GetVersion(HLERequestContext& ctx) {
    LOG_WARNING(Service_SM, "(stubbed)");
    ULauncherVersion version{1, 6, 7};
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
    rb.PushRaw(version);
}

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);
    server_manager->RegisterNamedService("ulsf:u", std::make_shared<ULSF_U>(system));
    ServerManager::RunServer(std::move(server_manager));
}

}
