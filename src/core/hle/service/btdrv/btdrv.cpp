// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/logging.h"
#include "core/core.h"
#include "core/hle/kernel/k_event.h"
#include "core/hle/service/btdrv/btdrv.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"
#include "core/hle/service/sm/sm.h"

namespace Service::BtDrv {

class IBluetoothUser final : public ServiceFramework<IBluetoothUser> {
public:
    explicit IBluetoothUser(Core::System& system_)
        : ServiceFramework{system_, "bt"}, service_context{system_, "bt"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            FunctionInfo{0, nullptr, "LeClientReadCharacteristic"},
            FunctionInfo{1, nullptr, "LeClientReadDescriptor"},
            FunctionInfo{2, nullptr, "LeClientWriteCharacteristic"},
            FunctionInfo{3, nullptr, "LeClientWriteDescriptor"},
            FunctionInfo{4, nullptr, "LeClientRegisterNotification"},
            FunctionInfo{5, nullptr, "LeClientDeregisterNotification"},
            FunctionInfo{6, nullptr, "SetLeResponse"},
            FunctionInfo{7, nullptr, "LeSendIndication"},
            FunctionInfo{8, nullptr, "GetLeEventInfo"},
            FunctionInfo{9, C<&IBluetoothUser::RegisterBleEvent>, "RegisterBleEvent"}
        };
        // clang-format on
        RegisterHandlers(functions);

        register_event = service_context.CreateEvent("BT:RegisterEvent");
    }

    ~IBluetoothUser() override {
        service_context.CloseEvent(register_event);
    }

private:
    Result RegisterBleEvent(OutCopyHandle<Kernel::KReadableEvent> out_event) {
        LOG_WARNING(Service_BTM, "(STUBBED) called");

        *out_event = &register_event->GetReadableEvent();
        R_SUCCEED();
    }

    KernelHelpers::ServiceContext service_context;

    Kernel::KEvent* register_event;
};

