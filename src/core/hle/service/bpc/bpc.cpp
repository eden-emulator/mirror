// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>

#include "core/hle/service/bpc/bpc.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"

namespace Service::BPC {

class BPC final : public ServiceFramework<BPC> {
public:
    explicit BPC(Core::System& system_) : ServiceFramework{system_, "bpc"} {}

    static const auto functions = CreateStaticMap(
            FunctionInfo{0, nullptr, "ShutdownSystem"},
            FunctionInfo{1, nullptr, "RebootSystem"},
            FunctionInfo{2, nullptr, "GetWakeupReason"},
            FunctionInfo{3, nullptr, "GetShutdownReason"},
            FunctionInfo{4, nullptr, "GetAcOk"},
            FunctionInfo{5, nullptr, "GetBoardPowerControlEvent"},
            FunctionInfo{6, nullptr, "GetSleepButtonState"},
            FunctionInfo{7, nullptr, "GetPowerEvent"},
            FunctionInfo{8, nullptr, "CreateWakeupTimer"},
            FunctionInfo{9, nullptr, "CancelWakeupTimer"},
            FunctionInfo{10, nullptr, "EnableWakeupTimerOnDevice"},
            FunctionInfo{11, nullptr, "CreateWakeupTimerEx"},
            FunctionInfo{12, nullptr, "GetLastEnabledWakeupTimerType"},
            FunctionInfo{13, nullptr, "CleanAllWakeupTimers"},
            FunctionInfo{14, nullptr, "GetPowerButton"},
            FunctionInfo{15, nullptr, "SetEnableWakeupTimer"}
        );
    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
};

class BPC_R final : public ServiceFramework<BPC_R> {
public:
    explicit BPC_R(Core::System& system_) : ServiceFramework{system_, "bpc:r"} {}

    static const auto functions = CreateStaticMap(
            FunctionInfo{0, nullptr, "GetRtcTime"},
            FunctionInfo{1, nullptr, "SetRtcTime"},
            FunctionInfo{2, nullptr, "GetRtcResetDetected"},
            FunctionInfo{3, nullptr, "ClearRtcResetDetected"},
            FunctionInfo{4, nullptr, "SetUpRtcResetOnShutdown"}
        );
    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
};

class BPC_C final : public ServiceFramework<BPC_C> {
public:
    explicit BPC_C(Core::System& system_) : ServiceFramework{system_, "bpc:c"} {}

    static const auto functions = CreateStaticMap(
            FunctionInfo{0, nullptr, "ShutdownSystem"},
            FunctionInfo{1, nullptr, "RebootSystem"},
            FunctionInfo{2, nullptr, "GetWakeupReason"},
            FunctionInfo{3, nullptr, "GetShutdownReason"},
            FunctionInfo{4, nullptr, "GetAcOk"},
            FunctionInfo{5, nullptr, "GetPowerEvent"}
        );
    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
};

class BPC_B final : public ServiceFramework<BPC_B> {
public:
    explicit BPC_B(Core::System& system_) : ServiceFramework{system_, "bpc:b"} {}

    static const auto functions = CreateStaticMap(
            FunctionInfo{0, nullptr, "GetSleepButtonState"},
            FunctionInfo{1, nullptr, "GetPowerButtonEvent"}
        );
    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
};

class BPC_W final : public ServiceFramework<BPC_W> {
public:
    explicit BPC_W(Core::System& system_) : ServiceFramework{system_, "bpc:w"} {}

    static const auto functions = CreateStaticMap(
            FunctionInfo{0, nullptr, "CreateWakeupTimer"},
            FunctionInfo{1, nullptr, "CancelWakeupTimer"},
            FunctionInfo{2, nullptr, "EnableWakeupTimerOnDevice"}
        );
    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
};

class BPC_AMS final : public ServiceFramework<BPC_AMS> {
public:
    explicit BPC_AMS(Core::System& system_) : ServiceFramework{system_, "bpc:ams"} {}

    static const auto functions = CreateStaticMap(
            FunctionInfo{65000, nullptr, "RebootToFatalError"},
            FunctionInfo{65001, nullptr, "SetRebootPayload"}
        );
    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("bpc", std::make_shared<BPC>(system), 13);
    server_manager->RegisterNamedService("bpc:r", std::make_shared<BPC_R>(system), 13);
    server_manager->RegisterNamedService("bpc:c", std::make_shared<BPC_C>(system), 13);
    server_manager->RegisterNamedService("bpc:b", std::make_shared<BPC_B>(system), 13);
    server_manager->RegisterNamedService("bpc:w", std::make_shared<BPC_W>(system), 13);
    server_manager->RegisterNamedService("bpc:ams", std::make_shared<BPC_AMS>(system), 4);
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::BPC
