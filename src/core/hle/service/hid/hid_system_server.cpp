// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/hle/service/hid/hid_system_server.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/set/settings_types.h"
#include "hid_core/hid_result.h"
#include "hid_core/resource_manager.h"
#include "hid_core/resources/hid_firmware_settings.h"
#include "hid_core/resources/npad/npad.h"
#include "hid_core/resources/npad/npad_types.h"
#include "hid_core/resources/npad/npad_vibration.h"
#include "hid_core/resources/palma/palma.h"
#include "hid_core/resources/touch_screen/touch_screen.h"

namespace Service::HID {

IHidSystemServer::IHidSystemServer(Core::System& system_, std::shared_ptr<ResourceManager> resource,
                                   std::shared_ptr<HidFirmwareSettings> settings)
    : ServiceFramework{system_, "hid:sys"}, service_context{system_, service_name},
      resource_manager{resource}, firmware_settings{settings} {
    // clang-format off
    static const FunctionInfo functions[] = {
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
    };
    // clang-format on

    RegisterHandlers(functions);

    joy_detach_event = service_context.CreateEvent("IHidSystemServer::JoyDetachEvent");
    acquire_device_registered_event =
        service_context.CreateEvent("IHidSystemServer::AcquireDeviceRegisteredEvent");
    acquire_connection_trigger_timeout_event =
        service_context.CreateEvent("IHidSystemServer::AcquireConnectionTriggerTimeoutEvent");
    unique_pad_connection_event =
        service_context.CreateEvent("IHidSystemServer::AcquireUniquePadConnectionEventHandle");
}

IHidSystemServer::~IHidSystemServer() {
    service_context.CloseEvent(joy_detach_event);
    service_context.CloseEvent(acquire_device_registered_event);
    service_context.CloseEvent(acquire_connection_trigger_timeout_event);
    service_context.CloseEvent(unique_pad_connection_event);
};

void IHidSystemServer::GetPlatformConfig(HLERequestContext& ctx) {
    const auto platform_config = firmware_settings->GetPlatformConfig();

    LOG_INFO(Service_HID, "called, platform_config={}", platform_config.raw);

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.PushRaw(platform_config);
}

void IHidSystemServer::ApplyNpadSystemCommonPolicy(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto applet_resource_user_id{rp.Pop<u64>()};

    LOG_INFO(Service_HID, "called, applet_resource_user_id={}", applet_resource_user_id);

    GetResourceManager()->GetNpad()->ApplyNpadSystemCommonPolicy(system.Kernel(), applet_resource_user_id);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IHidSystemServer::EnableAssigningSingleOnSlSrPress(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto applet_resource_user_id{rp.Pop<u64>()};

    LOG_INFO(Service_HID, "called, applet_resource_user_id={}", applet_resource_user_id);

    GetResourceManager()->GetNpad()->AssigningSingleOnSlSrPress(applet_resource_user_id, true);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IHidSystemServer::DisableAssigningSingleOnSlSrPress(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto applet_resource_user_id{rp.Pop<u64>()};

    LOG_INFO(Service_HID, "called, applet_resource_user_id={}", applet_resource_user_id);

    GetResourceManager()->GetNpad()->AssigningSingleOnSlSrPress(applet_resource_user_id, false);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IHidSystemServer::GetLastActiveNpad(HLERequestContext& ctx) {
    Core::HID::NpadIdType npad_id{};
    const Result result = GetResourceManager()->GetNpad()->GetLastActiveNpad(npad_id);

    LOG_DEBUG(Service_HID, "called, npad_id={}", npad_id);

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(result);
    rb.PushEnum(npad_id);
}

void IHidSystemServer::ApplyNpadSystemCommonPolicyFull(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto applet_resource_user_id{rp.Pop<u64>()};

    LOG_INFO(Service_HID, "called, applet_resource_user_id={}", applet_resource_user_id);

    GetResourceManager()->GetNpad()->ApplyNpadSystemCommonPolicyFull(system.Kernel(), applet_resource_user_id);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IHidSystemServer::GetNpadFullKeyGripColor(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto npad_id_type{rp.PopEnum<Core::HID::NpadIdType>()};

    LOG_DEBUG(Service_HID, "(STUBBED) called, npad_id_type={}",
              npad_id_type); // Spams a lot when controller applet is running

    Core::HID::NpadColor left_color{};
    Core::HID::NpadColor right_color{};
    // TODO: Get colors from Npad

    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.PushRaw(left_color);
    rb.PushRaw(right_color);
}

void IHidSystemServer::GetMaskedSupportedNpadStyleSet(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto applet_resource_user_id{rp.Pop<u64>()};

    LOG_INFO(Service_HID, "called, applet_resource_user_id={}", applet_resource_user_id);

    Core::HID::NpadStyleSet supported_styleset{};
    const auto npad = GetResourceManager()->GetNpad();
    const Result result = npad->GetMaskedSupportedNpadStyleSet(system.Kernel(), applet_resource_user_id, supported_styleset);

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(result);
    rb.PushEnum(supported_styleset);
}

void IHidSystemServer::SetSupportedNpadStyleSetAll(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto applet_resource_user_id{rp.Pop<u64>()};

    LOG_DEBUG(Service_HID, "called, applet_resource_user_id={}", applet_resource_user_id);

    const auto npad = GetResourceManager()->GetNpad();
    const auto result = npad->SetSupportedNpadStyleSet(system.Kernel(), applet_resource_user_id, Core::HID::NpadStyleSet::All);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result);
}

void IHidSystemServer::GetNpadCaptureButtonAssignment(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto applet_resource_user_id{rp.Pop<u64>()};
    const auto capture_button_list_size{ctx.GetWriteBufferNumElements<Core::HID::NpadButton>()};

    LOG_DEBUG(Service_HID, "called, applet_resource_user_id={}", applet_resource_user_id);

    std::vector<Core::HID::NpadButton> capture_button_list(capture_button_list_size);
    const auto& npad = GetResourceManager()->GetNpad();
    const u64 list_size =
        npad->GetNpadCaptureButtonAssignment(capture_button_list, applet_resource_user_id);

    if (list_size != 0) {
        ctx.WriteBuffer(capture_button_list);
    }

    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push(list_size);
}

void IHidSystemServer::GetAppletDetailedUiType(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto npad_id_type{rp.PopEnum<Core::HID::NpadIdType>()};

    LOG_DEBUG(Service_HID, "called, npad_id_type={}",
              npad_id_type); // Spams a lot when controller applet is running

    const AppletDetailedUiType detailed_ui_type =
        GetResourceManager()->GetNpad()->GetAppletDetailedUiType(npad_id_type);

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.PushRaw(detailed_ui_type);
}

void IHidSystemServer::GetNpadInterfaceType(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto npad_id_type{rp.PopEnum<Core::HID::NpadIdType>()};

    LOG_DEBUG(Service_HID, "(STUBBED) called, npad_id_type={}",
              npad_id_type); // Spams a lot when controller applet is running

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.PushEnum(Core::HID::NpadInterfaceType::Bluetooth);
}

void IHidSystemServer::GetNpadLeftRightInterfaceType(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto npad_id_type{rp.PopEnum<Core::HID::NpadIdType>()};

    LOG_DEBUG(Service_HID, "(STUBBED) called, npad_id_type={}",
              npad_id_type); // Spams a lot when controller applet is running

    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.PushEnum(Core::HID::NpadInterfaceType::Bluetooth);
    rb.PushEnum(Core::HID::NpadInterfaceType::Bluetooth);
}

void IHidSystemServer::HasBattery(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto npad_id_type{rp.PopEnum<Core::HID::NpadIdType>()};

    LOG_DEBUG(Service_HID, "(STUBBED) called, npad_id_type={}",
              npad_id_type); // Spams a lot when controller applet is running

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push(false);
}

void IHidSystemServer::HasLeftRightBattery(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto npad_id_type{rp.PopEnum<Core::HID::NpadIdType>()};

    LOG_DEBUG(Service_HID, "(STUBBED) called, npad_id_type={}",
              npad_id_type); // Spams a lot when controller applet is running

    struct LeftRightBattery {
        bool left;
        bool right;
    };

    LeftRightBattery left_right_battery{
        .left = false,
        .right = false,
    };

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.PushRaw(left_right_battery);
}

void IHidSystemServer::GetUniquePadsFromNpad(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto npad_id_type{rp.PopEnum<Core::HID::NpadIdType>()};

    LOG_DEBUG(Service_HID, "(STUBBED) called, npad_id_type={}",
              npad_id_type); // Spams a lot when controller applet is running

    const std::vector<Core::HID::UniquePadId> unique_pads{};

    if (!unique_pads.empty()) {
        ctx.WriteBuffer(unique_pads);
    }

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push(static_cast<u32>(unique_pads.size()));
}

void IHidSystemServer::SetNpadSystemExtStateEnabled(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    struct Parameters {
        bool is_enabled;
        INSERT_PADDING_BYTES_NOINIT(7);
        u64 applet_resource_user_id;
    };
    static_assert(sizeof(Parameters) == 0x10, "Parameters has incorrect size.");

    const auto parameters{rp.PopRaw<Parameters>()};

    LOG_INFO(Service_HID, "called, is_enabled={}, applet_resource_user_id={}",
             parameters.is_enabled, parameters.applet_resource_user_id);

    const auto result = GetResourceManager()->GetNpad()->SetNpadSystemExtStateEnabled(
        parameters.applet_resource_user_id, parameters.is_enabled);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result);
}
void IHidSystemServer::RegisterAppletResourceUserId(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    struct Parameters {
        bool enable_input;
        INSERT_PADDING_WORDS_NOINIT(1);
        u64 applet_resource_user_id;
    };
    static_assert(sizeof(Parameters) == 0x10, "Parameters has incorrect size.");

    const auto parameters{rp.PopRaw<Parameters>()};

    LOG_INFO(Service_HID, "called, enable_input={}, applet_resource_user_id={}",
             parameters.enable_input, parameters.applet_resource_user_id);

    Result result = GetResourceManager()->RegisterAppletResourceUserId(
        parameters.applet_resource_user_id, parameters.enable_input);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result);
}

void IHidSystemServer::UnregisterAppletResourceUserId(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    u64 applet_resource_user_id{rp.Pop<u64>()};

    LOG_INFO(Service_HID, "called, applet_resource_user_id={}", applet_resource_user_id);

    GetResourceManager()->UnregisterAppletResourceUserId(applet_resource_user_id);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IHidSystemServer::EnableAppletToGetInput(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    struct Parameters {
        bool is_enabled;
        INSERT_PADDING_WORDS_NOINIT(1);
        u64 applet_resource_user_id;
    };
    static_assert(sizeof(Parameters) == 0x10, "Parameters has incorrect size.");

    const auto parameters{rp.PopRaw<Parameters>()};

    LOG_INFO(Service_HID, "called, is_enabled={}, applet_resource_user_id={}",
             parameters.is_enabled, parameters.applet_resource_user_id);

    GetResourceManager()->EnableInput(parameters.applet_resource_user_id, parameters.is_enabled);
    GetResourceManager()->GetNpad()->EnableAppletToGetInput(parameters.applet_resource_user_id);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IHidSystemServer::SetAruidValidForVibration(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    struct Parameters {
        bool is_enabled;
        INSERT_PADDING_WORDS_NOINIT(1);
        u64 applet_resource_user_id;
    };
    static_assert(sizeof(Parameters) == 0x10, "Parameters has incorrect size.");

    const auto parameters{rp.PopRaw<Parameters>()};

    LOG_INFO(Service_HID, "called, is_enabled={}, applet_resource_user_id={}",
             parameters.is_enabled, parameters.applet_resource_user_id);

    GetResourceManager()->SetAruidValidForVibration(parameters.applet_resource_user_id,
                                                    parameters.is_enabled);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IHidSystemServer::EnableAppletToGetSixAxisSensor(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    struct Parameters {
        bool is_enabled;
        INSERT_PADDING_WORDS_NOINIT(1);
        u64 applet_resource_user_id;
    };
    static_assert(sizeof(Parameters) == 0x10, "Parameters has incorrect size.");

    const auto parameters{rp.PopRaw<Parameters>()};

    LOG_INFO(Service_HID, "called, is_enabled={}, applet_resource_user_id={}",
             parameters.is_enabled, parameters.applet_resource_user_id);

    GetResourceManager()->EnableTouchScreen(parameters.applet_resource_user_id,
                                            parameters.is_enabled);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IHidSystemServer::EnableAppletToGetPadInput(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    struct Parameters {
        bool is_enabled;
        INSERT_PADDING_WORDS_NOINIT(1);
        u64 applet_resource_user_id;
    };
    static_assert(sizeof(Parameters) == 0x10, "Parameters has incorrect size.");

    const auto parameters{rp.PopRaw<Parameters>()};

    LOG_INFO(Service_HID, "called, is_enabled={}, applet_resource_user_id={}",
             parameters.is_enabled, parameters.applet_resource_user_id);

    GetResourceManager()->EnablePadInput(parameters.applet_resource_user_id, parameters.is_enabled);
    GetResourceManager()->GetNpad()->EnableAppletToGetInput(parameters.applet_resource_user_id);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IHidSystemServer::EnableAppletToGetTouchScreen(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    struct Parameters {
        bool is_enabled;
        INSERT_PADDING_WORDS_NOINIT(1);
        u64 applet_resource_user_id;
    };
    static_assert(sizeof(Parameters) == 0x10, "Parameters has incorrect size.");

    const auto parameters{rp.PopRaw<Parameters>()};

    LOG_INFO(Service_HID, "called, is_enabled={}, applet_resource_user_id={}",
             parameters.is_enabled, parameters.applet_resource_user_id);

    GetResourceManager()->EnableTouchScreen(parameters.applet_resource_user_id,
                                            parameters.is_enabled);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IHidSystemServer::SetVibrationMasterVolume(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto master_volume{rp.Pop<f32>()};

    LOG_INFO(Service_HID, "called, volume={}", master_volume);

    const auto result =
        GetResourceManager()->GetNpad()->GetVibrationHandler()->SetVibrationMasterVolume(
            master_volume);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result);
}

void IHidSystemServer::GetVibrationMasterVolume(HLERequestContext& ctx) {
    f32 master_volume{};
    const auto result =
        GetResourceManager()->GetNpad()->GetVibrationHandler()->GetVibrationMasterVolume(
            master_volume);

    LOG_INFO(Service_HID, "called, volume={}", master_volume);

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(result);
    rb.Push(master_volume);
}

void IHidSystemServer::BeginPermitVibrationSession(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto applet_resource_user_id{rp.Pop<u64>()};

    LOG_INFO(Service_HID, "called, applet_resource_user_id={}", applet_resource_user_id);

    const auto result =
        GetResourceManager()->GetNpad()->GetVibrationHandler()->BeginPermitVibrationSession(
            applet_resource_user_id);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result);
}

void IHidSystemServer::EndPermitVibrationSession(HLERequestContext& ctx) {
    LOG_INFO(Service_HID, "called");

    const auto result =
        GetResourceManager()->GetNpad()->GetVibrationHandler()->EndPermitVibrationSession();

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result);
}

void IHidSystemServer::IsJoyConRailEnabled(HLERequestContext& ctx) {
    const bool is_attached = true;

    LOG_WARNING(Service_HID, "(STUBBED) called, is_attached={}", is_attached);

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push(is_attached);
}

void IHidSystemServer::IsJoyConAttachedOnAllRail(HLERequestContext& ctx) {
    const bool is_attached = true;

    LOG_DEBUG(Service_HID, "(STUBBED) called, is_attached={}", is_attached);

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push(is_attached);
}

void IHidSystemServer::AcquireConnectionTriggerTimeoutEvent(HLERequestContext& ctx) {
    LOG_INFO(Service_AM, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2, 1};
    rb.Push(ResultSuccess);
    rb.PushCopyObjects(ctx, acquire_device_registered_event->GetReadableEvent());
}

void IHidSystemServer::AcquireDeviceRegisteredEventForControllerSupport(HLERequestContext& ctx) {
    LOG_INFO(Service_HID, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2, 1};
    rb.Push(ResultSuccess);
    rb.PushCopyObjects(ctx, acquire_device_registered_event->GetReadableEvent());
}

void IHidSystemServer::GetRegisteredDevices(HLERequestContext& ctx) {
    LOG_WARNING(Service_HID, "(STUBBED) called, command={}", ctx.GetCommand()); //548 or 551

    struct RegisterData {
        std::array<u8, 0x68> data;
    };
    static_assert(sizeof(RegisterData) == 0x68, "RegisterData is an invalid size");
    std::vector<RegisterData> registered_devices{};

    if (!registered_devices.empty()) {
        ctx.WriteBuffer(registered_devices);
    }

    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<u64>(registered_devices.size());
}

void IHidSystemServer::AcquireUniquePadConnectionEventHandle(HLERequestContext& ctx) {
    LOG_WARNING(Service_HID, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2, 1};
    rb.PushCopyObjects(ctx, unique_pad_connection_event->GetReadableEvent());
    rb.Push(ResultSuccess);
}

void IHidSystemServer::GetUniquePadIds(HLERequestContext& ctx) {
    LOG_DEBUG(Service_HID, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<u64>(0);
}

void IHidSystemServer::AcquireJoyDetachOnBluetoothOffEventHandle(HLERequestContext& ctx) {
    LOG_INFO(Service_AM, "called");

    IPC::ResponseBuilder rb{ctx, 2, 1};
    rb.Push(ResultSuccess);
    rb.PushCopyObjects(ctx, joy_detach_event->GetReadableEvent());
}

void IHidSystemServer::IsUsbFullKeyControllerEnabled(HLERequestContext& ctx) {
    const bool is_enabled = false;

    LOG_WARNING(Service_HID, "(STUBBED) called, is_enabled={}", is_enabled);

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push(is_enabled);
}

void IHidSystemServer::EnableUsbFullKeyController(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto is_enabled{rp.Pop<bool>()};

    LOG_WARNING(Service_HID, "(STUBBED) called, is_enabled={}", is_enabled);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IHidSystemServer::IsHandheldButtonPressedOnConsoleMode(HLERequestContext& ctx) {
    const bool button_pressed = false;

    LOG_DEBUG(Service_HID, "(STUBBED) called, is_enabled={}",
              button_pressed); // Spams a lot when controller applet is open

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push(button_pressed);
}

void IHidSystemServer::InitializeFirmwareUpdate(HLERequestContext& ctx) {
    LOG_WARNING(Service_HID, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IHidSystemServer::CheckFirmwareUpdateRequired(HLERequestContext& ctx) {
    LOG_WARNING(Service_HID, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IHidSystemServer::SetFirmwareHotfixUpdateSkipEnabled(HLERequestContext& ctx) {
    LOG_WARNING(Service_HID, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IHidSystemServer::InitializeUsbFirmwareUpdate(HLERequestContext& ctx) {
    LOG_WARNING(Service_HID, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IHidSystemServer::FinalizeUsbFirmwareUpdate(HLERequestContext& ctx) {
    LOG_WARNING(Service_HID, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IHidSystemServer::CheckUsbFirmwareUpdateRequired(HLERequestContext& ctx) {
    LOG_WARNING(Service_HID, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IHidSystemServer::InitializeUsbFirmwareUpdateWithoutMemory(HLERequestContext& ctx) {
    LOG_WARNING(Service_HID, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IHidSystemServer::SetTouchScreenMagnification(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto point1x{rp.Pop<f32>()};
    const auto point1y{rp.Pop<f32>()};
    const auto point2x{rp.Pop<f32>()};
    const auto point2y{rp.Pop<f32>()};

    LOG_INFO(Service_HID, "called, point1=-({},{}), point2=({},{})", point1x, point1y, point2x,
             point2y);

    const Result result = GetResourceManager()->GetTouchScreen()->SetTouchScreenMagnification(
        point1x, point1y, point2x, point2y);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result);
}

void IHidSystemServer::GetTouchScreenFirmwareVersion(HLERequestContext& ctx) {
    LOG_INFO(Service_HID, "called");

    Core::HID::FirmwareVersion firmware{};
    const auto result = GetResourceManager()->GetTouchScreenFirmwareVersion(firmware);

    IPC::ResponseBuilder rb{ctx, 6};
    rb.Push(result);
    rb.PushRaw(firmware);
}

void IHidSystemServer::SetTouchScreenDefaultConfiguration(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    auto touchscreen_config{rp.PopRaw<Core::HID::TouchScreenConfigurationForNx>()};

    LOG_INFO(Service_HID, "called, touchscreen_config={}", touchscreen_config.mode);

    if (touchscreen_config.mode != Core::HID::TouchScreenModeForNx::Heat2 &&
        touchscreen_config.mode != Core::HID::TouchScreenModeForNx::Finger) {
        touchscreen_config.mode = Core::HID::TouchScreenModeForNx::UseSystemSetting;
    }

    const Result result =
        GetResourceManager()->GetTouchScreen()->SetTouchScreenDefaultConfiguration(
            touchscreen_config);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result);
}

void IHidSystemServer::GetTouchScreenDefaultConfiguration(HLERequestContext& ctx) {
    LOG_INFO(Service_HID, "called");

    Core::HID::TouchScreenConfigurationForNx touchscreen_config{};
    const Result result =
        GetResourceManager()->GetTouchScreen()->GetTouchScreenDefaultConfiguration(
            touchscreen_config);

    if (touchscreen_config.mode != Core::HID::TouchScreenModeForNx::Heat2 &&
        touchscreen_config.mode != Core::HID::TouchScreenModeForNx::Finger) {
        touchscreen_config.mode = Core::HID::TouchScreenModeForNx::UseSystemSetting;
    }

    IPC::ResponseBuilder rb{ctx, 6};
    rb.Push(result);
    rb.PushRaw(touchscreen_config);
}

void IHidSystemServer::SetForceHandheldStyleVibration(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto is_forced{rp.Pop<bool>()};

    LOG_INFO(Service_HID, "called, is_forced={}", is_forced);

    GetResourceManager()->SetForceHandheldStyleVibration(is_forced);
    GetResourceManager()->GetNpad()->UpdateHandheldAbstractState();

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IHidSystemServer::IsUsingCustomButtonConfig(HLERequestContext& ctx) {
    const bool is_enabled = false;

    LOG_DEBUG(Service_HID, "(STUBBED) called, is_enabled={}", is_enabled);

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push(is_enabled);
}

void IHidSystemServer::IsAnyCustomButtonConfigEnabled(HLERequestContext& ctx) {
    const bool is_enabled = false;

    LOG_DEBUG(Service_HID, "(STUBBED) called, is_enabled={}", is_enabled);

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push(is_enabled);
}

std::shared_ptr<ResourceManager> IHidSystemServer::GetResourceManager() {
    resource_manager->Initialize();
    return resource_manager;
}

} // namespace Service::HID