class IBluetoothDriver final : public ServiceFramework<IBluetoothDriver> {
public:
    explicit IBluetoothDriver(Core::System& system_) : ServiceFramework{system_, "btdrv"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            FunctionInfo{0, nullptr, "InitializeBluetoothDriver"},
            FunctionInfo{1, nullptr, "InitializeBluetooth"},
            FunctionInfo{2, nullptr, "EnableBluetooth"},
            FunctionInfo{3, nullptr, "DisableBluetooth"},
            FunctionInfo{4, nullptr, "FinalizeBluetooth"},
            FunctionInfo{5, nullptr, "GetAdapterProperties"},
            FunctionInfo{6, nullptr, "GetAdapterProperty"},
            FunctionInfo{7, nullptr, "SetAdapterProperty"},
            FunctionInfo{8, nullptr, "StartInquiry"},
            FunctionInfo{9, nullptr, "StopInquiry"},
            FunctionInfo{10, nullptr, "CreateBond"},
            FunctionInfo{11, nullptr, "RemoveBond"},
            FunctionInfo{12, nullptr, "CancelBond"},
            FunctionInfo{13, nullptr, "RespondToPinRequest"},
            FunctionInfo{14, nullptr, "RespondToSspRequest"},
            FunctionInfo{15, nullptr, "GetEventInfo"},
            FunctionInfo{16, nullptr, "InitializeHid"},
            FunctionInfo{17, nullptr, "OpenHidConnection"},
            FunctionInfo{18, nullptr, "CloseHidConnection"},
            FunctionInfo{19, nullptr, "WriteHidData"},
            FunctionInfo{20, nullptr, "WriteHidData2"},
            FunctionInfo{21, nullptr, "SetHidReport"},
            FunctionInfo{22, nullptr, "GetHidReport"},
            FunctionInfo{23, nullptr, "TriggerConnection"},
            FunctionInfo{24, nullptr, "AddPairedDeviceInfo"},
            FunctionInfo{25, nullptr, "GetPairedDeviceInfo"},
            FunctionInfo{26, nullptr, "FinalizeHid"},
            FunctionInfo{27, nullptr, "GetHidEventInfo"},
            FunctionInfo{28, nullptr, "SetTsi"},
            FunctionInfo{29, nullptr, "EnableBurstMode"},
            FunctionInfo{30, nullptr, "SetZeroRetransmission"},
            FunctionInfo{31, nullptr, "EnableMcMode"},
            FunctionInfo{32, nullptr, "EnableLlrScan"},
            FunctionInfo{33, nullptr, "DisableLlrScan"},
            FunctionInfo{34, C<&IBluetoothDriver::EnableRadio>, "EnableRadio"},
            FunctionInfo{35, nullptr, "SetVisibility"},
            FunctionInfo{36, nullptr, "EnableTbfcScan"},
            FunctionInfo{37, nullptr, "RegisterHidReportEvent"},
            FunctionInfo{38, nullptr, "GetHidReportEventInfo"},
            FunctionInfo{39, nullptr, "GetLatestPlr"},
            FunctionInfo{40, nullptr, "GetPendingConnections"},
            FunctionInfo{41, nullptr, "GetChannelMap"},
            FunctionInfo{42, nullptr, "EnableTxPowerBoostSetting"},
            FunctionInfo{43, nullptr, "IsTxPowerBoostSettingEnabled"},
            FunctionInfo{44, nullptr, "EnableAfhSetting"},
            FunctionInfo{45, nullptr, "IsAfhSettingEnabled"},
            FunctionInfo{46, nullptr, "InitializeBle"},
            FunctionInfo{47, nullptr, "EnableBle"},
            FunctionInfo{48, nullptr, "DisableBle"},
            FunctionInfo{49, nullptr, "FinalizeBle"},
            FunctionInfo{50, nullptr, "SetBleVisibility"},
            FunctionInfo{51, nullptr, "SetBleConnectionParameter"},
            FunctionInfo{52, nullptr, "SetBleDefaultConnectionParameter"},
            FunctionInfo{53, nullptr, "SetBleAdvertiseData"},
            FunctionInfo{54, nullptr, "SetBleAdvertiseParameter"},
            FunctionInfo{55, nullptr, "StartBleScan"},
            FunctionInfo{56, nullptr, "StopBleScan"},
            FunctionInfo{57, nullptr, "AddBleScanFilterCondition"},
            FunctionInfo{58, nullptr, "DeleteBleScanFilterCondition"},
            FunctionInfo{59, nullptr, "DeleteBleScanFilter"},
            FunctionInfo{60, nullptr, "ClearBleScanFilters"},
            FunctionInfo{61, nullptr, "EnableBleScanFilter"},
            FunctionInfo{62, nullptr, "RegisterGattClient"},
            FunctionInfo{63, nullptr, "UnregisterGattClient"},
            FunctionInfo{64, nullptr, "UnregisterAllGattClients"},
            FunctionInfo{65, nullptr, "ConnectGattServer"},
            FunctionInfo{66, nullptr, "CancelConnectGattServer"},
            FunctionInfo{67, nullptr, "DisconnectGattServer"},
            FunctionInfo{68, nullptr, "GetGattAttribute"},
            FunctionInfo{69, nullptr, "GetGattService"},
            FunctionInfo{70, nullptr, "ConfigureAttMtu"},
            FunctionInfo{71, nullptr, "RegisterGattServer"},
            FunctionInfo{72, nullptr, "UnregisterGattServer"},
            FunctionInfo{73, nullptr, "ConnectGattClient"},
            FunctionInfo{74, nullptr, "DisconnectGattClient"},
            FunctionInfo{75, nullptr, "AddGattService"},
            FunctionInfo{76, nullptr, "EnableGattService"},
            FunctionInfo{77, nullptr, "AddGattCharacteristic"},
            FunctionInfo{78, nullptr, "AddGattDescriptor"},
            FunctionInfo{79, nullptr, "GetBleManagedEventInfo"},
            FunctionInfo{80, nullptr, "GetGattFirstCharacteristic"},
            FunctionInfo{81, nullptr, "GetGattNextCharacteristic"},
            FunctionInfo{82, nullptr, "GetGattFirstDescriptor"},
            FunctionInfo{83, nullptr, "GetGattNextDescriptor"},
            FunctionInfo{84, nullptr, "RegisterGattManagedDataPath"},
            FunctionInfo{85, nullptr, "UnregisterGattManagedDataPath"},
            FunctionInfo{86, nullptr, "RegisterGattHidDataPath"},
            FunctionInfo{87, nullptr, "UnregisterGattHidDataPath"},
            FunctionInfo{88, nullptr, "RegisterGattDataPath"},
            FunctionInfo{89, nullptr, "UnregisterGattDataPath"},
            FunctionInfo{90, nullptr, "ReadGattCharacteristic"},
            FunctionInfo{91, nullptr, "ReadGattDescriptor"},
            FunctionInfo{92, nullptr, "WriteGattCharacteristic"},
            FunctionInfo{93, nullptr, "WriteGattDescriptor"},
            FunctionInfo{94, nullptr, "RegisterGattNotification"},
            FunctionInfo{95, nullptr, "UnregisterGattNotification"},
            FunctionInfo{96, nullptr, "GetLeHidEventInfo"},
            FunctionInfo{97, nullptr, "RegisterBleHidEvent"},
            FunctionInfo{98, nullptr, "SetBleScanParameter"},
            FunctionInfo{99, nullptr, "MoveToSecondaryPiconet"},
            FunctionInfo{100, nullptr, "IsBluetoothEnabled"},
            FunctionInfo{128, nullptr, "AcquireAudioEvent"},
            FunctionInfo{129, nullptr, "GetAudioEventInfo"},
            FunctionInfo{130, nullptr, "OpenAudioConnection"},
            FunctionInfo{131, nullptr, "CloseAudioConnection"},
            FunctionInfo{132, nullptr, "OpenAudioOut"},
            FunctionInfo{133, nullptr, "CloseAudioOut"},
            FunctionInfo{134, nullptr, "AcquireAudioOutStateChangedEvent"},
            FunctionInfo{135, nullptr, "StartAudioOut"},
            FunctionInfo{136, nullptr, "StopAudioOut"},
            FunctionInfo{137, nullptr, "GetAudioOutState"},
            FunctionInfo{138, nullptr, "GetAudioOutFeedingCodec"},
            FunctionInfo{139, nullptr, "GetAudioOutFeedingParameter"},
            FunctionInfo{140, nullptr, "AcquireAudioOutBufferAvailableEvent"},
            FunctionInfo{141, nullptr, "SendAudioData"},
            FunctionInfo{142, nullptr, "AcquireAudioControlInputStateChangedEvent"},
            FunctionInfo{143, nullptr, "GetAudioControlInputState"},
            FunctionInfo{144, nullptr, "AcquireAudioConnectionStateChangedEvent"},
            FunctionInfo{145, nullptr, "GetConnectedAudioDevice"},
            FunctionInfo{146, nullptr, "CloseAudioControlInput"},
            FunctionInfo{147, nullptr, "RegisterAudioControlNotification"},
            FunctionInfo{148, nullptr, "SendAudioControlPassthroughCommand"},
            FunctionInfo{149, nullptr, "SendAudioControlSetAbsoluteVolumeCommand"},
            FunctionInfo{150, nullptr, "AcquireAudioSinkVolumeLocallyChangedEvent"},
            FunctionInfo{151, nullptr, "AcquireAudioSinkVolumeUpdateRequestCompletedEvent"},
            FunctionInfo{152, nullptr, "GetAudioSinkVolume"},
            FunctionInfo{153, nullptr, "RequestUpdateAudioSinkVolume"},
            FunctionInfo{154, nullptr, "IsAudioSinkVolumeSupported"},
            FunctionInfo{256, nullptr, "IsManufacturingMode"},
            FunctionInfo{257, nullptr, "EmulateBluetoothCrash"},
            FunctionInfo{258, nullptr, "GetBleChannelMap"}
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    Result EnableRadio() {
        LOG_WARNING(Service_BTDRV, "(STUBBED) called");
        R_SUCCEED();
    }
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("btdrv", std::make_shared<IBluetoothDriver>(system));
    server_manager->RegisterNamedService("bt", std::make_shared<IBluetoothUser>(system));
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::BtDrv
