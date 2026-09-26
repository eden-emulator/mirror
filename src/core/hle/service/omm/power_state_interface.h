// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::OMM {

class IPowerStateInterface final : public ServiceFramework<IPowerStateInterface> {
public:
    explicit IPowerStateInterface(Core::System& system_);
    ~IPowerStateInterface() override;

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, nullptr, "GetState"},
        FunctionInfo{1, nullptr, "EnterSleep"},
        FunctionInfo{2, nullptr, "GetLastWakeReason"},
        FunctionInfo{3, nullptr, "Shutdown"},
        FunctionInfo{4, nullptr, "GetNotificationMessageEventHandle"},
        FunctionInfo{5, nullptr, "ReceiveNotificationMessage"},
        FunctionInfo{6, nullptr, "AnalyzeLogForLastSleepWakeSequence"},
        FunctionInfo{7, nullptr, "ResetEventLog"},
        FunctionInfo{8, nullptr, "AnalyzePerformanceLogForLastSleepWakeSequence"},
        FunctionInfo{9, nullptr, "ChangeHomeButtonLongPressingTime"},
        FunctionInfo{10, nullptr, "PutErrorState"},
        FunctionInfo{11, nullptr, "InvalidateCurrentHomeButtonPressing"}
    );
};

} // namespace Service::OMM
