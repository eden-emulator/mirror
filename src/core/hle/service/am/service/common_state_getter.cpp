// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/settings.h"
#include "core/hle/service/am/am_results.h"
#include "core/hle/service/am/applet.h"
#include "core/hle/service/am/service/common_state_getter.h"
#include "core/hle/service/am/service/lock_accessor.h"
#include "core/hle/service/am/service/storage.h"
#include "core/hle/service/apm/apm_interface.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/pm/pm.h"
#include "core/hle/service/sm/sm.h"
#include "core/hle/service/vi/vi_types.h"

namespace Service::AM {

ICommonStateGetter::ICommonStateGetter(Core::System& system_, std::shared_ptr<Applet> applet)
    : ServiceFramework{system_, "ICommonStateGetter"}, m_applet{std::move(applet)} {
}

ICommonStateGetter::~ICommonStateGetter() = default;

Result ICommonStateGetter::GetEventHandle(OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_AM, "called");
    *out_event = m_applet->lifecycle_manager.GetSystemEvent().GetHandle();
    R_SUCCEED();
}

Result ICommonStateGetter::ReceiveMessage(Out<AppletMessage> out_applet_message) {
    LOG_DEBUG(Service_AM, "called");

    if (!m_applet->lifecycle_manager.PopMessage(system.Kernel(), out_applet_message)) {
        LOG_ERROR(Service_AM, "Tried to pop message but none was available!");
        R_THROW(AM::ResultNoMessages);
    }

    LOG_DEBUG(Service_AM, "called, returning message={} to applet_id={}",
             static_cast<u32>(*out_applet_message), static_cast<u32>(m_applet->applet_id));

    R_SUCCEED();
}

Result ICommonStateGetter::GetCurrentFocusState(Out<FocusState> out_focus_state) {
    LOG_DEBUG(Service_AM, "called");

    std::scoped_lock lk{m_applet->lock};
    *out_focus_state = m_applet->lifecycle_manager.GetAndClearFocusState();

    R_SUCCEED();
}

Result ICommonStateGetter::RequestToAcquireSleepLock() {
    LOG_WARNING(Service_AM, "(STUBBED) called");

    // Sleep lock is acquired immediately.
    m_applet->sleep_lock_event.Signal(system.Kernel());
    R_SUCCEED();
}

Result ICommonStateGetter::ReleaseSleepLock() {
    LOG_WARNING(Service_AM, "(STUBBED) called");

    m_applet->sleep_lock_event.Clear(system.Kernel());
    R_SUCCEED();
}

Result ICommonStateGetter::ReleaseSleepLockTransiently() {
    LOG_WARNING(Service_AM, "(STUBBED) called");

    m_applet->sleep_lock_event.Clear(system.Kernel());
    R_SUCCEED();
}

Result ICommonStateGetter::GetAcquiredSleepLockEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Service_AM, "called");
    *out_event = m_applet->sleep_lock_event.GetHandle();
    R_SUCCEED();
}

Result ICommonStateGetter::GetReaderLockAccessorEx(
    Out<SharedPointer<ILockAccessor>> out_lock_accessor, u32 button_type) {
    LOG_INFO(Service_AM, "called, button_type={}", button_type);
    *out_lock_accessor = std::make_shared<ILockAccessor>(system);
    R_SUCCEED();
}

Result ICommonStateGetter::GetWriterLockAccessorEx(
    Out<SharedPointer<ILockAccessor>> out_lock_accessor, u32 button_type) {
    LOG_INFO(Service_AM, "called, button_type={}", button_type);
    *out_lock_accessor = std::make_shared<ILockAccessor>(system);
    R_SUCCEED();
}

Result ICommonStateGetter::GetDefaultDisplayResolutionChangeEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_AM, "called");
    *out_event = m_applet->lifecycle_manager.GetOperationModeChangedSystemEvent().GetHandle();
    R_SUCCEED();
}

Result ICommonStateGetter::GetHdcpAuthenticationState(Out<s32> out_state) {
    LOG_DEBUG(Service_AM, "called");
    *out_state = 1;
    R_SUCCEED();
}

Result ICommonStateGetter::GetHdcpAuthenticationStateChangeEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_AM, "called");
    *out_event = m_applet->lifecycle_manager.GetHDCPStateChangedEvent().GetHandle();
    R_SUCCEED();
}

Result ICommonStateGetter::GetOperationMode(Out<OperationMode> out_operation_mode) {
    const bool use_docked_mode{Settings::IsDockedMode()};
    LOG_DEBUG(Service_AM, "called, use_docked_mode={}", use_docked_mode);
    *out_operation_mode = use_docked_mode ? OperationMode::Docked : OperationMode::Handheld;
    R_SUCCEED();
}

