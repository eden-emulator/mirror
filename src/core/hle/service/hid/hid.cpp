// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/service/hid/hid.h"
#include "core/hle/service/hid/hid_debug_server.h"
#include "core/hle/service/hid/hid_server.h"
#include "core/hle/service/hid/hid_system_server.h"
#include "core/hle/service/hid/hidbus.h"
#include "core/hle/service/hid/irs.h"
#include "core/hle/service/hid/xcd.h"
#include "core/hle/service/server_manager.h"
#include "hid_core/resource_manager.h"
#include "hid_core/resources/hid_firmware_settings.h"
#include "core/hle/service/ipc_helpers.h"
#include "hid_core/hid_types.h"
#include "hid_core/resources/touch_screen/gesture.h"
#include "hid_core/resources/touch_screen/touch_screen.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::HID {

class IHidTemporaryServer final : public ServiceFramework<IHidTemporaryServer> {
public:
    explicit IHidTemporaryServer(Core::System& system_)
    : ServiceFramework{system_, "hid:tmp"} {}

    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, nullptr, "GetConsoleSixAxisSensorCalibrationValues"}
    );
    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    ~IHidTemporaryServer() override = default;
};

class AHID_CD final : public ServiceFramework<AHID_CD> {
public:
    explicit AHID_CD(Core::System& system_)
    : ServiceFramework{system_, "ahid:cd"} {}

    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, nullptr, "AcquireDevice"},
        FunctionInfo{1, nullptr, "ReleaseDevice"},
        FunctionInfo{2, nullptr, "GetCtrlSession"},
        FunctionInfo{3, nullptr, "GetReadSession"},
        FunctionInfo{4, nullptr, "GetWriteSession"}
    );
    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    ~AHID_CD() override = default;
};

class AHID_HDR final : public ServiceFramework<AHID_HDR> {
public:
    explicit AHID_HDR(Core::System& system_)
    : ServiceFramework{system_, "ahid:hdr"} {}

    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, nullptr, "GetDeviceEntries"},
        FunctionInfo{1, nullptr, "GetDeviceList"},
        FunctionInfo{2, nullptr, "GetDeviceParameters"},
        FunctionInfo{3, nullptr, "AttachDevice"},
        FunctionInfo{4, nullptr, "DetachDevice"},
        FunctionInfo{5, nullptr, "SetDeviceFilter"}
    );
    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    ~AHID_HDR() override = default;
};

class IHidDebugServer final : public ServiceFramework<IHidDebugServer> {
public:
    explicit IHidDebugServer(Core::System& system_, std::shared_ptr<ResourceManager> resource,
                                    std::shared_ptr<HidFirmwareSettings> settings)
        : ServiceFramework{system_, "hid:dbg"}
        , resource_manager{resource}
        , firmware_settings{settings}
    {}

    ~IHidDebugServer() override = default;

    Result DeactivateTouchScreen() {
        LOG_INFO(Service_HID, "called");

        if (!firmware_settings->IsDeviceManaged()) {
            R_RETURN(GetResourceManager()->GetTouchScreen()->Deactivate());
        }

        R_SUCCEED();
    }

    Result SetTouchScreenAutoPilotState(
        InArray<TouchState, BufferAttr_HipcMapAlias> auto_pilot_buffer) {
        AutoPilotState auto_pilot{};

        auto_pilot.count =
            static_cast<u64>((std::min)(auto_pilot_buffer.size(), auto_pilot.state.size()));
        memcpy(auto_pilot.state.data(), auto_pilot_buffer.data(),
            auto_pilot.count * sizeof(TouchState));

        LOG_INFO(Service_HID, "called, auto_pilot_count={}", auto_pilot.count);

        R_RETURN(GetResourceManager()->GetTouchScreen()->SetTouchScreenAutoPilotState(auto_pilot));
    }

    Result UnsetTouchScreenAutoPilotState() {
        LOG_INFO(Service_HID, "called");
        R_RETURN(GetResourceManager()->GetTouchScreen()->UnsetTouchScreenAutoPilotState());
    }

    Result GetTouchScreenConfiguration(
        Out<Core::HID::TouchScreenConfigurationForNx> out_touchscreen_config,
        ClientAppletResourceUserId aruid) {
        LOG_INFO(Service_HID, "called, applet_resource_user_id={}", aruid.pid);

        R_TRY(GetResourceManager()->GetTouchScreen()->GetTouchScreenConfiguration(
            *out_touchscreen_config, aruid.pid));

        if (out_touchscreen_config->mode != Core::HID::TouchScreenModeForNx::Heat2 &&
            out_touchscreen_config->mode != Core::HID::TouchScreenModeForNx::Finger) {
            out_touchscreen_config->mode = Core::HID::TouchScreenModeForNx::UseSystemSetting;
        }

        R_SUCCEED();
    }

