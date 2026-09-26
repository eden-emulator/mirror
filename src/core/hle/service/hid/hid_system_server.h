// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Kernel {
class KEvent;
}

namespace Service::HID {
class ResourceManager;
class HidFirmwareSettings;

class IHidSystemServer final : public ServiceFramework<IHidSystemServer> {
public:
    explicit IHidSystemServer(Core::System& system_, std::shared_ptr<ResourceManager> resource,
                              std::shared_ptr<HidFirmwareSettings> settings);
    ~IHidSystemServer() override;

private:
    void GetPlatformConfig(HLERequestContext& ctx);
    void ApplyNpadSystemCommonPolicy(HLERequestContext& ctx);
    void EnableAssigningSingleOnSlSrPress(HLERequestContext& ctx);
    void DisableAssigningSingleOnSlSrPress(HLERequestContext& ctx);
    void GetLastActiveNpad(HLERequestContext& ctx);
    void ApplyNpadSystemCommonPolicyFull(HLERequestContext& ctx);
    void GetNpadFullKeyGripColor(HLERequestContext& ctx);
    void GetMaskedSupportedNpadStyleSet(HLERequestContext& ctx);
    void SetSupportedNpadStyleSetAll(HLERequestContext& ctx);
    void GetNpadCaptureButtonAssignment(HLERequestContext& ctx);
    void GetAppletDetailedUiType(HLERequestContext& ctx);
    void GetNpadInterfaceType(HLERequestContext& ctx);
    void GetNpadLeftRightInterfaceType(HLERequestContext& ctx);
    void HasBattery(HLERequestContext& ctx);
    void HasLeftRightBattery(HLERequestContext& ctx);
    void GetUniquePadsFromNpad(HLERequestContext& ctx);
    void SetNpadSystemExtStateEnabled(HLERequestContext& ctx);
    void RegisterAppletResourceUserId(HLERequestContext& ctx);
    void UnregisterAppletResourceUserId(HLERequestContext& ctx);
    void EnableAppletToGetInput(HLERequestContext& ctx);
    void SetAruidValidForVibration(HLERequestContext& ctx);
    void EnableAppletToGetSixAxisSensor(HLERequestContext& ctx);
    void EnableAppletToGetPadInput(HLERequestContext& ctx);
    void EnableAppletToGetTouchScreen(HLERequestContext& ctx);
    void SetVibrationMasterVolume(HLERequestContext& ctx);
    void GetVibrationMasterVolume(HLERequestContext& ctx);
    void BeginPermitVibrationSession(HLERequestContext& ctx);
    void EndPermitVibrationSession(HLERequestContext& ctx);
    void IsJoyConRailEnabled(HLERequestContext& ctx);
    void IsJoyConAttachedOnAllRail(HLERequestContext& ctx);
    void AcquireConnectionTriggerTimeoutEvent(HLERequestContext& ctx);
    void AcquireDeviceRegisteredEventForControllerSupport(HLERequestContext& ctx);
    void GetRegisteredDevices(HLERequestContext& ctx);
    void AcquireUniquePadConnectionEventHandle(HLERequestContext& ctx);
    void GetUniquePadIds(HLERequestContext& ctx);
    void AcquireJoyDetachOnBluetoothOffEventHandle(HLERequestContext& ctx);
    void IsUsbFullKeyControllerEnabled(HLERequestContext& ctx);
    void EnableUsbFullKeyController(HLERequestContext& ctx);
    void IsHandheldButtonPressedOnConsoleMode(HLERequestContext& ctx);
    void InitializeFirmwareUpdate(HLERequestContext& ctx);
    void CheckFirmwareUpdateRequired(HLERequestContext& ctx);
    void SetFirmwareHotfixUpdateSkipEnabled(HLERequestContext& ctx);
    void InitializeUsbFirmwareUpdate(HLERequestContext& ctx);
    void FinalizeUsbFirmwareUpdate(HLERequestContext& ctx);
    void CheckUsbFirmwareUpdateRequired(HLERequestContext& ctx);
    void InitializeUsbFirmwareUpdateWithoutMemory(HLERequestContext& ctx);
    void SetTouchScreenMagnification(HLERequestContext& ctx);
    void GetTouchScreenFirmwareVersion(HLERequestContext& ctx);
    void SetTouchScreenDefaultConfiguration(HLERequestContext& ctx);
    void GetTouchScreenDefaultConfiguration(HLERequestContext& ctx);
    void SetForceHandheldStyleVibration(HLERequestContext& ctx);
    void IsUsingCustomButtonConfig(HLERequestContext& ctx);
    void IsAnyCustomButtonConfigEnabled(HLERequestContext& ctx);

