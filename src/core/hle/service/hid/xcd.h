// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::HID {

class XCD_SYS final : public ServiceFramework<XCD_SYS> {
public:
    explicit XCD_SYS(Core::System& system_);
    ~XCD_SYS() override;

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static const auto functions = CreateStaticMap(
        FunctionInfo{0, nullptr, "GetDataFormat"},
        FunctionInfo{1, nullptr, "SetDataFormat"},
        FunctionInfo{2, nullptr, "GetMcuState"},
        FunctionInfo{3, nullptr, "SetMcuState"},
        FunctionInfo{4, nullptr, "GetMcuVersionForNfc"},
        FunctionInfo{5, nullptr, "CheckNfcDevicePower"},
        FunctionInfo{10, nullptr, "SetNfcEvent"},
        FunctionInfo{11, nullptr, "GetNfcInfo"},
        FunctionInfo{12, nullptr, "StartNfcDiscovery"},
        FunctionInfo{13, nullptr, "StopNfcDiscovery"},
        FunctionInfo{14, nullptr, "StartNtagRead"},
        FunctionInfo{15, nullptr, "StartNtagWrite"},
        FunctionInfo{16, nullptr, "SendNfcRawData"},
        FunctionInfo{17, nullptr, "RegisterMifareKey"},
        FunctionInfo{18, nullptr, "ClearMifareKey"},
        FunctionInfo{19, nullptr, "StartMifareRead"},
        FunctionInfo{20, nullptr, "StartMifareWrite"},
        FunctionInfo{101, nullptr, "GetAwakeTriggerReasonForLeftRail"},
        FunctionInfo{102, nullptr, "GetAwakeTriggerReasonForRightRail"},
        FunctionInfo{103, nullptr, "GetAwakeTriggerBatteryLevelTransitionForLeftRail"},
        FunctionInfo{104, nullptr, "GetAwakeTriggerBatteryLevelTransitionForRightRail"}
    );
};

} // namespace Service::HID