    Result ProcessTouchScreenAutoTune() {
        LOG_INFO(Service_HID, "called");
        R_RETURN(GetResourceManager()->GetTouchScreen()->ProcessTouchScreenAutoTune());
    }

    Result ForceStopTouchScreenManagement() {
        LOG_INFO(Service_HID, "called");

        if (!firmware_settings->IsDeviceManaged()) {
            R_SUCCEED();
        }

        auto touch_screen = GetResourceManager()->GetTouchScreen();
        auto gesture = GetResourceManager()->GetGesture();

        if (firmware_settings->IsTouchI2cManaged()) {
            bool is_touch_active{};
            bool is_gesture_active{};
            R_TRY(touch_screen->IsActive(is_touch_active));
            R_TRY(gesture->IsActive(is_gesture_active));

            if (is_touch_active) {
                R_TRY(touch_screen->Deactivate());
            }
            if (is_gesture_active) {
                R_TRY(gesture->Deactivate());
            }
        }

        R_SUCCEED();
    }

    Result ForceRestartTouchScreenManagement(u32 basic_gesture_id,
                                                            ClientAppletResourceUserId aruid) {
        LOG_INFO(Service_HID, "called, basic_gesture_id={}, applet_resource_user_id={}",
                basic_gesture_id, aruid.pid);

        auto touch_screen = GetResourceManager()->GetTouchScreen();
        auto gesture = GetResourceManager()->GetGesture();

        if (firmware_settings->IsDeviceManaged() && firmware_settings->IsTouchI2cManaged()) {
            R_TRY(gesture->Activate());
            R_TRY(gesture->Activate(aruid.pid, basic_gesture_id));
            R_TRY(touch_screen->Activate());
            R_TRY(touch_screen->Activate(aruid.pid));
        }

        R_SUCCEED();
    }

    Result IsTouchScreenManaged(Out<bool> out_is_managed) {
        LOG_INFO(Service_HID, "called");

        bool is_touch_active{};
        bool is_gesture_active{};
        R_TRY(GetResourceManager()->GetTouchScreen()->IsActive(is_touch_active));
        R_TRY(GetResourceManager()->GetGesture()->IsActive(is_gesture_active));

        *out_is_managed = is_touch_active || is_gesture_active;
        R_SUCCEED();
    }

    Result DeactivateGesture() {
        LOG_INFO(Service_HID, "called");

        if (!firmware_settings->IsDeviceManaged()) {
            R_RETURN(GetResourceManager()->GetGesture()->Deactivate());
        }

        R_SUCCEED();
    }

