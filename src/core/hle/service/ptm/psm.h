// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Service::PTM {

class PSM final : public ServiceFramework<PSM> {
public:
    explicit PSM(Core::System& system_);
    ~PSM() override;

private:
    enum class ChargerType : u32 {
        Unplugged = 0,
        RegularCharger = 1,
        LowPowerCharger = 2,
        Unknown = 3,
    };
    void GetBatteryChargePercentage(HLERequestContext& ctx);
    void GetChargerType(HLERequestContext& ctx);
    void OpenSession(HLERequestContext& ctx);
    void GetBatteryVoltageState(HLERequestContext& ctx);
    void GetBatteryAgePercentage(HLERequestContext& ctx);
    void GetBatteryChargeInfoFields(HLERequestContext& ctx);

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, &PSM::GetBatteryChargePercentage, "GetBatteryChargePercentage"},
        FunctionInfo{1, &PSM::GetChargerType, "GetChargerType"},
        FunctionInfo{2, nullptr, "EnableBatteryCharging"},
        FunctionInfo{3, nullptr, "DisableBatteryCharging"},
        FunctionInfo{4, nullptr, "IsBatteryChargingEnabled"},
        FunctionInfo{5, nullptr, "AcquireControllerPowerSupply"},
        FunctionInfo{6, nullptr, "ReleaseControllerPowerSupply"},
        FunctionInfo{7, &PSM::OpenSession, "OpenSession"},
        FunctionInfo{8, nullptr, "EnableEnoughPowerChargeEmulation"},
        FunctionInfo{9, nullptr, "DisableEnoughPowerChargeEmulation"},
        FunctionInfo{10, nullptr, "EnableFastBatteryCharging"},
        FunctionInfo{11, nullptr, "DisableFastBatteryCharging"},
        FunctionInfo{12, &PSM::GetBatteryVoltageState, "GetBatteryVoltageState"},
        FunctionInfo{13, nullptr, "GetRawBatteryChargePercentage"},
        FunctionInfo{14, nullptr, "IsEnoughPowerSupplied"},
        FunctionInfo{15, &PSM::GetBatteryAgePercentage, "GetBatteryAgePercentage"},
        FunctionInfo{16, nullptr, "GetBatteryChargeInfoEvent"},
        FunctionInfo{17, &PSM::GetBatteryChargeInfoFields, "GetBatteryChargeInfoFields"},
        FunctionInfo{18, nullptr, "GetBatteryChargeCalibratedEvent"}
    );
};

} // namespace Service::PTM