    std::shared_ptr<ResourceManager> GetResourceManager();

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{31, nullptr, "SendKeyboardLockKeyEvent"},
        FunctionInfo{101, nullptr, "AcquireHomeButtonEventHandle"},
        FunctionInfo{111, nullptr, "ActivateHomeButton"},
        FunctionInfo{121, nullptr, "AcquireSleepButtonEventHandle"},
        FunctionInfo{131, nullptr, "ActivateSleepButton"},
        FunctionInfo{141, nullptr, "AcquireCaptureButtonEventHandle"},
        FunctionInfo{151, nullptr, "ActivateCaptureButton"},
        FunctionInfo{161, &IHidSystemServer::GetPlatformConfig, "GetPlatformConfig"},
        FunctionInfo{210, nullptr, "AcquireNfcDeviceUpdateEventHandle"},
        FunctionInfo{211, nullptr, "GetNpadsWithNfc"},
        FunctionInfo{212, nullptr, "AcquireNfcActivateEventHandle"},
        FunctionInfo{213, nullptr, "ActivateNfc"},
        FunctionInfo{214, nullptr, "GetXcdHandleForNpadWithNfc"},
        FunctionInfo{215, nullptr, "IsNfcActivated"},
        FunctionInfo{216, nullptr, "GetAbstractedPadIdForNpadWithNfc"},
        FunctionInfo{217, nullptr, "SetNfcEvent"},
        FunctionInfo{218, nullptr, "GetNfcInfo"},
        FunctionInfo{219, nullptr, "StartNfcDiscovery"},
        FunctionInfo{220, nullptr, "StopNfcDiscovery"},
        FunctionInfo{221, nullptr, "StartNtagRead"},
        FunctionInfo{222, nullptr, "StartNtagWrite"},
        FunctionInfo{223, nullptr, "SendNfcRawData"},
        FunctionInfo{224, nullptr, "RegisterMifareKey"},
        FunctionInfo{225, nullptr, "ClearMifareKey"},
        FunctionInfo{226, nullptr, "StartMifareRead"},
        FunctionInfo{227, nullptr, "StartMifareWrite"},
        FunctionInfo{230, nullptr, "AcquireIrSensorEventHndle" },
        FunctionInfo{231, nullptr, "ActivateIrSensor"},
        FunctionInfo{232, nullptr, "GetIrSensorState"},
        FunctionInfo{233, nullptr, "GetXcdHandleForNpadWihIrSensor" },
        FunctionInfo{234, nullptr, "GetNpadJoyHoldType"},
        FunctionInfo{241, nullptr, "GetDataFormat"},
        FunctionInfo{242, nullptr, "SetDataFormat"},
        FunctionInfo{243, nullptr, "GetMcuState"},
        FunctionInfo{244, nullptr, "SetMcuState"},
        FunctionInfo{245, nullptr, "GetMcuVersionForNfc"},
        FunctionInfo{246, nullptr, "CheckNfcDevicePower"},
        FunctionInfo{247, nullptr, "SetMcuStateImmediate"},
        FunctionInfo{301, nullptr, "ActivateNpadSystem"},
        FunctionInfo{303, &IHidSystemServer::ApplyNpadSystemCommonPolicy, "ApplyNpadSystemCommonPolicy"},
        FunctionInfo{304, &IHidSystemServer::EnableAssigningSingleOnSlSrPress, "EnableAssigningSingleOnSlSrPress"},
        FunctionInfo{305, &IHidSystemServer::DisableAssigningSingleOnSlSrPress, "DisableAssigningSingleOnSlSrPress"},
        FunctionInfo{306, &IHidSystemServer::GetLastActiveNpad, "GetLastActiveNpad"},
        FunctionInfo{307, nullptr, "GetNpadSystemExtStyle"},
        FunctionInfo{308, &IHidSystemServer::ApplyNpadSystemCommonPolicyFull, "ApplyNpadSystemCommonPolicyFull"},
        FunctionInfo{309, &IHidSystemServer::GetNpadFullKeyGripColor, "GetNpadFullKeyGripColor"},
        FunctionInfo{310, &IHidSystemServer::GetMaskedSupportedNpadStyleSet, "GetMaskedSupportedNpadStyleSet"},
        FunctionInfo{311, nullptr, "SetNpadPlayerLedBlinkingDevice"},
        FunctionInfo{312, &IHidSystemServer::SetSupportedNpadStyleSetAll, "SetSupportedNpadStyleSetAll"},
        FunctionInfo{313, &IHidSystemServer::GetNpadCaptureButtonAssignment, "GetNpadCaptureButtonAssignment"},
        FunctionInfo{314, nullptr, "GetAppletFooterUiType"},
        FunctionInfo{315, &IHidSystemServer::GetAppletDetailedUiType, "GetAppletDetailedUiType"},
        FunctionInfo{316, &IHidSystemServer::GetNpadInterfaceType, "GetNpadInterfaceType"},
        FunctionInfo{317, &IHidSystemServer::GetNpadLeftRightInterfaceType, "GetNpadLeftRightInterfaceType"},
        FunctionInfo{318, &IHidSystemServer::HasBattery, "HasBattery"},
        FunctionInfo{319, &IHidSystemServer::HasLeftRightBattery, "HasLeftRightBattery"},
        FunctionInfo{321, &IHidSystemServer::GetUniquePadsFromNpad, "GetUniquePadsFromNpad"},
        FunctionInfo{322, &IHidSystemServer::SetNpadSystemExtStateEnabled, "SetNpadSystemExtStateEnabled"},
        FunctionInfo{323, nullptr, "GetLastActiveUniquePad"},
        FunctionInfo{324, nullptr, "GetUniquePadButtonSet"},
        FunctionInfo{325, nullptr, "GetUniquePadColor"},
        FunctionInfo{326, nullptr, "GetUniquePadAppletDetailedUiType"},
        FunctionInfo{327, nullptr, "GetAbstractedPadIdDataFromNpad"},
        FunctionInfo{328, nullptr, "AttachAbstractedPadToNpad"},
        FunctionInfo{329, nullptr, "DetachAbstractedPadAll"},
        FunctionInfo{330, nullptr, "CheckAbstractedPadConnection"},
        FunctionInfo{332, nullptr, "ConvertAppletDetailedUiTypeFromPlayReportType"}, //19.0.0+
        FunctionInfo{333, nullptr, "SetNpadUserSpgApplet"}, //20.0.0+
        FunctionInfo{334, nullptr, "AcquireUniquePadButtonStateChangedEventHandle"}, //20.0.0+
        FunctionInfo{501, &IHidSystemServer::RegisterAppletResourceUserId, "RegisterAppletResourceUserId"},
        FunctionInfo{502, &IHidSystemServer::UnregisterAppletResourceUserId, "UnregisterAppletResourceUserId"},
        FunctionInfo{503, &IHidSystemServer::EnableAppletToGetInput, "EnableAppletToGetInput"},
        FunctionInfo{504, &IHidSystemServer::SetAruidValidForVibration, "SetAruidValidForVibration"},
        FunctionInfo{505, &IHidSystemServer::EnableAppletToGetSixAxisSensor, "EnableAppletToGetSixAxisSensor"},
        FunctionInfo{506, &IHidSystemServer::EnableAppletToGetPadInput, "EnableAppletToGetPadInput"},
        FunctionInfo{507, &IHidSystemServer::EnableAppletToGetTouchScreen, "EnableAppletToGetTouchScreen"},
        FunctionInfo{510, &IHidSystemServer::SetVibrationMasterVolume, "SetVibrationMasterVolume"},
        FunctionInfo{511, &IHidSystemServer::GetVibrationMasterVolume, "GetVibrationMasterVolume"},
        FunctionInfo{512, &IHidSystemServer::BeginPermitVibrationSession, "BeginPermitVibrationSession"},
        FunctionInfo{513, &IHidSystemServer::EndPermitVibrationSession, "EndPermitVibrationSession"},
        FunctionInfo{514, nullptr, "Unknown514"},
        FunctionInfo{520, nullptr, "EnableHandheldHids"},
        FunctionInfo{521, nullptr, "DisableHandheldHids"},
        FunctionInfo{522, nullptr, "SetJoyConRailEnabled"},
        FunctionInfo{523, &IHidSystemServer::IsJoyConRailEnabled, "IsJoyConRailEnabled"},
        FunctionInfo{524, nullptr, "IsHandheldHidsEnabled"},
        FunctionInfo{525, &IHidSystemServer::IsJoyConAttachedOnAllRail, "IsJoyConAttachedOnAllRail"},
        FunctionInfo{540, nullptr, "AcquirePlayReportControllerUsageUpdateEvent"},
        FunctionInfo{541, nullptr, "GetPlayReportControllerUsages"},
        FunctionInfo{542, nullptr, "AcquirePlayReportRegisteredDeviceUpdateEvent"},
        FunctionInfo{543, nullptr, "GetRegisteredDevicesOld"},
        FunctionInfo{544, &IHidSystemServer::AcquireConnectionTriggerTimeoutEvent, "AcquireConnectionTriggerTimeoutEvent"},
        FunctionInfo{545, nullptr, "SendConnectionTrigger"},
        FunctionInfo{546, &IHidSystemServer::AcquireDeviceRegisteredEventForControllerSupport, "AcquireDeviceRegisteredEventForControllerSupport"},
        FunctionInfo{547, nullptr, "GetAllowedBluetoothLinksCount"},
        FunctionInfo{548, &IHidSystemServer::GetRegisteredDevices, "GetRegisteredDevices"},
        FunctionInfo{549, nullptr, "GetConnectableRegisteredDevices"},
        FunctionInfo{551, &IHidSystemServer::GetRegisteredDevices, "GetRegisteredDevicesForControllerSupport"}, //20.0.0+ //mocked via 548 for Diablo 3 (at least)
        FunctionInfo{700, nullptr, "ActivateUniquePad"},
        FunctionInfo{702, &IHidSystemServer::AcquireUniquePadConnectionEventHandle, "AcquireUniquePadConnectionEventHandle"},
        FunctionInfo{703, &IHidSystemServer::GetUniquePadIds, "GetUniquePadIds"},
        FunctionInfo{711, nullptr, "AcquireUniquePadConnectionOnHandheldForNsEventHandle"}, //20.0.0+
        FunctionInfo{712, nullptr, "GetUniquePadColor12"}, //20.0.0+
        FunctionInfo{751, &IHidSystemServer::AcquireJoyDetachOnBluetoothOffEventHandle, "AcquireJoyDetachOnBluetoothOffEventHandle"},
        FunctionInfo{800, nullptr, "ListSixAxisSensorHandles"},
        FunctionInfo{801, nullptr, "IsSixAxisSensorUserCalibrationSupported"},
        FunctionInfo{802, nullptr, "ResetSixAxisSensorCalibrationValues"},
        FunctionInfo{803, nullptr, "StartSixAxisSensorUserCalibration"},
        FunctionInfo{804, nullptr, "CancelSixAxisSensorUserCalibration"},
        FunctionInfo{805, nullptr, "GetUniquePadBluetoothAddress"},
        FunctionInfo{806, nullptr, "DisconnectUniquePad"},
        FunctionInfo{807, nullptr, "GetUniquePadType"},
        FunctionInfo{808, nullptr, "GetUniquePadInterface"},
        FunctionInfo{809, nullptr, "GetUniquePadSerialNumber"},
        FunctionInfo{810, nullptr, "GetUniquePadControllerNumber"},
        FunctionInfo{811, nullptr, "GetSixAxisSensorUserCalibrationStage"},
        FunctionInfo{812, nullptr, "GetConsoleUniqueSixAxisSensorHandle"},
        FunctionInfo{813, nullptr, "GetDeviceType"},
        FunctionInfo{821, nullptr, "StartAnalogStickManualCalibration"},
        FunctionInfo{822, nullptr, "RetryCurrentAnalogStickManualCalibrationStage"},
        FunctionInfo{823, nullptr, "CancelAnalogStickManualCalibration"},
        FunctionInfo{824, nullptr, "ResetAnalogStickManualCalibration"},
        FunctionInfo{825, nullptr, "GetAnalogStickState"},
        FunctionInfo{826, nullptr, "GetAnalogStickManualCalibrationStage"},
        FunctionInfo{827, nullptr, "IsAnalogStickButtonPressed"},
        FunctionInfo{828, nullptr, "IsAnalogStickInReleasePosition"},
        FunctionInfo{829, nullptr, "IsAnalogStickInCircumference"},
        FunctionInfo{830, nullptr, "SetNotificationLedPattern"},
        FunctionInfo{831, nullptr, "SetNotificationLedPatternWithTimeout"},
        FunctionInfo{832, nullptr, "PrepareHidsForNotificationWake"},
        FunctionInfo{850, &IHidSystemServer::IsUsbFullKeyControllerEnabled, "IsUsbFullKeyControllerEnabled"},
        FunctionInfo{851, &IHidSystemServer::EnableUsbFullKeyController, "EnableUsbFullKeyController"},
        FunctionInfo{852, nullptr, "IsUsbConnected"},
        FunctionInfo{870, &IHidSystemServer::IsHandheldButtonPressedOnConsoleMode, "IsHandheldButtonPressedOnConsoleMode"},
        FunctionInfo{900, nullptr, "ActivateInputDetector"},
        FunctionInfo{901, nullptr, "NotifyInputDetector"},
        FunctionInfo{1000, &IHidSystemServer::InitializeFirmwareUpdate, "InitializeFirmwareUpdate"},
        FunctionInfo{1001, nullptr, "GetFirmwareVersion"},
        FunctionInfo{1002, nullptr, "GetAvailableFirmwareVersion"},
        FunctionInfo{1003, nullptr, "IsFirmwareUpdateAvailable"},
        FunctionInfo{1004, &IHidSystemServer::CheckFirmwareUpdateRequired, "CheckFirmwareUpdateRequired"},
        FunctionInfo{1005, nullptr, "StartFirmwareUpdate"},
        FunctionInfo{1006, nullptr, "AbortFirmwareUpdate"},
        FunctionInfo{1007, nullptr, "GetFirmwareUpdateState"},
        FunctionInfo{1008, nullptr, "ActivateAudioControl"},
        FunctionInfo{1009, nullptr, "AcquireAudioControlEventHandle"},
        FunctionInfo{1010, nullptr, "GetAudioControlStates"},
        FunctionInfo{1011, nullptr, "DeactivateAudioControl"},
        FunctionInfo{1012, nullptr, "GetFirmwareVersionStringForUserSupportPage"},
        FunctionInfo{1050, nullptr, "IsSixAxisSensorAccurateUserCalibrationSupported"},
        FunctionInfo{1051, nullptr, "StartSixAxisSensorAccurateUserCalibration"},
        FunctionInfo{1052, nullptr, "CancelSixAxisSensorAccurateUserCalibration"},
        FunctionInfo{1053, nullptr, "GetSixAxisSensorAccurateUserCalibrationState"},
        FunctionInfo{1100, nullptr, "GetHidbusSystemServiceObject"},
        FunctionInfo{1120, &IHidSystemServer::SetFirmwareHotfixUpdateSkipEnabled, "SetFirmwareHotfixUpdateSkipEnabled"},
        FunctionInfo{1130, &IHidSystemServer::InitializeUsbFirmwareUpdate, "InitializeUsbFirmwareUpdate"},
        FunctionInfo{1131, &IHidSystemServer::FinalizeUsbFirmwareUpdate, "FinalizeUsbFirmwareUpdate"},
        FunctionInfo{1132, &IHidSystemServer::CheckUsbFirmwareUpdateRequired, "CheckUsbFirmwareUpdateRequired"},
        FunctionInfo{1133, nullptr, "StartUsbFirmwareUpdate"},
        FunctionInfo{1134, nullptr, "GetUsbFirmwareUpdateState"},
        FunctionInfo{1135, &IHidSystemServer::InitializeUsbFirmwareUpdateWithoutMemory, "InitializeUsbFirmwareUpdateWithoutMemory"},
        FunctionInfo{1150, &IHidSystemServer::SetTouchScreenMagnification, "SetTouchScreenMagnification"},
        FunctionInfo{1151, &IHidSystemServer::GetTouchScreenFirmwareVersion, "GetTouchScreenFirmwareVersion"},
        FunctionInfo{1152, &IHidSystemServer::SetTouchScreenDefaultConfiguration, "SetTouchScreenDefaultConfiguration"},
        FunctionInfo{1153, &IHidSystemServer::GetTouchScreenDefaultConfiguration, "GetTouchScreenDefaultConfiguration"},
        FunctionInfo{1154, nullptr, "IsFirmwareAvailableForNotification"},
        FunctionInfo{1155, &IHidSystemServer::SetForceHandheldStyleVibration, "SetForceHandheldStyleVibration"},
        FunctionInfo{1156, nullptr, "SendConnectionTriggerWithoutTimeoutEvent"},
        FunctionInfo{1157, nullptr, "CancelConnectionTrigger"},
        FunctionInfo{1158, nullptr, "SetConnectionLimitForSplay"}, //20.1.0+
        FunctionInfo{1159, nullptr, "ClearConnectionLimitForSplay"}, //20.1.0+
        FunctionInfo{1200, nullptr, "IsButtonConfigSupported"},
        FunctionInfo{1201, nullptr, "IsButtonConfigEmbeddedSupported"},
        FunctionInfo{1202, nullptr, "DeleteButtonConfig"},
        FunctionInfo{1203, nullptr, "DeleteButtonConfigEmbedded"},
        FunctionInfo{1204, nullptr, "SetButtonConfigEnabled"},
        FunctionInfo{1205, nullptr, "SetButtonConfigEmbeddedEnabled"},
        FunctionInfo{1206, nullptr, "IsButtonConfigEnabled"},
        FunctionInfo{1207, nullptr, "IsButtonConfigEmbeddedEnabled"},
        FunctionInfo{1208, nullptr, "SetButtonConfigEmbedded"},
        FunctionInfo{1209, nullptr, "SetButtonConfigFull"},
        FunctionInfo{1210, nullptr, "SetButtonConfigLeft"},
        FunctionInfo{1211, nullptr, "SetButtonConfigRight"},
        FunctionInfo{1212, nullptr, "GetButtonConfigEmbedded"},
        FunctionInfo{1213, nullptr, "GetButtonConfigFull"},
        FunctionInfo{1214, nullptr, "GetButtonConfigLeft"},
        FunctionInfo{1215, nullptr, "GetButtonConfigRight"},
        FunctionInfo{1250, nullptr, "IsCustomButtonConfigSupported"},
        FunctionInfo{1251, nullptr, "IsDefaultButtonConfigEmbedded"},
        FunctionInfo{1252, nullptr, "IsDefaultButtonConfigFull"},
        FunctionInfo{1253, nullptr, "IsDefaultButtonConfigLeft"},
        FunctionInfo{1254, nullptr, "IsDefaultButtonConfigRight"},
        FunctionInfo{1255, nullptr, "IsButtonConfigStorageEmbeddedEmpty"},
        FunctionInfo{1256, nullptr, "IsButtonConfigStorageFullEmpty"},
        FunctionInfo{1257, nullptr, "IsButtonConfigStorageLeftEmpty"},
        FunctionInfo{1258, nullptr, "IsButtonConfigStorageRightEmpty"},
        FunctionInfo{1259, nullptr, "GetButtonConfigStorageEmbeddedDeprecated"},
        FunctionInfo{1260, nullptr, "GetButtonConfigStorageFullDeprecated"},
        FunctionInfo{1261, nullptr, "GetButtonConfigStorageLeftDeprecated"},
        FunctionInfo{1262, nullptr, "GetButtonConfigStorageRightDeprecated"},
        FunctionInfo{1263, nullptr, "SetButtonConfigStorageEmbeddedDeprecated"},
        FunctionInfo{1264, nullptr, "SetButtonConfigStorageFullDeprecated"},
        FunctionInfo{1265, nullptr, "SetButtonConfigStorageLeftDeprecated"},
        FunctionInfo{1266, nullptr, "SetButtonConfigStorageRightDeprecated"},
        FunctionInfo{1267, nullptr, "DeleteButtonConfigStorageEmbedded"},
        FunctionInfo{1268, nullptr, "DeleteButtonConfigStorageFull"},
        FunctionInfo{1269, nullptr, "DeleteButtonConfigStorageLeft"},
        FunctionInfo{1270, nullptr, "DeleteButtonConfigStorageRight"},
        FunctionInfo{1271, &IHidSystemServer::IsUsingCustomButtonConfig, "IsUsingCustomButtonConfig"},
        FunctionInfo{1272, &IHidSystemServer::IsAnyCustomButtonConfigEnabled, "IsAnyCustomButtonConfigEnabled"},
        FunctionInfo{1273, nullptr, "SetAllCustomButtonConfigEnabled"},
        FunctionInfo{1274, nullptr, "SetDefaultButtonConfig"},
        FunctionInfo{1275, nullptr, "SetAllDefaultButtonConfig"},
        FunctionInfo{1276, nullptr, "SetHidButtonConfigEmbedded"},
        FunctionInfo{1277, nullptr, "SetHidButtonConfigFull"},
        FunctionInfo{1278, nullptr, "SetHidButtonConfigLeft"},
        FunctionInfo{1279, nullptr, "SetHidButtonConfigRight"},
        FunctionInfo{1280, nullptr, "GetHidButtonConfigEmbedded"},
        FunctionInfo{1281, nullptr, "GetHidButtonConfigFull"},
        FunctionInfo{1282, nullptr, "GetHidButtonConfigLeft"},
        FunctionInfo{1283, nullptr, "GetHidButtonConfigRight"},
        FunctionInfo{1284, nullptr, "GetButtonConfigStorageEmbedded"},
        FunctionInfo{1285, nullptr, "GetButtonConfigStorageFull"},
        FunctionInfo{1286, nullptr, "GetButtonConfigStorageLeft"},
        FunctionInfo{1287, nullptr, "GetButtonConfigStorageRight"},
        FunctionInfo{1288, nullptr, "SetButtonConfigStorageEmbedded"},
        FunctionInfo{1289, nullptr, "SetButtonConfigStorageFull"},
        FunctionInfo{1290, nullptr, "DeleteButtonConfigStorageRight"},
        FunctionInfo{1291, nullptr, "DeleteButtonConfigStorageRight"},
        FunctionInfo{1308, nullptr, "SetButtonConfigVisible"}, //18.0.0+
        FunctionInfo{1309, nullptr, "IsButtonConfigVisible"}, //18.0.0+
        FunctionInfo{1320, nullptr, "WakeTouchScreenUp"}, //17.0.0+
        FunctionInfo{1321, nullptr, "PutTouchScreenToSleep"}, //17.0.0+
        FunctionInfo{1322, nullptr, "AcquireTouchScreenAsyncWakeCompletedEvent"}, //20.0.0+
        FunctionInfo{1323, nullptr, "StartTouchScreenAutoTuneForSystemSettings"}, //21.0.0+
        FunctionInfo{1324, nullptr, "AcquireTouchScreenAutoTuneCompletedEvent"}, //21.0.0+
        FunctionInfo{1325, nullptr, "IsTouchScreenAutoTuneRequiredForRepairProviderReplacement"}, //21.0.0+
        FunctionInfo{1326, nullptr, "SetTouchScreenOffset"}, //21.0.0+
        FunctionInfo{1420, nullptr, "GetAppletResourceProperty"}, //19.0.0+
        FunctionInfo{12010, nullptr, "SetButtonConfigLeft"} //11.0.0-17.0.1
    );

    Kernel::KEvent* acquire_connection_trigger_timeout_event;
    Kernel::KEvent* acquire_device_registered_event;
    Kernel::KEvent* joy_detach_event;
    Kernel::KEvent* unique_pad_connection_event;
    KernelHelpers::ServiceContext service_context;
    std::shared_ptr<ResourceManager> resource_manager;
    std::shared_ptr<HidFirmwareSettings> firmware_settings;
};

} // namespace Service::HID
