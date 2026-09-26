// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/am/am_types.h"
#include "core/hle/service/apm/apm_controller.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/pm/pm.h"
#include "core/hle/service/service.h"
#include "core/hle/service/set/settings_types.h"

namespace Kernel {
class KReadableEvent;
}

namespace Service::AM {

struct Applet;
class ILockAccessor;
class IStorage;

class ICommonStateGetter final : public ServiceFramework<ICommonStateGetter> {
public:
    explicit ICommonStateGetter(Core::System& system_, std::shared_ptr<Applet> applet_);
    ~ICommonStateGetter() override;

private:
    Result GetEventHandle(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result ReceiveMessage(Out<AppletMessage> out_applet_message);
    Result GetCurrentFocusState(Out<FocusState> out_focus_state);
    Result RequestToAcquireSleepLock();
    Result ReleaseSleepLock();
    Result ReleaseSleepLockTransiently();
    Result GetAcquiredSleepLockEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result GetReaderLockAccessorEx(Out<SharedPointer<ILockAccessor>> out_lock_accessor,
                                   u32 button_type);
    Result GetWriterLockAccessorEx(Out<SharedPointer<ILockAccessor>> out_lock_accessor,
                                   u32 button_type);
    Result GetDefaultDisplayResolutionChangeEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result GetHdcpAuthenticationState(Out<s32> out_state);
    Result GetHdcpAuthenticationStateChangeEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result GetOperationMode(Out<OperationMode> out_operation_mode);
    Result GetPerformanceMode(Out<APM::PerformanceMode> out_performance_mode);
    Result GetBootMode(Out<PM::SystemBootMode> out_boot_mode);
    Result IsVrModeEnabled(Out<bool> out_is_vr_mode_enabled);
    Result SetVrModeEnabled(bool is_vr_mode_enabled);
    Result SetLcdBacklighOffEnabled(bool is_lcd_backlight_off_enabled);
    Result BeginVrModeEx();
    Result EndVrModeEx();
    Result IsInControllerFirmwareUpdateSection(
        Out<bool> out_is_in_controller_firmware_update_section);
    Result GetDefaultDisplayResolution(Out<s32> out_width, Out<s32> out_height);
    Result GetBuiltInDisplayType(Out<s32> out_display_type);
    Result PerformSystemButtonPressingIfInFocus(SystemButtonType type);
    Result EnableStartupLogoDisappearedMessage();
    Result GetOperationModeSystemInfo(Out<u32> out_operation_mode_system_info);
    Result GetAppletLaunchedHistory(Out<s32> out_count,
                                    OutArray<AppletId, BufferAttr_HipcMapAlias> out_applet_ids);
    Result GetSettingsPlatformRegion(Out<Set::PlatformRegion> out_settings_platform_region);
    Result SetRequestExitToLibraryAppletAtExecuteNextProgramEnabled();
    Result PushToGeneralChannel(SharedPointer<IStorage> storage); // cmd 20
    Result GetHomeButtonReaderLockAccessor(Out<SharedPointer<ILockAccessor>> out_lock_accessor);
    Result SetHandlingHomeButtonShortPressedEnabled(bool enabled);
    Result Unknown610(u64 unk);
    Result Unknown611(u8 unk);
    Result BeginVrMode3d();
    Result EndVrMode3d();
    Result IsVrModeEnabled3d(Out<bool> out_is_vr_mode_enabled_3d);
    Result GetVrLaboGoggleViewport(Out<s32> out_x, Out<s32> out_y, Out<s32> out_width, Out<s32> out_height);
    Result GetPanelPhysicalSizeForSpecificTitle(Out<f32> out_width, Out<f32> out_height);
    Result GetPanelResolutionForSpecificTitle(Out<s32> out_width, Out<s32> out_height);

    void SetCpuBoostMode(HLERequestContext& ctx);

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, D<&ICommonStateGetter::GetEventHandle>, "GetEventHandle"},
        FunctionInfo{1, D<&ICommonStateGetter::ReceiveMessage>, "ReceiveMessage"},
        FunctionInfo{2, nullptr, "GetThisAppletKind"},
        FunctionInfo{3, nullptr, "AllowToEnterSleep"},
        FunctionInfo{4, nullptr, "DisallowToEnterSleep"},
        FunctionInfo{5, D<&ICommonStateGetter::GetOperationMode>, "GetOperationMode"},
        FunctionInfo{6, D<&ICommonStateGetter::GetPerformanceMode>, "GetPerformanceMode"},
        FunctionInfo{7, nullptr, "GetCradleStatus"},
        FunctionInfo{8, D<&ICommonStateGetter::GetBootMode>, "GetBootMode"},
        FunctionInfo{9, D<&ICommonStateGetter::GetCurrentFocusState>, "GetCurrentFocusState"},
        FunctionInfo{10, D<&ICommonStateGetter::RequestToAcquireSleepLock>, "RequestToAcquireSleepLock"},
        FunctionInfo{11, D<&ICommonStateGetter::ReleaseSleepLock>, "ReleaseSleepLock"},
        FunctionInfo{12, D<&ICommonStateGetter::ReleaseSleepLockTransiently>, "ReleaseSleepLockTransiently"},
        FunctionInfo{13, D<&ICommonStateGetter::GetAcquiredSleepLockEvent>, "GetAcquiredSleepLockEvent"},
        FunctionInfo{14, nullptr, "GetWakeupCount"}, //11.0.0+
        FunctionInfo{15, nullptr, "Unknown15"}, //19.0.0+
        FunctionInfo{20, D<&ICommonStateGetter::PushToGeneralChannel>, "PushToGeneralChannel"},
        FunctionInfo{30, D<&ICommonStateGetter::GetHomeButtonReaderLockAccessor>, "GetHomeButtonReaderLockAccessor"},
        FunctionInfo{31, D<&ICommonStateGetter::GetReaderLockAccessorEx>, "GetReaderLockAccessorEx"}, //2.0.0+
        FunctionInfo{32, D<&ICommonStateGetter::GetWriterLockAccessorEx>, "GetWriterLockAccessorEx"}, //7.0.0+
        FunctionInfo{40, nullptr, "GetCradleFwVersion"}, //2.0.0+
        FunctionInfo{50, D<&ICommonStateGetter::IsVrModeEnabled>, "IsVrModeEnabled"}, //3.0.0+
        FunctionInfo{51, D<&ICommonStateGetter::SetVrModeEnabled>, "SetVrModeEnabled"}, //3.0.0+
        FunctionInfo{52, D<&ICommonStateGetter::SetLcdBacklighOffEnabled>, "SetLcdBacklighOffEnabled"}, //4.0.0+
        FunctionInfo{53, D<&ICommonStateGetter::BeginVrModeEx>, "BeginVrModeEx"}, //7.0.0+
        FunctionInfo{54, D<&ICommonStateGetter::EndVrModeEx>, "EndVrModeEx"}, //7.0.0+
        FunctionInfo{55, D<&ICommonStateGetter::IsInControllerFirmwareUpdateSection>, "IsInControllerFirmwareUpdateSection"}, //3.0.0+
        FunctionInfo{59, nullptr, "SetVrPositionForDebug"}, //1.0.0+
        FunctionInfo{60, D<&ICommonStateGetter::GetDefaultDisplayResolution>, "GetDefaultDisplayResolution"},
        FunctionInfo{61, D<&ICommonStateGetter::GetDefaultDisplayResolutionChangeEvent>, "GetDefaultDisplayResolutionChangeEvent"},
        FunctionInfo{62, D<&ICommonStateGetter::GetHdcpAuthenticationState>, "GetHdcpAuthenticationState"},
        FunctionInfo{63, D<&ICommonStateGetter::GetHdcpAuthenticationStateChangeEvent>, "GetHdcpAuthenticationStateChangeEvent"},
        FunctionInfo{64, nullptr, "SetTvPowerStateMatchingMode"},
        FunctionInfo{65, nullptr, "GetApplicationIdByContentActionName"},
        FunctionInfo{66, &ICommonStateGetter::SetCpuBoostMode, "SetCpuBoostMode"},
        FunctionInfo{67, nullptr, "CancelCpuBoostMode"},
        FunctionInfo{68, D<&ICommonStateGetter::GetBuiltInDisplayType>, "GetBuiltInDisplayType"},
        FunctionInfo{80, D<&ICommonStateGetter::PerformSystemButtonPressingIfInFocus>, "PerformSystemButtonPressingIfInFocus"},
        FunctionInfo{90, nullptr, "SetPerformanceConfigurationChangedNotification"},
        FunctionInfo{91, nullptr, "GetCurrentPerformanceConfiguration"},
        FunctionInfo{100, D<&ICommonStateGetter::SetHandlingHomeButtonShortPressedEnabled>, "SetHandlingHomeButtonShortPressedEnabled"},
        FunctionInfo{110, nullptr, "OpenMyGpuErrorHandler"},
        FunctionInfo{120, D<&ICommonStateGetter::GetAppletLaunchedHistory>, "GetAppletLaunchedHistory"}, //13.0.0+
        FunctionInfo{130, D<&ICommonStateGetter::EnableStartupLogoDisappearedMessage>, "EnableStartupLogoDisappearedMessage"}, //21.0.0+
        FunctionInfo{200, D<&ICommonStateGetter::GetOperationModeSystemInfo>, "GetOperationModeSystemInfo"},
        FunctionInfo{300, D<&ICommonStateGetter::GetSettingsPlatformRegion>, "GetSettingsPlatformRegion"},
        FunctionInfo{400, nullptr, "ActivateMigrationService"},
        FunctionInfo{401, nullptr, "DeactivateMigrationService"},
        FunctionInfo{500, nullptr, "DisableSleepTillShutdown"},
        FunctionInfo{501, nullptr, "SuppressDisablingSleepTemporarily"},
        FunctionInfo{502, nullptr, "IsSleepEnabled"},
        FunctionInfo{503, nullptr, "IsDisablingSleepSuppressed"},
        FunctionInfo{600, nullptr, "SetHidInputMagnificationForApplication"}, //20.0.0+
        FunctionInfo{610, D<&ICommonStateGetter::Unknown610>, "Unknown610"}, //21.0.0+
        FunctionInfo{611, D<&ICommonStateGetter::Unknown611>, "Unknown611"}, //22.0.0+
        FunctionInfo{900, D<&ICommonStateGetter::SetRequestExitToLibraryAppletAtExecuteNextProgramEnabled>, "SetRequestExitToLibraryAppletAtExecuteNextProgramEnabled"}, //11.0.0+
        FunctionInfo{910, nullptr, "GetLaunchRequiredTick"}, //17.0.0+
        FunctionInfo{1000, D<&ICommonStateGetter::BeginVrMode3d>, "BeginVrMode3d"}, //19.0.0+
        FunctionInfo{1001, D<&ICommonStateGetter::EndVrMode3d>, "EndVrMode3d"}, //19.0.0+
        FunctionInfo{1002, D<&ICommonStateGetter::IsVrModeEnabled3d>, "IsVrModeEnabled3d"}, //19.0.0+
        FunctionInfo{1003, D<&ICommonStateGetter::GetVrLaboGoggleViewport>, "GetVrLaboGoggleViewport"}, //21.0.0+
        FunctionInfo{1004, D<&ICommonStateGetter::GetPanelPhysicalSizeForSpecificTitle>, "GetPanelPhysicalSizeForSpecificTitle"}, //21.0.0+
        FunctionInfo{1005, D<&ICommonStateGetter::GetPanelResolutionForSpecificTitle>, "GetPanelResolutionForSpecificTitle"}, //21.0.0+
    );
    const std::shared_ptr<Applet> m_applet;
};

} // namespace Service::AM
