// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/ns/develop_interface.h"
#include "core/hle/service/ns/ns.h"
#include "core/hle/service/ns/platform_service_manager.h"
#include "core/hle/service/ns/query_service.h"
#include "core/hle/service/ns/service_getter_interface.h"
#include "core/hle/service/ns/system_update_interface.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/server_manager.h"

namespace Service::NS {

class INotifyService final : public ServiceFramework<INotifyService> {
public:
    explicit INotifyService(Core::System& system_) : ServiceFramework{system_, "pdm:ntfy"} {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key,
            FunctionInfo{0, nullptr, "NotifyAppletEvent" },
            FunctionInfo{2, nullptr, "NotifyOperationModeChangeEvent" },
            FunctionInfo{3, nullptr, "NotifyPowerStateChangeEvent" },
            FunctionInfo{4, nullptr, "NotifyClearAllEvent" },
            FunctionInfo{5, nullptr, "NotifyEventForDebug" },
            FunctionInfo{6, nullptr, "SuspendUserAccountEventService" },
            FunctionInfo{7, nullptr, "ResumeUserAccountEventService" },
            FunctionInfo{8, nullptr, "NotifyLibraryAppletEvent" },
            FunctionInfo{9, nullptr, "Cmd9" },
            FunctionInfo{20, nullptr, "Cmd20" },
            FunctionInfo{30, nullptr, "Cmd30" },
            FunctionInfo{100, nullptr, "Cmd100" },
            FunctionInfo{101, nullptr, "Cmd101" }
        );
    }
};

class IVulnerabilityManagerInterface final
    : public ServiceFramework<IVulnerabilityManagerInterface> {
public:
    explicit IVulnerabilityManagerInterface(Core::System& system_)
        : ServiceFramework{system_, "ns:vm"} {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key,
            FunctionInfo{1200, D<&IVulnerabilityManagerInterface::NeedsUpdateVulnerability>, "NeedsUpdateVulnerability"},
            FunctionInfo{1201, nullptr, "UpdateSafeSystemVersionForDebug"},
            FunctionInfo{1202, nullptr, "GetSafeSystemVersion"},
            FunctionInfo{3100, D<&IVulnerabilityManagerInterface::GetSafeSystemVersionCheckInfo>, "GetSafeSystemVersionCheckInfo"},
            FunctionInfo{3101, nullptr, "RequestUpdateSafeSystemVersionCheckInfo"},
            FunctionInfo{3102, D<&IVulnerabilityManagerInterface::ResetSafeSystemVersionCheckInfo>, "ResetSafeSystemVersionCheckInfo"}
        );
    }
    ~IVulnerabilityManagerInterface() override = default;

    Result NeedsUpdateVulnerability(Out<bool> out_needs_update_vulnerability) {
        LOG_WARNING(Service_NS, "(STUBBED)");
        *out_needs_update_vulnerability = false;
        R_SUCCEED();
    }

    Result GetSafeSystemVersionCheckInfo(Out<std::array<u8, 0x20>> out_system_version_check_info) {
        LOG_WARNING(Service_NS, "(STUBBED)");
        *out_system_version_check_info = {};
        R_SUCCEED();
    }

    Result ResetSafeSystemVersionCheckInfo() {
        LOG_WARNING(Service_NS, "(STUBBED)");
        R_SUCCEED();
    }
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("ns:am2", std::make_shared<IServiceGetterInterface>(system, "ns:am2"), 5);
    server_manager->RegisterNamedService("ns:ec", std::make_shared<IServiceGetterInterface>(system, "ns:ec"), 5);
    server_manager->RegisterNamedService("ns:rid", std::make_shared<IServiceGetterInterface>(system, "ns:rid"), 5);
    server_manager->RegisterNamedService("ns:rt", std::make_shared<IServiceGetterInterface>(system, "ns:rt"), 5);
    server_manager->RegisterNamedService("ns:web", std::make_shared<IServiceGetterInterface>(system, "ns:web"), 5);
    server_manager->RegisterNamedService("ns:ro", std::make_shared<IServiceGetterInterface>(system, "ns:ro"), 5);

    server_manager->RegisterNamedService("ns:dev", std::make_shared<IDevelopInterface>(system), 5);
    server_manager->RegisterNamedService("ns:su", std::make_shared<ISystemUpdateInterface>(system), 5);
    server_manager->RegisterNamedService("ns:vm", std::make_shared<IVulnerabilityManagerInterface>(system), 5);
    server_manager->RegisterNamedService("pdm:ntfy", std::make_shared<INotifyService>(system));
    server_manager->RegisterNamedService("pdm:qry", std::make_shared<IQueryService>(system));

    server_manager->RegisterNamedService("pl:s", std::make_shared<IPlatformServiceManager>(system, "pl:s"));
    server_manager->RegisterNamedService("pl:u", std::make_shared<IPlatformServiceManager>(system, "pl:u"));
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::NS
