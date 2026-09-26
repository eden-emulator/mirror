// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>

#include "common/logging.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/set/system_settings_server.h"
#include "core/hle/service/sm/sm.h"
#include "core/hle/service/btm/btm.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"

namespace Kernel {
class KEvent;
class KReadableEvent;
} // namespace Kernel

namespace Core {
class System;
}

namespace Service::Set {
class ISystemSettingsServer;
}

namespace Service::BTM {

class IBtmSystemCore final : public ServiceFramework<IBtmSystemCore> {
public:
    explicit IBtmSystemCore(Core::System& system_)
        : ServiceFramework{system_, "IBtmSystemCore"}, service_context{system_, "IBtmSystemCore"} {
        radio_event = service_context.CreateEvent("IBtmSystemCore:RadioEvent");
        audio_device_connection_event = service_context.CreateEvent("IBtmSystemCore:AudioDeviceConnectionEvent");
        m_set_sys = system.ServiceManager().GetService<Service::Set::ISystemSettingsServer>("set:sys", true);
    }

    ~IBtmSystemCore() override {
        service_context.CloseEvent(radio_event);
        service_context.CloseEvent(audio_device_connection_event);
    }

    Result StartGamepadPairing() {
        LOG_WARNING(Service_BTM, "(STUBBED) called");
        R_SUCCEED();
    }

    Result CancelGamepadPairing() {
        LOG_WARNING(Service_BTM, "(STUBBED) called");
        R_SUCCEED();
    }

    Result EnableRadio() {
        LOG_DEBUG(Service_BTM, "called");

        R_RETURN(m_set_sys->SetBluetoothEnableFlag(true));
    }
    Result DisableRadio() {
        LOG_DEBUG(Service_BTM, "called");

        R_RETURN(m_set_sys->SetBluetoothEnableFlag(false));
    }

    Result IsRadioEnabled(Out<bool> out_is_enabled) {
        LOG_DEBUG(Service_BTM, "called");

        R_RETURN(m_set_sys->GetBluetoothEnableFlag(out_is_enabled));
    }

    Result AcquireRadioEvent(Out<bool> out_is_valid,
                                            OutCopyHandle<Kernel::KReadableEvent> out_event) {
        LOG_WARNING(Service_BTM, "(STUBBED) called");

        *out_is_valid = true;
        *out_event = &radio_event->GetReadableEvent();
        R_SUCCEED();
    }

    Result GetDiscoveredAudioDevice(OutArray<std::array<u8, 0xFF>, BufferAttr_HipcPointer> out_audio_devices, s32 count, Out<s32> out_total) {
        LOG_WARNING(Service_BTM, "(STUBBED) called");
        R_SUCCEED();
    }

    Result AcquireAudioDeviceConnectionEvent(
        OutCopyHandle<Kernel::KReadableEvent> out_event) {
        LOG_WARNING(Service_BTM, "(STUBBED) called");

        *out_event = &audio_device_connection_event->GetReadableEvent();
        R_SUCCEED();
    }

    Result GetConnectedAudioDevices(
        Out<s32> out_count, OutArray<std::array<u8, 0xFF>, BufferAttr_HipcPointer> out_audio_devices) {
        LOG_WARNING(Service_BTM, "(STUBBED) called");

        *out_count = 0;
        R_SUCCEED();
    }

    Result GetPairedAudioDevices(
        Out<s32> out_count, OutArray<std::array<u8, 0xFF>, BufferAttr_HipcPointer> out_audio_devices) {
        LOG_WARNING(Service_BTM, "(STUBBED) called");

        *out_count = 0;
        R_SUCCEED();
    }

