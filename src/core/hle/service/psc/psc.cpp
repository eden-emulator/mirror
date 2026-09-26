// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/psc/ovln/receiver_service.h"
#include "core/hle/service/psc/ovln/sender_service.h"
#include "core/hle/service/psc/pm_control.h"
#include "core/hle/service/psc/pm_module.h"
#include "core/hle/service/psc/psc.h"
#include "core/hle/service/psc/time/manager.h"
#include "core/hle/service/psc/time/power_state_service.h"
#include "core/hle/service/psc/time/service_manager.h"
#include "core/hle/service/psc/time/static.h"
#include "core/hle/service/service.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::PSC {

class PSC_L final : public ServiceFramework<PSC_L> {
public:
    explicit PSC_L(Core::System& system_) : ServiceFramework{system_, "psc:l"} {}

    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, nullptr, "Initialize_3"},
        FunctionInfo{1, nullptr, "Lock"},
        FunctionInfo{2, nullptr, "Unlock"},
        FunctionInfo{3, nullptr, "IsLocked"},
        FunctionInfo{4, nullptr, "GetRelatedState"}
    );
    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
};

class INS_R final : public ServiceFramework<INS_R> {
public:
    explicit INS_R(Core::System& system_) : ServiceFramework{system_, "ins:r"} {}

    static constexpr auto functions = CreateStaticMap(
            FunctionInfo{0, nullptr, "GetInputSourceState"},
            FunctionInfo{1, nullptr, "GetTriggerTargetEvent"}
        );
    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
};

class INS_S final : public ServiceFramework<INS_S> {
public:
    explicit INS_S(Core::System& system_) : ServiceFramework{system_, "ins:s"} {}

    static constexpr auto functions = CreateStaticMap(
            FunctionInfo{0, nullptr, "GetNotifyEvent"}
        );
    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
};

class HSHL_SYS final : public ServiceFramework<HSHL_SYS> {
public:
    explicit HSHL_SYS(Core::System& system_) : ServiceFramework{system_, "hshl:sys"} {}

    static constexpr auto functions = CreateStaticMap(
            FunctionInfo{0, nullptr, "GetBatteryPercentage"},
            FunctionInfo{1, nullptr, "GetChargerType"},
            FunctionInfo{2, nullptr, "OpenChargeSession"},
            FunctionInfo{3, nullptr, "GetRawBatteryPercentage"},
            FunctionInfo{4, nullptr, "GetBatteryVoltageLevel"},
            FunctionInfo{5, nullptr, "OpenThermalSession"},
            FunctionInfo{6, nullptr, "GetAbnormalTemperatureSet"},
            FunctionInfo{7, nullptr, "OpenClockSession"},
            FunctionInfo{8, nullptr, "GetClockRate"},
            FunctionInfo{9, nullptr, "OpenBridgeSession"},
            FunctionInfo{10, nullptr, "GetBridgePowerSupply"},
            FunctionInfo{11, nullptr, "OpenVsysVoltageSession"},
            FunctionInfo{12, nullptr, "GetIsBatteryEnoughForFullAwake"},
            FunctionInfo{13, nullptr, "GetIsCharging"},
            FunctionInfo{14, nullptr, "Cmd14"},
            FunctionInfo{15, nullptr, "Cmd15"}
        );
    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
};

class HSHL_SET final : public ServiceFramework<HSHL_SET> {
public:
    explicit HSHL_SET(Core::System& system_) : ServiceFramework{system_, "hshl:set"} {}

    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, nullptr, "OpenChargeSession_2"},
        FunctionInfo{1, nullptr, "OpenThermalSession_2"},
        FunctionInfo{2, nullptr, "SetClockRate"},
        FunctionInfo{3, nullptr, "SetBridgePowerSupply"},
        FunctionInfo{4, nullptr, "Cmd4"},
        FunctionInfo{5, nullptr, "Cmd5"}
    );
    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
};

class IPmModule;

class IPmService final : public ServiceFramework<IPmService> {
public:
    explicit IPmService(Core::System& system_) : ServiceFramework{system_, "psc:m"} {}
    ~IPmService() override = default;

private:
    Result GetPmModule(Out<SharedPointer<IPmModule>> out_module) {
        LOG_DEBUG(Service_PSC, "called");
        *out_module = std::make_shared<IPmModule>(system);
        R_SUCCEED();
    }

    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, D<&IPmService::GetPmModule>, "GetPmModule"}
    );
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("psc:c", std::make_shared<IPmControl>(system));
    server_manager->RegisterNamedService("psc:m", std::make_shared<IPmService>(system));
    server_manager->RegisterNamedService("psc:l", std::make_shared<PSC_L>(system));
    server_manager->RegisterNamedService("ins:r", std::make_shared<INS_R>(system));
    server_manager->RegisterNamedService("ins:s", std::make_shared<INS_S>(system));
    server_manager->RegisterNamedService("hshl:sys", std::make_shared<HSHL_SYS>(system));
    server_manager->RegisterNamedService("hshl:set", std::make_shared<HSHL_SET>(system));
    server_manager->RegisterNamedService("ovln:rcv", std::make_shared<IReceiverService>(system));
    server_manager->RegisterNamedService("ovln:snd", std::make_shared<ISenderService>(system));

    auto time = std::make_shared<Time::TimeManager>(system);

    server_manager->RegisterNamedService("time:m", std::make_shared<Time::ServiceManager>(system, time, server_manager.get()));
    server_manager->RegisterNamedService("time:su", std::make_shared<Time::StaticService>(system, Time::StaticServiceSetupInfo{0, 0, 0, 0, 0, 1}, time, "time:su"));
    server_manager->RegisterNamedService("time:al", std::make_shared<Time::IAlarmService>(system, time));

    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::PSC
