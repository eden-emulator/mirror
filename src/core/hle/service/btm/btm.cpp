// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>

#include "core/hle/service/btm/btm.h"
#include "core/hle/service/btm/btm_debug.h"
#include "core/hle/service/btm/btm_system.h"
#include "core/hle/service/btm/btm_user.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"

namespace Service::BTM {

class IBtm final : public ServiceFramework<IBtm> {
public:
    explicit IBtm(Core::System& system_) : ServiceFramework{system_, "btm"} {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key,
            FunctionInfo{0, nullptr, "GetState"},
            FunctionInfo{1, nullptr, "GetHostDeviceProperty"},
            FunctionInfo{2, nullptr, "AcquireDeviceConditionEvent"},
            FunctionInfo{3, nullptr, "GetDeviceCondition"},
            FunctionInfo{4, nullptr, "SetBurstMode"},
            FunctionInfo{5, nullptr, "SetSlotMode"},
            FunctionInfo{6, nullptr, "SetBluetoothMode"},
            FunctionInfo{7, nullptr, "SetWlanMode"},
            FunctionInfo{8, nullptr, "AcquireDeviceInfoEvent"},
            FunctionInfo{9, nullptr, "GetDeviceInfo"},
            FunctionInfo{10, nullptr, "AddDeviceInfo"},
            FunctionInfo{11, nullptr, "RemoveDeviceInfo"},
            FunctionInfo{12, nullptr, "IncreaseDeviceInfoOrder"},
            FunctionInfo{13, nullptr, "LlrNotify"},
            FunctionInfo{14, nullptr, "EnableRadio"},
            FunctionInfo{15, nullptr, "DisableRadio"},
            FunctionInfo{16, nullptr, "HidDisconnect"},
            FunctionInfo{17, nullptr, "HidSetRetransmissionMode"},
            FunctionInfo{18, nullptr, "AcquireAwakeReqEvent"},
            FunctionInfo{19, nullptr, "AcquireLlrStateEvent"},
            FunctionInfo{20, nullptr, "IsLlrStarted"},
            FunctionInfo{21, nullptr, "EnableSlotSaving"},
            FunctionInfo{22, nullptr, "ProtectDeviceInfo"},
            FunctionInfo{23, nullptr, "AcquireBleScanEvent"},
            FunctionInfo{24, nullptr, "GetBleScanParameterGeneral"},
            FunctionInfo{25, nullptr, "GetBleScanParameterSmartDevice"},
            FunctionInfo{26, nullptr, "StartBleScanForGeneral"},
            FunctionInfo{27, nullptr, "StopBleScanForGeneral"},
            FunctionInfo{28, nullptr, "GetBleScanResultsForGeneral"},
            FunctionInfo{29, nullptr, "StartBleScanForPairedDevice"},
            FunctionInfo{30, nullptr, "StopBleScanForPairedDevice"},
            FunctionInfo{31, nullptr, "StartBleScanForSmartDevice"},
            FunctionInfo{32, nullptr, "StopBleScanForSmartDevice"},
            FunctionInfo{33, nullptr, "GetBleScanResultsForSmartDevice"},
            FunctionInfo{34, nullptr, "AcquireBleConnectionEvent"},
            FunctionInfo{35, nullptr, "BleConnect"},
            FunctionInfo{36, nullptr, "BleOverrideConnection"},
            FunctionInfo{37, nullptr, "BleDisconnect"},
            FunctionInfo{38, nullptr, "BleGetConnectionState"},
            FunctionInfo{39, nullptr, "BleGetGattClientConditionList"},
            FunctionInfo{40, nullptr, "AcquireBlePairingEvent"},
            FunctionInfo{41, nullptr, "BlePairDevice"},
            FunctionInfo{42, nullptr, "BleUnpairDeviceOnBoth"},
            FunctionInfo{43, nullptr, "BleUnpairDevice"},
            FunctionInfo{44, nullptr, "BleGetPairedAddresses"},
            FunctionInfo{45, nullptr, "AcquireBleServiceDiscoveryEvent"},
            FunctionInfo{46, nullptr, "GetGattServices"},
            FunctionInfo{47, nullptr, "GetGattService"},
            FunctionInfo{48, nullptr, "GetGattIncludedServices"},
            FunctionInfo{49, nullptr, "GetBelongingService"},
            FunctionInfo{50, nullptr, "GetGattCharacteristics"},
            FunctionInfo{51, nullptr, "GetGattDescriptors"},
            FunctionInfo{52, nullptr, "AcquireBleMtuConfigEvent"},
            FunctionInfo{53, nullptr, "ConfigureBleMtu"},
            FunctionInfo{54, nullptr, "GetBleMtu"},
            FunctionInfo{55, nullptr, "RegisterBleGattDataPath"},
            FunctionInfo{56, nullptr, "UnregisterBleGattDataPath"},
            FunctionInfo{57, nullptr, "RegisterAppletResourceUserId"},
            FunctionInfo{58, nullptr, "UnregisterAppletResourceUserId"},
            FunctionInfo{59, nullptr, "SetAppletResourceUserId"},
            FunctionInfo{60, nullptr, "AcquireBleConnectionParameterUpdateEvent"}, //8.0.0+
            FunctionInfo{61, nullptr, "SetCeLength"}, //8.0.0+
            FunctionInfo{62, nullptr, "EnsureSlotExpansion"}, //9.0.0+
            FunctionInfo{63, nullptr, "IsSlotExpansionEnsured"}, //9.0.0+
            FunctionInfo{64, nullptr, "CancelConnectionTrigger"}, //10.0.0+
            FunctionInfo{65, nullptr, "GetConnectionCapacity"}, //13.0.0+
            FunctionInfo{66, nullptr, "GetWlanMode"}, //13.0.0+
            FunctionInfo{67, nullptr, "IsSlotSavingEnabled"}, //13.0.0+
            FunctionInfo{68, nullptr, "IsSlotSavingForPairingEnabled"}, //13.0.0+
            FunctionInfo{69, nullptr, "AcquireAudioDeviceConnectionEvent"}, //13.0.0+
            FunctionInfo{70, nullptr, "GetConnectedAudioDevices"}, //13.0.0+
            FunctionInfo{71, nullptr, "SetAudioSourceVolume"}, //13.0.0+
            FunctionInfo{72, nullptr, "GetAudioSourceVolume"}, //13.0.0+
            FunctionInfo{73, nullptr, "RequestAudioDeviceConnectionRejection"}, //13.0.0+
            FunctionInfo{74, nullptr, "CancelAudioDeviceConnectionRejection"}, //13.0.0+
            FunctionInfo{75, nullptr, "GetPairedAudioDevices"}, //13.0.0+
            FunctionInfo{76, nullptr, "SetWlanModeWithOption"}, //13.1.0+
            FunctionInfo{100, nullptr, "AcquireConnectionDisallowedEvent"}, //13.0.0+
            FunctionInfo{101, nullptr, "GetUsecaseViolationFactor"}, //13.0.0+
            FunctionInfo{110, nullptr, "GetShortenedDeviceInfo"}, //13.0.0+
            FunctionInfo{111, nullptr, "AcquirePairingCountUpdateEvent"},//13.0.0+
            FunctionInfo{112, nullptr, "Unknown112"}, //14.0.0-14.1.2
            FunctionInfo{113, nullptr, "Unknown113"}, //14.0.0-14.1.2
            FunctionInfo{114, nullptr, "IsFirstAudioControlConnection"}, //14.0.0+
            FunctionInfo{115, nullptr, "GetShortenedDeviceCondition"}, //14.0.0+
            FunctionInfo{116, nullptr, "SetAudioSinkVolume"}, //15.0.0+
            FunctionInfo{117, nullptr, "GetAudioSinkVolume"} //15.0.0+
        );
    }
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("btm", std::make_shared<IBtm>(system));
    server_manager->RegisterNamedService("btm:dbg", std::make_shared<IBtmDebug>(system));
    server_manager->RegisterNamedService("btm:sys", std::make_shared<IBtmSystem>(system));
    server_manager->RegisterNamedService("btm:u", std::make_shared<IBtmUser>(system));
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::BTM