    Result RequestAudioDeviceConnectionRejection(ClientAppletResourceUserId aruid) {
        LOG_WARNING(Service_BTM, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
        R_SUCCEED();
    }

    Result CancelAudioDeviceConnectionRejection(ClientAppletResourceUserId aruid) {
        LOG_WARNING(Service_BTM, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
        R_SUCCEED();
    }

    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, C<&IBtmSystemCore::StartGamepadPairing>, "StartGamepadPairing"},
        FunctionInfo{1, C<&IBtmSystemCore::CancelGamepadPairing>, "CancelGamepadPairing"},
        FunctionInfo{2, nullptr, "ClearGamepadPairingDatabase"},
        FunctionInfo{3, nullptr, "GetPairedGamepadCount"},
        FunctionInfo{4, C<&IBtmSystemCore::EnableRadio>, "EnableRadio"},
        FunctionInfo{5, C<&IBtmSystemCore::DisableRadio>, "DisableRadio"},
        FunctionInfo{6, C<&IBtmSystemCore::IsRadioEnabled>, "IsRadioEnabled"},
        FunctionInfo{7, C<&IBtmSystemCore::AcquireRadioEvent>, "AcquireRadioEvent"},
        FunctionInfo{8, nullptr, "AcquireGamepadPairingEvent"},
        FunctionInfo{9, nullptr, "IsGamepadPairingStarted"},
        FunctionInfo{10, nullptr, "StartAudioDeviceDiscovery"},
        FunctionInfo{11, nullptr, "StopAudioDeviceDiscovery"},
        FunctionInfo{12, nullptr, "IsDiscoveryingAudioDevice"},
        FunctionInfo{13, C<&IBtmSystemCore::GetDiscoveredAudioDevice>, "GetDiscoveredAudioDevice"},
        FunctionInfo{14, C<&IBtmSystemCore::AcquireAudioDeviceConnectionEvent>, "AcquireAudioDeviceConnectionEvent"},
        FunctionInfo{15, nullptr, "ConnectAudioDevice"},
        FunctionInfo{16, nullptr, "IsConnectingAudioDevice"},
        FunctionInfo{17, C<&IBtmSystemCore::GetConnectedAudioDevices>, "GetConnectedAudioDevices"},
        FunctionInfo{18, nullptr, "DisconnectAudioDevice"},
        FunctionInfo{19, nullptr, "AcquirePairedAudioDeviceInfoChangedEvent"},
        FunctionInfo{20, C<&IBtmSystemCore::GetPairedAudioDevices>, "GetPairedAudioDevices"},
        FunctionInfo{21, nullptr, "RemoveAudioDevicePairing"},
        FunctionInfo{22, C<&IBtmSystemCore::RequestAudioDeviceConnectionRejection>, "RequestAudioDeviceConnectionRejection"},
        FunctionInfo{23, C<&IBtmSystemCore::CancelAudioDeviceConnectionRejection>, "CancelAudioDeviceConnectionRejection"}
    );
    KernelHelpers::ServiceContext service_context;
    Kernel::KEvent* radio_event;
    Kernel::KEvent* audio_device_connection_event;
    std::shared_ptr<Service::Set::ISystemSettingsServer> m_set_sys;
};

class IBtmSystem final : public ServiceFramework<IBtmSystem> {
public:
    explicit IBtmSystem(Core::System& system_) : ServiceFramework{system_, "btm:sys"} {}
    ~IBtmSystem() override = default;

    Result GetCore(OutInterface<IBtmSystemCore> out_interface) {
        LOG_WARNING(Service_BTM, "called");

        *out_interface = std::make_shared<IBtmSystemCore>(system);
        R_SUCCEED();
    }

    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, C<&IBtmSystem::GetCore>, "GetCore"}
    );
};

class IBtmUserCore final : public ServiceFramework<IBtmUserCore> {
public:
    explicit IBtmUserCore(Core::System& system_)
        : ServiceFramework{system_, "IBtmUserCore"}, service_context{system_, "IBtmUserCore"} {
        scan_event = service_context.CreateEvent("IBtmUserCore:ScanEvent");
        connection_event = service_context.CreateEvent("IBtmUserCore:ConnectionEvent");
        service_discovery_event = service_context.CreateEvent("IBtmUserCore:DiscoveryEvent");
        config_event = service_context.CreateEvent("IBtmUserCore:ConfigEvent");
    }

    ~IBtmUserCore() override {
        service_context.CloseEvent(scan_event);
        service_context.CloseEvent(connection_event);
        service_context.CloseEvent(service_discovery_event);
        service_context.CloseEvent(config_event);
    }

    Result AcquireBleScanEvent(Out<bool> out_is_valid,
                                            OutCopyHandle<Kernel::KReadableEvent> out_event) {
        LOG_WARNING(Service_BTM, "(STUBBED) called");

        *out_is_valid = true;
        *out_event = &scan_event->GetReadableEvent();
        R_SUCCEED();
    }

    Result AcquireBleConnectionEvent(Out<bool> out_is_valid,
                                                OutCopyHandle<Kernel::KReadableEvent> out_event) {
        LOG_WARNING(Service_BTM, "(STUBBED) called");

        *out_is_valid = true;
        *out_event = &connection_event->GetReadableEvent();
        R_SUCCEED();
    }

    Result AcquireBleServiceDiscoveryEvent(
        Out<bool> out_is_valid, OutCopyHandle<Kernel::KReadableEvent> out_event) {
        LOG_WARNING(Service_BTM, "(STUBBED) called");

        *out_is_valid = true;
        *out_event = &service_discovery_event->GetReadableEvent();
        R_SUCCEED();
    }

