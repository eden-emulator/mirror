// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::Glue {

class BGTC_T final : public ServiceFramework<BGTC_T> {
public:
    explicit BGTC_T(Core::System& system_);
    ~BGTC_T() override;

    void OpenTaskService(HLERequestContext& ctx);
    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{100, &BGTC_T::OpenTaskService, "OpenTaskService"}
    );
};

class ITaskService final : public ServiceFramework<ITaskService> {
public:
    explicit ITaskService(Core::System& system_);
    ~ITaskService() override;

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{1, nullptr, "NotifyTaskStarting"},
        FunctionInfo{2, nullptr, "NotifyTaskFinished"},
        FunctionInfo{3, nullptr, "GetTriggerEvent"},
        FunctionInfo{4, nullptr, "IsInHalfAwake"},
        FunctionInfo{5, nullptr, "NotifyClientName"},
        FunctionInfo{6, nullptr, "IsInFullAwake"},
        FunctionInfo{11, nullptr, "ScheduleTask"},
        FunctionInfo{12, nullptr, "GetScheduledTaskInterval"},
        FunctionInfo{13, nullptr, "UnscheduleTask"},
        FunctionInfo{14, nullptr, "GetScheduleEvent"},
        FunctionInfo{15, nullptr, "SchedulePeriodicTask"},
        FunctionInfo{16, nullptr, "Unknown16"},
        FunctionInfo{101, nullptr, "GetOperationMode"},
        FunctionInfo{102, nullptr, "WillDisconnectNetworkWhenEnteringSleep"},
        FunctionInfo{103, nullptr, "WillStayHalfAwakeInsteadSleep"},
        FunctionInfo{200, nullptr, "Unknown200"}
    );
};

class BGTC_SC final : public ServiceFramework<BGTC_SC> {
public:
    explicit BGTC_SC(Core::System& system_);
    ~BGTC_SC() override;

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{1, nullptr, "GetState"},
        FunctionInfo{2, nullptr, "GetStateChangedEvent"},
        FunctionInfo{3, nullptr, "NotifyEnteringHalfAwake"},
        FunctionInfo{4, nullptr, "NotifyLeavingHalfAwake"},
        FunctionInfo{5, nullptr, "SetIsUsingSleepUnsupportedDevices"}
    );
};

} // namespace Service::Glue