Result ICommonStateGetter::GetPerformanceMode(Out<APM::PerformanceMode> out_performance_mode) {
    LOG_DEBUG(Service_AM, "called");
    *out_performance_mode = system.GetAPMController().GetCurrentPerformanceMode();
    R_SUCCEED();
}

Result ICommonStateGetter::GetBootMode(Out<PM::SystemBootMode> out_boot_mode) {
    LOG_DEBUG(Service_AM, "called");
    *out_boot_mode = Service::PM::SystemBootMode::Normal;
    R_SUCCEED();
}

Result ICommonStateGetter::IsVrModeEnabled(Out<bool> out_is_vr_mode_enabled) {
    LOG_DEBUG(Service_AM, "called");

    std::scoped_lock lk{m_applet->lock};
    *out_is_vr_mode_enabled = m_applet->vr_mode_enabled;
    R_SUCCEED();
}

Result ICommonStateGetter::SetVrModeEnabled(bool is_vr_mode_enabled) {
    std::scoped_lock lk{m_applet->lock};
    m_applet->vr_mode_enabled = is_vr_mode_enabled;
    LOG_WARNING(Service_AM, "VR Mode is {}", m_applet->vr_mode_enabled ? "on" : "off");
    R_SUCCEED();
}

Result ICommonStateGetter::SetLcdBacklighOffEnabled(bool is_lcd_backlight_off_enabled) {
    LOG_WARNING(Service_AM, "(STUBBED) called. is_lcd_backlight_off_enabled={}",
                is_lcd_backlight_off_enabled);
    R_SUCCEED();
}

Result ICommonStateGetter::BeginVrModeEx() {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    std::scoped_lock lk{m_applet->lock};
    m_applet->vr_mode_enabled = true;
    R_SUCCEED();
}

Result ICommonStateGetter::EndVrModeEx() {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    std::scoped_lock lk{m_applet->lock};
    m_applet->vr_mode_enabled = false;
    R_SUCCEED();
}

Result ICommonStateGetter::IsInControllerFirmwareUpdateSection(
    Out<bool> out_is_in_controller_firmware_update_section) {
    LOG_INFO(Service_AM, "called");
    *out_is_in_controller_firmware_update_section = false;
    R_SUCCEED();
}

Result ICommonStateGetter::GetDefaultDisplayResolution(Out<s32> out_width, Out<s32> out_height) {
    LOG_DEBUG(Service_AM, "called");

    if (Settings::IsDockedMode()) {
        *out_width = static_cast<u32>(Service::VI::DisplayResolution::DockedWidth);
        *out_height = static_cast<u32>(Service::VI::DisplayResolution::DockedHeight);
    } else {
        *out_width = static_cast<u32>(Service::VI::DisplayResolution::UndockedWidth);
        *out_height = static_cast<u32>(Service::VI::DisplayResolution::UndockedHeight);
    }

    R_SUCCEED();
}

void ICommonStateGetter::SetCpuBoostMode(HLERequestContext& ctx) {
    LOG_DEBUG(Service_AM, "called, forwarding to APM:SYS");

    const auto& sm = system.ServiceManager();
    const auto apm_sys = sm.GetService<APM::APM_Sys>("apm:sys");
    ASSERT(apm_sys != nullptr);

    apm_sys->SetCpuBoostMode(ctx);
}

Result ICommonStateGetter::GetBuiltInDisplayType(Out<s32> out_display_type) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    *out_display_type = 0;
    R_SUCCEED();
}

Result ICommonStateGetter::PerformSystemButtonPressingIfInFocus(SystemButtonType type) {
    LOG_DEBUG(Service_AM, "called, type={}", type);

    std::scoped_lock lk{m_applet->lock};

    switch (type) {
    case SystemButtonType::HomeButtonShortPressing:
        if (!m_applet->home_button_short_pressed_blocked) {
            m_applet->lifecycle_manager.PushUnorderedMessage(system.Kernel(),
                AppletMessage::DetectShortPressingHomeButton);
        }
        break;
    case SystemButtonType::HomeButtonLongPressing:
        if (!m_applet->home_button_long_pressed_blocked) {
            m_applet->lifecycle_manager.PushUnorderedMessage(system.Kernel(),
                AppletMessage::DetectLongPressingHomeButton);
        }
        break;
    case SystemButtonType::CaptureButtonShortPressing:
        if (m_applet->handling_capture_button_short_pressed_message_enabled_for_applet) {
            m_applet->lifecycle_manager.PushUnorderedMessage(system.Kernel(),
                AppletMessage::DetectShortPressingCaptureButton);
        }
        break;
    case SystemButtonType::CaptureButtonLongPressing:
        if (m_applet->handling_capture_button_long_pressed_message_enabled_for_applet) {
            m_applet->lifecycle_manager.PushUnorderedMessage(system.Kernel(),
                AppletMessage::DetectLongPressingCaptureButton);
        }
        break;
    default:
        // Other buttons ignored for now
        break;
    }

    R_SUCCEED();
}