    Result AcquireBleMtuConfigEvent(Out<bool> out_is_valid,
                                                OutCopyHandle<Kernel::KReadableEvent> out_event) {
        LOG_WARNING(Service_BTM, "(STUBBED) called");

        *out_is_valid = true;
        *out_event = &config_event->GetReadableEvent();
        R_SUCCEED();
    }

    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, C<&IBtmUserCore::AcquireBleScanEvent>, "AcquireBleScanEvent"},
        FunctionInfo{1, nullptr, "GetBleScanFilterParameter"},
        FunctionInfo{2, nullptr, "GetBleScanFilterParameter2"},
        FunctionInfo{3, nullptr, "StartBleScanForGeneral"},
        FunctionInfo{4, nullptr, "StopBleScanForGeneral"},
        FunctionInfo{5, nullptr, "GetBleScanResultsForGeneral"},
        FunctionInfo{6, nullptr, "StartBleScanForPaired"},
        FunctionInfo{7, nullptr, "StopBleScanForPaired"},
        FunctionInfo{8, nullptr, "StartBleScanForSmartDevice"},
        FunctionInfo{9, nullptr, "StopBleScanForSmartDevice"},
        FunctionInfo{10, nullptr, "GetBleScanResultsForSmartDevice"},
        FunctionInfo{17, C<&IBtmUserCore::AcquireBleConnectionEvent>, "AcquireBleConnectionEvent"},
        FunctionInfo{18, nullptr, "BleConnect"},
        FunctionInfo{19, nullptr, "BleDisconnect"},
        FunctionInfo{20, nullptr, "BleGetConnectionState"},
        FunctionInfo{21, nullptr, "AcquireBlePairingEvent"},
        FunctionInfo{22, nullptr, "BlePairDevice"},
        FunctionInfo{23, nullptr, "BleUnPairDevice"},
        FunctionInfo{24, nullptr, "BleUnPairDevice2"},
        FunctionInfo{25, nullptr, "BleGetPairedDevices"},
        FunctionInfo{26, C<&IBtmUserCore::AcquireBleServiceDiscoveryEvent>, "AcquireBleServiceDiscoveryEvent"},
        FunctionInfo{27, nullptr, "GetGattServices"},
        FunctionInfo{28, nullptr, "GetGattService"},
        FunctionInfo{29, nullptr, "GetGattIncludedServices"},
        FunctionInfo{30, nullptr, "GetBelongingGattService"},
        FunctionInfo{31, nullptr, "GetGattCharacteristics"},
        FunctionInfo{32, nullptr, "GetGattDescriptors"},
        FunctionInfo{33, C<&IBtmUserCore::AcquireBleMtuConfigEvent>, "AcquireBleMtuConfigEvent"},
        FunctionInfo{34, nullptr, "ConfigureBleMtu"},
        FunctionInfo{35, nullptr, "GetBleMtu"},
        FunctionInfo{36, nullptr, "RegisterBleGattDataPath"},
        FunctionInfo{37, nullptr, "UnregisterBleGattDataPath"}
    );
    KernelHelpers::ServiceContext service_context;
    Kernel::KEvent* scan_event;
    Kernel::KEvent* connection_event;
    Kernel::KEvent* service_discovery_event;
    Kernel::KEvent* config_event;
};

class IBtmUser final : public ServiceFramework<IBtmUser> {
public:
    explicit IBtmUser(Core::System& system_) : ServiceFramework{system_, "btm:u"} {}
    ~IBtmUser() override = default;

    Result GetCore(OutInterface<IBtmUserCore> out_interface) {
        LOG_WARNING(Service_BTM, "called");

        *out_interface = std::make_shared<IBtmUserCore>(system);
        R_SUCCEED();
    }

    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, C<&IBtmUser::GetCore>, "GetCore"}
    );
};

class IBtmDebug final : public ServiceFramework<IBtmDebug> {
public:
    explicit IBtmDebug(Core::System& system_) : ServiceFramework{system_, "btm:dbg"} {}
    ~IBtmDebug() override = default;
    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
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
        FunctionInfo{17, nullptr, "ForceEnableBtm"} //15.0.0+
    );
};

class IBtm final : public ServiceFramework<IBtm> {
public:
    explicit IBtm(Core::System& system_) : ServiceFramework{system_, "btm"} {}

    static constexpr auto functions = CreateStaticMap(
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
    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
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
