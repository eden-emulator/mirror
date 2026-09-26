// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::Set {

class IFactorySettingsServer final : public ServiceFramework<IFactorySettingsServer> {
public:
    explicit IFactorySettingsServer(Core::System& system_);
    ~IFactorySettingsServer() override;

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, nullptr, "GetBluetoothBdAddress"},
        FunctionInfo{1, nullptr, "GetConfigurationId1"},
        FunctionInfo{2, nullptr, "GetAccelerometerOffset"},
        FunctionInfo{3, nullptr, "GetAccelerometerScale"},
        FunctionInfo{4, nullptr, "GetGyroscopeOffset"},
        FunctionInfo{5, nullptr, "GetGyroscopeScale"},
        FunctionInfo{6, nullptr, "GetWirelessLanMacAddress"},
        FunctionInfo{7, nullptr, "GetWirelessLanCountryCodeCount"},
        FunctionInfo{8, nullptr, "GetWirelessLanCountryCodes"},
        FunctionInfo{9, nullptr, "GetSerialNumber"},
        FunctionInfo{10, nullptr, "SetInitialSystemAppletProgramId"},
        FunctionInfo{11, nullptr, "SetOverlayDispProgramId"},
        FunctionInfo{12, nullptr, "GetBatteryLot"},
        FunctionInfo{14, nullptr, "GetEciDeviceCertificate"},
        FunctionInfo{15, nullptr, "GetEticketDeviceCertificate"},
        FunctionInfo{16, nullptr, "GetSslKey"},
        FunctionInfo{17, nullptr, "GetSslCertificate"},
        FunctionInfo{18, nullptr, "GetGameCardKey"},
        FunctionInfo{19, nullptr, "GetGameCardCertificate"},
        FunctionInfo{20, nullptr, "GetEciDeviceKey"},
        FunctionInfo{21, nullptr, "GetEticketDeviceKey"},
        FunctionInfo{22, nullptr, "GetSpeakerParameter"},
        FunctionInfo{23, nullptr, "GetLcdVendorId"},
        FunctionInfo{24, nullptr, "GetEciDeviceCertificate2"},
        FunctionInfo{25, nullptr, "GetEciDeviceKey2"},
        FunctionInfo{26, nullptr, "GetAmiiboKey"},
        FunctionInfo{27, nullptr, "GetAmiiboEcqvCertificate"},
        FunctionInfo{28, nullptr, "GetAmiiboEcdsaCertificate"},
        FunctionInfo{29, nullptr, "GetAmiiboEcqvBlsKey"},
        FunctionInfo{30, nullptr, "GetAmiiboEcqvBlsCertificate"},
        FunctionInfo{31, nullptr, "GetAmiiboEcqvBlsRootCertificate"},
        FunctionInfo{32, nullptr, "GetUsbTypeCPowerSourceCircuitVersion"},
        FunctionInfo{33, nullptr, "GetAnalogStickModuleTypeL"},
        FunctionInfo{34, nullptr, "GetAnalogStickModelParameterL"},
        FunctionInfo{35, nullptr, "GetAnalogStickFactoryCalibrationL"},
        FunctionInfo{36, nullptr, "GetAnalogStickModuleTypeR"},
        FunctionInfo{37, nullptr, "GetAnalogStickModelParameterR"},
        FunctionInfo{38, nullptr, "GetAnalogStickFactoryCalibrationR"},
        FunctionInfo{39, nullptr, "GetConsoleSixAxisSensorModuleType"},
        FunctionInfo{40, nullptr, "GetConsoleSixAxisSensorHorizontalOffset"},
        FunctionInfo{41, nullptr, "GetBatteryVersion"},
        FunctionInfo{42, nullptr, "GetDeviceId"},
        FunctionInfo{43, nullptr, "GetConsoleSixAxisSensorMountType"}
    );
};

} // namespace Service::Set
