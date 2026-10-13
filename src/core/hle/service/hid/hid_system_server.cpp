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