Result ICommonStateGetter::EnableStartupLogoDisappearedMessage() {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    m_applet->lifecycle_manager.PushUnorderedMessage(system.Kernel(), AppletMessage::StartupLogoDisappeared);
    R_SUCCEED();
}

Result ICommonStateGetter::GetOperationModeSystemInfo(Out<u32> out_operation_mode_system_info) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    *out_operation_mode_system_info = 0;
    R_SUCCEED();
}

Result ICommonStateGetter::GetAppletLaunchedHistory(
    Out<s32> out_count, OutArray<AppletId, BufferAttr_HipcMapAlias> out_applet_ids) {
    LOG_INFO(Service_AM, "called");

    std::shared_ptr<Applet> current_applet = m_applet;

    for (*out_count = 0;
         *out_count < static_cast<s32>(out_applet_ids.size()) && current_applet != nullptr;
         /* ... */) {
        out_applet_ids[(*out_count)++] = current_applet->applet_id;
        current_applet = current_applet->caller_applet.lock();
    }

    R_SUCCEED();
}

Result ICommonStateGetter::GetSettingsPlatformRegion(
    Out<Set::PlatformRegion> out_settings_platform_region) {
    LOG_INFO(Service_AM, "called");
    *out_settings_platform_region = Set::PlatformRegion::Global;
    R_SUCCEED();
}

Result ICommonStateGetter::SetRequestExitToLibraryAppletAtExecuteNextProgramEnabled() {
    LOG_WARNING(Service_AM, "(STUBBED) called");

    std::scoped_lock lk{m_applet->lock};
    m_applet->request_exit_to_library_applet_at_execute_next_program_enabled = true;

    R_SUCCEED();
}

Result ICommonStateGetter::PushToGeneralChannel(SharedPointer<IStorage> storage) {
    LOG_DEBUG(Service_AM, "called");
    system.PushGeneralChannelData(storage->GetData());
    R_SUCCEED();
}

Result ICommonStateGetter::GetHomeButtonReaderLockAccessor(Out<SharedPointer<ILockAccessor>> out_lock_accessor) {
    LOG_DEBUG(Service_AM, "called");
    *out_lock_accessor = std::make_shared<ILockAccessor>(system);
    R_SUCCEED();
}

Result ICommonStateGetter::SetHandlingHomeButtonShortPressedEnabled(bool enabled) {
    LOG_DEBUG(Service_AM, "called, enabled={} applet_id={}", enabled, m_applet->applet_id);

    std::scoped_lock lk{m_applet->lock};
    m_applet->home_button_short_pressed_blocked = !enabled;
    R_SUCCEED();
}

Result ICommonStateGetter::Unknown610(u64 unk) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    R_SUCCEED();
}

Result ICommonStateGetter::Unknown611(u8 unk) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    R_SUCCEED();
}

Result ICommonStateGetter::BeginVrMode3d() {
    std::scoped_lock lk{m_applet->lock};
    m_applet->vr_mode_enabled_3d = true;
    LOG_WARNING(Service_AM, "VR Mode is {}", m_applet->vr_mode_enabled_3d ? "on" : "off");
    R_SUCCEED();
}

Result ICommonStateGetter::EndVrMode3d() {
    std::scoped_lock lk{m_applet->lock};
    m_applet->vr_mode_enabled_3d = false;
    LOG_WARNING(Service_AM, "VR Mode is {}", m_applet->vr_mode_enabled_3d ? "on" : "off");
    R_SUCCEED();
}

Result ICommonStateGetter::IsVrModeEnabled3d(Out<bool> out_is_vr_mode_enabled_3d) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    std::scoped_lock lk{m_applet->lock};
    *out_is_vr_mode_enabled_3d = m_applet->vr_mode_enabled_3d;
    R_SUCCEED();
}

Result ICommonStateGetter::GetVrLaboGoggleViewport(Out<s32> out_x, Out<s32> out_y, Out<s32> out_width, Out<s32> out_height) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    *out_x = 0;
    *out_y = 0;
    *out_width = 1280;
    *out_height = 720;
    R_SUCCEED();
}

Result ICommonStateGetter::GetPanelPhysicalSizeForSpecificTitle(Out<f32> out_width, Out<f32> out_height) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    *out_width = 137250.0f / 1000.0f;
    *out_height = 77200.0f / 1000.0f;
    R_SUCCEED();
}

Result ICommonStateGetter::GetPanelResolutionForSpecificTitle(Out<s32> out_width, Out<s32> out_height) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    *out_width = 1280;
    *out_height = 720;
    R_SUCCEED();
}

} // namespace Service::AM