    std::shared_ptr<ResourceManager> GetResourceManager() {
        resource_manager->Initialize();
        return resource_manager;
    }

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, nullptr, "DeactivateDebugPad"},
        FunctionInfo{1, nullptr, "SetDebugPadAutoPilotState"},
        FunctionInfo{2, nullptr, "UnsetDebugPadAutoPilotState"},
        FunctionInfo{10, C<&IHidDebugServer::DeactivateTouchScreen>, "DeactivateTouchScreen"},
        FunctionInfo{11, C<&IHidDebugServer::SetTouchScreenAutoPilotState>, "SetTouchScreenAutoPilotState"},
        FunctionInfo{12, C<&IHidDebugServer::UnsetTouchScreenAutoPilotState>, "UnsetTouchScreenAutoPilotState"},
        FunctionInfo{13, C<&IHidDebugServer::GetTouchScreenConfiguration>, "GetTouchScreenConfiguration"},
        FunctionInfo{14, C<&IHidDebugServer::ProcessTouchScreenAutoTune>, "ProcessTouchScreenAutoTune"},
        FunctionInfo{15, C<&IHidDebugServer::ForceStopTouchScreenManagement>, "ForceStopTouchScreenManagement"},
        FunctionInfo{16, C<&IHidDebugServer::ForceRestartTouchScreenManagement>, "ForceRestartTouchScreenManagement"},
        FunctionInfo{17, C<&IHidDebugServer::IsTouchScreenManaged>, "IsTouchScreenManaged"},
        FunctionInfo{20, nullptr, "DeactivateMouse"},
        FunctionInfo{21, nullptr, "SetMouseAutoPilotState"},
        FunctionInfo{22, nullptr, "UnsetMouseAutoPilotState"},
        FunctionInfo{23, nullptr, "AddMouseSideWheelDelta"},
        FunctionInfo{25, nullptr, "SetDebugMouseAutoPilotState"},
        FunctionInfo{26, nullptr, "UnsetDebugMouseAutoPilotState"},
        FunctionInfo{30, nullptr, "DeactivateKeyboard"},
        FunctionInfo{31, nullptr, "SetKeyboardAutoPilotState"},
        FunctionInfo{32, nullptr, "UnsetKeyboardAutoPilotState"},
        FunctionInfo{50, nullptr, "DeactivateXpad"},
        FunctionInfo{51, nullptr, "SetXpadAutoPilotState"},
        FunctionInfo{52, nullptr, "UnsetXpadAutoPilotState"},
        FunctionInfo{53, nullptr, "DeactivateJoyXpad"},
        FunctionInfo{60, nullptr, "ClearNpadSystemCommonPolicy"},
        FunctionInfo{61, nullptr, "DeactivateNpad"},
        FunctionInfo{62, nullptr, "ForceDisconnectNpad"},
        FunctionInfo{91, C<&IHidDebugServer::DeactivateGesture>, "DeactivateGesture"},
        FunctionInfo{110, nullptr, "DeactivateHomeButton"},
        FunctionInfo{111, nullptr, "SetHomeButtonAutoPilotState"},
        FunctionInfo{112, nullptr, "UnsetHomeButtonAutoPilotState"},
        FunctionInfo{120, nullptr, "DeactivateSleepButton"},
        FunctionInfo{121, nullptr, "SetSleepButtonAutoPilotState"},
        FunctionInfo{122, nullptr, "UnsetSleepButtonAutoPilotState"},
        FunctionInfo{123, nullptr, "DeactivateInputDetector"},
        FunctionInfo{130, nullptr, "DeactivateCaptureButton"},
        FunctionInfo{131, nullptr, "SetCaptureButtonAutoPilotState"},
        FunctionInfo{132, nullptr, "UnsetCaptureButtonAutoPilotState"},
        FunctionInfo{133, nullptr, "SetShiftAccelerometerCalibrationValue"},
        FunctionInfo{134, nullptr, "GetShiftAccelerometerCalibrationValue"},
        FunctionInfo{135, nullptr, "SetShiftGyroscopeCalibrationValue"},
        FunctionInfo{136, nullptr, "GetShiftGyroscopeCalibrationValue"},
        FunctionInfo{140, nullptr, "DeactivateConsoleSixAxisSensor"},
        FunctionInfo{141, nullptr, "GetConsoleSixAxisSensorSamplingFrequency"},
        FunctionInfo{142, nullptr, "DeactivateSevenSixAxisSensor"},
        FunctionInfo{143, nullptr, "GetConsoleSixAxisSensorCountStates"},
        FunctionInfo{144, nullptr, "GetAccelerometerFsr"},
        FunctionInfo{145, nullptr, "SetAccelerometerFsr"},
        FunctionInfo{146, nullptr, "GetAccelerometerOdr"},
        FunctionInfo{147, nullptr, "SetAccelerometerOdr"},
        FunctionInfo{148, nullptr, "GetGyroscopeFsr"},
        FunctionInfo{149, nullptr, "SetGyroscopeFsr"},
        FunctionInfo{150, nullptr, "GetGyroscopeOdr"},
        FunctionInfo{151, nullptr, "SetGyroscopeOdr"},
        FunctionInfo{152, nullptr, "GetWhoAmI"},
        FunctionInfo{201, nullptr, "ActivateFirmwareUpdate"},
        FunctionInfo{202, nullptr, "DeactivateFirmwareUpdate"},
        FunctionInfo{203, nullptr, "StartFirmwareUpdate"},
        FunctionInfo{204, nullptr, "GetFirmwareUpdateStage"},
        FunctionInfo{205, nullptr, "GetFirmwareVersion"},
        FunctionInfo{206, nullptr, "GetDestinationFirmwareVersion"},
        FunctionInfo{207, nullptr, "DiscardFirmwareInfoCacheForRevert"},
        FunctionInfo{208, nullptr, "StartFirmwareUpdateForRevert"},
        FunctionInfo{209, nullptr, "GetAvailableFirmwareVersionForRevert"},
        FunctionInfo{210, nullptr, "IsFirmwareUpdatingDevice"},
        FunctionInfo{211, nullptr, "StartFirmwareUpdateIndividual"},
        FunctionInfo{212, nullptr, "GetDetailFirmwareVersion"}, // 19.0.0+
        FunctionInfo{215, nullptr, "SetUsbFirmwareForceUpdateEnabled"},
        FunctionInfo{216, nullptr, "SetAllKuinaDevicesToFirmwareUpdateMode"},
        FunctionInfo{221, nullptr, "UpdateControllerColor"},
        FunctionInfo{222, nullptr, "ConnectUsbPadsAsync"},
        FunctionInfo{223, nullptr, "DisconnectUsbPadsAsync"},
        FunctionInfo{224, nullptr, "UpdateDesignInfo"},
        FunctionInfo{225, nullptr, "GetUniquePadDriverState"},
        FunctionInfo{226, nullptr, "GetSixAxisSensorDriverStates"},
        FunctionInfo{227, nullptr, "GetRxPacketHistory"},
        FunctionInfo{228, nullptr, "AcquireOperationEventHandle"},
        FunctionInfo{229, nullptr, "ReadSerialFlash"},
        FunctionInfo{230, nullptr, "WriteSerialFlash"},
        FunctionInfo{231, nullptr, "GetOperationResult"},
        FunctionInfo{232, nullptr, "EnableShipmentMode"},
        FunctionInfo{233, nullptr, "ClearPairingInfo"},
        FunctionInfo{234, nullptr, "GetUniquePadDeviceTypeSetInternal"},
        FunctionInfo{235, nullptr, "EnableAnalogStickPower"},
        FunctionInfo{236, nullptr, "RequestKuinaUartClockCal"},
        FunctionInfo{237, nullptr, "GetKuinaUartClockCal"},
        FunctionInfo{238, nullptr, "SetKuinaUartClockTrim"},
        FunctionInfo{239, nullptr, "KuinaLoopbackTest"},
        FunctionInfo{240, nullptr, "RequestBatteryVoltage"},
        FunctionInfo{241, nullptr, "GetBatteryVoltage"},
        FunctionInfo{242, nullptr, "GetUniquePadPowerInfo"},
        FunctionInfo{243, nullptr, "RebootUniquePad"},
        FunctionInfo{244, nullptr, "RequestKuinaFirmwareVersion"},
        FunctionInfo{245, nullptr, "GetKuinaFirmwareVersion"},
        FunctionInfo{246, nullptr, "GetVidPid"},
        FunctionInfo{247, nullptr, "GetAnalogStickCalibrationValue"},
        FunctionInfo{248, nullptr, "GetUniquePadIdsFull"},
        FunctionInfo{249, nullptr, "ConnectUniquePad"},
        FunctionInfo{250, nullptr, "IsVirtual"},
        FunctionInfo{251, nullptr, "GetAnalogStickModuleParam"},
        FunctionInfo{253, nullptr, "ClearStorageForShipment"}, //19.0.0+
        FunctionInfo{261, nullptr, "UpdateDesignInfo12"}, //21.0.0+
        FunctionInfo{262, nullptr, "GetUniquePadButtonCount"}, //21.0.0+
        FunctionInfo{267, nullptr, "SetAnalogStickCalibration"}, //21.0.0+
        FunctionInfo{268, nullptr, "ResetAnalogStickCalibration"}, //21.0.0+
        FunctionInfo{301, nullptr, "GetAbstractedPadHandles"},
        FunctionInfo{302, nullptr, "GetAbstractedPadState"},
        FunctionInfo{303, nullptr, "GetAbstractedPadsState"},
        FunctionInfo{321, nullptr, "SetAutoPilotVirtualPadState"},
        FunctionInfo{322, nullptr, "UnsetAutoPilotVirtualPadState"},
        FunctionInfo{323, nullptr, "UnsetAllAutoPilotVirtualPadState"},
        FunctionInfo{324, nullptr, "AttachHdlsWorkBuffer"},
        FunctionInfo{325, nullptr, "ReleaseHdlsWorkBuffer"},
        FunctionInfo{326, nullptr, "DumpHdlsNpadAssignmentState"},
        FunctionInfo{327, nullptr, "DumpHdlsStates"},
        FunctionInfo{328, nullptr, "ApplyHdlsNpadAssignmentState"},
        FunctionInfo{329, nullptr, "ApplyHdlsStateList"},
        FunctionInfo{330, nullptr, "AttachHdlsVirtualDevice"},
        FunctionInfo{331, nullptr, "DetachHdlsVirtualDevice"},
        FunctionInfo{332, nullptr, "SetHdlsState"},
        FunctionInfo{350, nullptr, "AddRegisteredDevice"},
        FunctionInfo{351, nullptr, "GetRegisteredDevicesCountDebug"}, //17.0.0-18.1.0
        FunctionInfo{352, nullptr, "DeleteRegisteredDevicesDebug"}, //17.0.0-18.1.0
        FunctionInfo{400, nullptr, "DisableExternalMcuOnNxDevice"},
        FunctionInfo{401, nullptr, "DisableRailDeviceFiltering"},
        FunctionInfo{402, nullptr, "EnableWiredPairing"},
        FunctionInfo{403, nullptr, "EnableShipmentModeAutoClear"},
        FunctionInfo{404, nullptr, "SetRailEnabled"},
        FunctionInfo{500, nullptr, "SetFactoryInt"},
        FunctionInfo{501, nullptr, "IsFactoryBootEnabled"},
        FunctionInfo{550, nullptr, "SetAnalogStickModelDataTemporarily"},
        FunctionInfo{551, nullptr, "GetAnalogStickModelData"},
        FunctionInfo{552, nullptr, "ResetAnalogStickModelData"},
        FunctionInfo{600, nullptr, "ConvertPadState"},
        FunctionInfo{601, nullptr, "IsButtonConfigSupported"}, //18.0.0+
        FunctionInfo{602, nullptr, "IsButtonConfigEmbeddedSupported"}, //18.0.0+
        FunctionInfo{603, nullptr, "DeleteButtonConfig"}, //18.0.0+
        FunctionInfo{604, nullptr, "DeleteButtonConfigEmbedded"}, //18.0.0+
        FunctionInfo{605, nullptr, "SetButtonConfigEnabled"}, //18.0.0+
        FunctionInfo{606, nullptr, "SetButtonConfigEmbeddedEnabled"}, //18.0.0+
        FunctionInfo{607, nullptr, "IsButtonConfigEnabled"}, //18.0.0+
        FunctionInfo{608, nullptr, "IsButtonConfigEmbeddedEnabled"}, //18.0.0+
        FunctionInfo{609, nullptr, "SetButtonConfigEmbedded"}, //18.0.0+
        FunctionInfo{610, nullptr, "SetButtonConfigFull"}, //18.0.0+
        FunctionInfo{611, nullptr, "SetButtonConfigLeft"}, //18.0.0+
        FunctionInfo{612, nullptr, "SetButtonConfigRight"}, //18.0.0+
        FunctionInfo{613, nullptr, "GetButtonConfigEmbedded"}, //18.0.0+
        FunctionInfo{614, nullptr, "GetButtonConfigFull"}, //18.0.0+
        FunctionInfo{615, nullptr, "GetButtonConfigLeft"}, //18.0.0+
        FunctionInfo{616, nullptr, "GetButtonConfigRight"}, //18.0.0+
        FunctionInfo{650, nullptr, "AddButtonPlayData"},
        FunctionInfo{651, nullptr, "StartButtonPlayData"},
        FunctionInfo{652, nullptr, "StopButtonPlayData"},
        FunctionInfo{700, nullptr, "GetRailAttachEventCount"}, //21.0.0+
        FunctionInfo{2000, nullptr, "DeactivateDigitizer"},
        FunctionInfo{2001, nullptr, "SetDigitizerAutoPilotState"},
        FunctionInfo{2002, nullptr, "UnsetDigitizerAutoPilotState"},
        FunctionInfo{3000, nullptr, "ReloadFirmwareDebugSettings"}
    );
    std::shared_ptr<ResourceManager> resource_manager;
    std::shared_ptr<HidFirmwareSettings> firmware_settings;
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);
    auto firmware_settings = std::make_shared<HidFirmwareSettings>(system);
    auto resource_manager = std::make_shared<ResourceManager>(system, firmware_settings);

    resource_manager->Initialize();

    server_manager->RegisterNamedService("hid", std::make_shared<IHidServer>(system, resource_manager, firmware_settings));
    server_manager->RegisterNamedService("hid:dbg", std::make_shared<IHidDebugServer>(system, resource_manager, firmware_settings));
    server_manager->RegisterNamedService("hid:sys", std::make_shared<IHidSystemServer>(system, resource_manager, firmware_settings));
    server_manager->RegisterNamedService("hid:tmp", std::make_shared<IHidTemporaryServer>(system));

    server_manager->RegisterNamedService("hidbus", std::make_shared<Hidbus>(system));

    server_manager->RegisterNamedService("irs", std::make_shared<IRS::IRS>(system));
    server_manager->RegisterNamedService("irs:sys", std::make_shared<IRS::IRS_SYS>(system));

    server_manager->RegisterNamedService("ahid:cd", std::make_shared<AHID_CD>(system));
    server_manager->RegisterNamedService("ahid:hdr", std::make_shared<AHID_HDR>(system));

    server_manager->RegisterNamedService("xcd:sys", std::make_shared<XCD_SYS>(system));

    system.RunServer(std::move(server_manager));
}

} // namespace Service::HID
