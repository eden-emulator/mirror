// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/logging.h"
#include "core/hle/service/btm/btm_system_core.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/set/system_settings_server.h"
#include "core/hle/service/sm/sm.h"

namespace Service::BTM {

IBtmSystemCore::IBtmSystemCore(Core::System& system_)
    : ServiceFramework{system_, "IBtmSystemCore"}, service_context{system_, "IBtmSystemCore"} {
    radio_event = service_context.CreateEvent("IBtmSystemCore::RadioEvent");
    audio_device_connection_event = service_context.CreateEvent("IBtmSystemCore::AudioDeviceConnectionEvent");
    m_set_sys = system.ServiceManager().GetService<Service::Set::ISystemSettingsServer>("set:sys", true);
}

IBtmSystemCore::~IBtmSystemCore() {
    service_context.CloseEvent(radio_event);
    service_context.CloseEvent(audio_device_connection_event);
}

Result IBtmSystemCore::StartGamepadPairing() {
    LOG_WARNING(Service_BTM, "(STUBBED) called");
    R_SUCCEED();
}

Result IBtmSystemCore::CancelGamepadPairing() {
    LOG_WARNING(Service_BTM, "(STUBBED) called");
    R_SUCCEED();
}

Result IBtmSystemCore::EnableRadio() {
    LOG_DEBUG(Service_BTM, "called");

    R_RETURN(m_set_sys->SetBluetoothEnableFlag(true));
}
Result IBtmSystemCore::DisableRadio() {
    LOG_DEBUG(Service_BTM, "called");

    R_RETURN(m_set_sys->SetBluetoothEnableFlag(false));
}

Result IBtmSystemCore::IsRadioEnabled(Out<bool> out_is_enabled) {
    LOG_DEBUG(Service_BTM, "called");

    R_RETURN(m_set_sys->GetBluetoothEnableFlag(out_is_enabled));
}

Result IBtmSystemCore::AcquireRadioEvent(Out<bool> out_is_valid,
                                         OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Service_BTM, "(STUBBED) called");

    *out_is_valid = true;
    *out_event = &radio_event->GetReadableEvent();
    R_SUCCEED();
}

Result IBtmSystemCore::GetDiscoveredAudioDevice(OutArray<std::array<u8, 0xFF>, BufferAttr_HipcPointer> out_audio_devices, s32 count, Out<s32> out_total) {
    LOG_WARNING(Service_BTM, "(STUBBED) called");
    R_SUCCEED();
}

Result IBtmSystemCore::AcquireAudioDeviceConnectionEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Service_BTM, "(STUBBED) called");

    *out_event = &audio_device_connection_event->GetReadableEvent();
    R_SUCCEED();
}

Result IBtmSystemCore::GetConnectedAudioDevices(
    Out<s32> out_count, OutArray<std::array<u8, 0xFF>, BufferAttr_HipcPointer> out_audio_devices) {
    LOG_WARNING(Service_BTM, "(STUBBED) called");

    *out_count = 0;
    R_SUCCEED();
}

Result IBtmSystemCore::GetPairedAudioDevices(
    Out<s32> out_count, OutArray<std::array<u8, 0xFF>, BufferAttr_HipcPointer> out_audio_devices) {
    LOG_WARNING(Service_BTM, "(STUBBED) called");

    *out_count = 0;
    R_SUCCEED();
}

Result IBtmSystemCore::RequestAudioDeviceConnectionRejection(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_BTM, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

Result IBtmSystemCore::CancelAudioDeviceConnectionRejection(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_BTM, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

} // namespace Service::BTM
