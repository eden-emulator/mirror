// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>

#include "core/hle/service/pcie/pcie.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"
#include "frontend_common/firmware_manager.h"

namespace Service::PCIe {

class ISession final : public ServiceFramework<ISession> {
public:
    explicit ISession(Core::System& system_) : ServiceFramework{system_, "ISession"} {}

    static constexpr auto functions = CreateStaticMap(
            FunctionInfo{0, nullptr, "QueryFunctions"},
            FunctionInfo{1, nullptr, "AcquireFunction"},
            FunctionInfo{2, nullptr, "ReleaseFunction"},
            FunctionInfo{3, nullptr, "GetFunctionState"},
            FunctionInfo{4, nullptr, "GetBarProfile"},
            FunctionInfo{5, nullptr, "ReadConfig"},
            FunctionInfo{6, nullptr, "WriteConfig"},
            FunctionInfo{7, nullptr, "ReadBarRegion"},
            FunctionInfo{8, nullptr, "WriteBarRegion"},
            FunctionInfo{9, nullptr, "FindCapability"},
            FunctionInfo{10, nullptr, "FindExtendedCapability"},
            FunctionInfo{11, nullptr, "MapDma"},
            FunctionInfo{12, nullptr, "UnmapDma"},
            FunctionInfo{13, nullptr, "UnmapDmaBusAddress"},
            FunctionInfo{14, nullptr, "GetDmaBusAddress"},
            FunctionInfo{15, nullptr, "GetDmaBusAddressRange"},
            FunctionInfo{16, nullptr, "SetDmaEnable"},
            FunctionInfo{17, nullptr, "AcquireIrq"},
            FunctionInfo{18, nullptr, "ReleaseIrq"},
            FunctionInfo{19, nullptr, "SetIrqEnable"},
            FunctionInfo{20, nullptr, "GetIrqEvent"},
            FunctionInfo{21, nullptr, "SetAspmEnable"},
            FunctionInfo{22, nullptr, "SetResetUponResumeEnable"},
            FunctionInfo{23, nullptr, "ResetFunction"}
        );
    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
};

class PCIE final : public ServiceFramework<PCIE> {
public:
    explicit PCIE(Core::System& system_) : ServiceFramework{system_, "pcie"} {}

    static constexpr auto functions = CreateStaticMap(
            FunctionInfo{0, nullptr, "RegisterClassDriver"},
            FunctionInfo{1, nullptr, "QueryFunctionsUnregistered"}
        );
    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
};

class PCIE_LOG final : public ServiceFramework<PCIE_LOG> {
public:
    explicit PCIE_LOG(Core::System& system_) : ServiceFramework{system_, "pcie:log"} {}

    static constexpr auto functions = CreateStaticMap(
            FunctionInfo{0, nullptr, "GetLoggedState"},
            FunctionInfo{1, nullptr, "GetLoggedStateEvent"}
        );
    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("pcie", std::make_shared<PCIE>(system));
    // +6.0.0
    if (FirmwareManager::GetFirmwareVersion(system).first.major >= 6) {
        server_manager->RegisterNamedService("pcie:log", std::make_shared<PCIE_LOG>(system));
    }
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::PCIe
