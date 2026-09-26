// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::BTM {

class IBtmDebug final : public ServiceFramework<IBtmDebug> {
public:
    explicit IBtmDebug(Core::System& system_);
    ~IBtmDebug() override;

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, nullptr, "AcquireDiscoveryEvent"},
        FunctionInfo{1, nullptr, "StartDiscovery"},
        FunctionInfo{2, nullptr, "CancelDiscovery"},
        FunctionInfo{3, nullptr, "GetDeviceProperty"},
        FunctionInfo{4, nullptr, "CreateBond"},
        FunctionInfo{5, nullptr, "CancelBond"},
        FunctionInfo{6, nullptr, "SetTsiMode"},
        FunctionInfo{7, nullptr, "GeneralTest"},
        FunctionInfo{8, nullptr, "HidConnect"},
        FunctionInfo{9, nullptr, "GeneralGet"}, //5.0.0+
        FunctionInfo{10, nullptr, "GetGattClientDisconnectionReason"}, //5.0.0+
        FunctionInfo{11, nullptr, "GetBleConnectionParameter"}, //5.1.0+
        FunctionInfo{12, nullptr, "GetBleConnectionParameterRequest"}, //5.1.0+
        FunctionInfo{13, nullptr, "GetDiscoveredDevice"}, //12.0.0+
        FunctionInfo{14, nullptr, "SleepAwakeLoopTest"}, //15.0.0+
        FunctionInfo{15, nullptr, "SleepTest"}, //15.0.0+
        FunctionInfo{16, nullptr, "MinimumAwakeTest"}, //15.0.0+
        FunctionInfo{17, nullptr, "ForceEnableBtm"}, //15.0.0+
    );
};

} // namespace Service::BTM
