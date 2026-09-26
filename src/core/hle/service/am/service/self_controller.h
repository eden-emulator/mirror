// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/am/am_types.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Kernel {
class KReadableEvent;
}

namespace Service::Capture {
enum class AlbumImageOrientation;
enum class AlbumReportOption;
} // namespace Service::Capture

namespace Service::AM {

struct Applet;

class ISelfController final : public ServiceFramework<ISelfController> {
public:
    explicit ISelfController(Core::System& system_, std::shared_ptr<Applet> applet,
                             Kernel::KProcess* process);
    ~ISelfController() override;

private:
    Result Exit();
    Result LockExit();
    Result UnlockExit();
    Result EnterFatalSection();
    Result LeaveFatalSection();
    Result GetLibraryAppletLaunchableEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result SetScreenShotPermission(ScreenshotPermission screen_shot_permission);
    Result SetOperationModeChangedNotification(bool enabled);
    Result SetPerformanceModeChangedNotification(bool enabled);
    Result SetFocusHandlingMode(bool notify, bool background, bool suspend);
    Result SetRestartMessageEnabled(bool enabled);
    Result SetScreenShotAppletIdentityInfo(AppletIdentityInfo screen_shot_applet_identity_info);
    Result SetOutOfFocusSuspendingEnabled(bool enabled);
    Result SetAlbumImageOrientation(Capture::AlbumImageOrientation album_image_orientation);
    Result IsSystemBufferSharingEnabled();
    Result GetSystemSharedBufferHandle(Out<u64> out_buffer_id);
    Result GetSystemSharedLayerHandle(Out<u64> out_buffer_id, Out<u64> out_layer_id);
    Result CreateManagedDisplayLayer(Out<u64> out_layer_id);
    Result CreateManagedDisplaySeparableLayer(Out<u64> out_layer_id,
                                              Out<u64> out_recording_layer_id);
    Result SetHandlesRequestToDisplay(bool enable);
    Result ApproveToDisplay();
    Result SetMediaPlaybackState(bool state);
    Result OverrideAutoSleepTimeAndDimmingTime(s32 a, s32 b, s32 c, s32 d);
    Result SetIdleTimeDetectionExtension(IdleTimeDetectionExtension idle_time_detection_extension);
    Result GetIdleTimeDetectionExtension(
        Out<IdleTimeDetectionExtension> out_idle_time_detection_extension);
    Result ReportUserIsActive();
    Result SetAutoSleepDisabled(bool is_auto_sleep_disabled);
    Result IsAutoSleepDisabled(Out<bool> out_is_auto_sleep_disabled);
    Result IsIlluminanceAvailable(Out<bool> out_is_illuminance_available);
    Result SetInputDetectionPolicy(InputDetectionPolicy input_detection_policy);
    Result GetAccumulatedSuspendedTickValue(Out<u64> out_accumulated_suspended_tick_value);
    Result GetAccumulatedSuspendedTickChangedEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result SetAlbumImageTakenNotificationEnabled(bool enabled);
    Result SaveCurrentScreenshot(Capture::AlbumReportOption album_report_option);
    Result SetRecordVolumeMuted(bool muted);
    Result Unknown230(u32 in_val, Out<u16> out_val);
    Result Unknown240(u32 in_val);

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, D<&ISelfController::Exit>, "Exit"},
        FunctionInfo{1, D<&ISelfController::LockExit>, "LockExit"},
        FunctionInfo{2, D<&ISelfController::UnlockExit>, "UnlockExit"},
        FunctionInfo{3, D<&ISelfController::EnterFatalSection>, "EnterFatalSection"},
        FunctionInfo{4, D<&ISelfController::LeaveFatalSection>, "LeaveFatalSection"},
        FunctionInfo{9, D<&ISelfController::GetLibraryAppletLaunchableEvent>, "GetLibraryAppletLaunchableEvent"},
        FunctionInfo{10, D<&ISelfController::SetScreenShotPermission>, "SetScreenShotPermission"},
        FunctionInfo{11, D<&ISelfController::SetOperationModeChangedNotification>, "SetOperationModeChangedNotification"},
        FunctionInfo{12, D<&ISelfController::SetPerformanceModeChangedNotification>, "SetPerformanceModeChangedNotification"},
        FunctionInfo{13, D<&ISelfController::SetFocusHandlingMode>, "SetFocusHandlingMode"},
        FunctionInfo{14, D<&ISelfController::SetRestartMessageEnabled>, "SetRestartMessageEnabled"},
        FunctionInfo{15, D<&ISelfController::SetScreenShotAppletIdentityInfo>, "SetScreenShotAppletIdentityInfo"},
        FunctionInfo{16, D<&ISelfController::SetOutOfFocusSuspendingEnabled>, "SetOutOfFocusSuspendingEnabled"},
        FunctionInfo{17, nullptr, "SetControllerFirmwareUpdateSection"},
        FunctionInfo{18, nullptr, "SetRequiresCaptureButtonShortPressedMessage"},
        FunctionInfo{19, D<&ISelfController::SetAlbumImageOrientation>, "SetAlbumImageOrientation"},
        FunctionInfo{20, nullptr, "SetDesirableKeyboardLayout"},
        FunctionInfo{21, nullptr, "GetScreenShotProgramId"},
        FunctionInfo{40, D<&ISelfController::CreateManagedDisplayLayer>, "CreateManagedDisplayLayer"},
        FunctionInfo{41, D<&ISelfController::IsSystemBufferSharingEnabled>, "IsSystemBufferSharingEnabled"},
        FunctionInfo{42, D<&ISelfController::GetSystemSharedLayerHandle>, "GetSystemSharedLayerHandle"},
        FunctionInfo{43, D<&ISelfController::GetSystemSharedBufferHandle>, "GetSystemSharedBufferHandle"},
        FunctionInfo{44, D<&ISelfController::CreateManagedDisplaySeparableLayer>, "CreateManagedDisplaySeparableLayer"},
        FunctionInfo{45, nullptr, "SetManagedDisplayLayerSeparationMode"},
        FunctionInfo{46, nullptr, "SetRecordingLayerCompositionEnabled"},
        FunctionInfo{50, D<&ISelfController::SetHandlesRequestToDisplay>, "SetHandlesRequestToDisplay"},
        FunctionInfo{51, D<&ISelfController::ApproveToDisplay>, "ApproveToDisplay"},
        FunctionInfo{60, D<&ISelfController::OverrideAutoSleepTimeAndDimmingTime>, "OverrideAutoSleepTimeAndDimmingTime"},
        FunctionInfo{61, D<&ISelfController::SetMediaPlaybackState>, "SetMediaPlaybackState"},
        FunctionInfo{62, D<&ISelfController::SetIdleTimeDetectionExtension>, "SetIdleTimeDetectionExtension"},
        FunctionInfo{63, D<&ISelfController::GetIdleTimeDetectionExtension>, "GetIdleTimeDetectionExtension"},
        FunctionInfo{64, nullptr, "SetInputDetectionSourceSet"},
        FunctionInfo{65, D<&ISelfController::ReportUserIsActive>, "ReportUserIsActive"},
        FunctionInfo{66, nullptr, "GetCurrentIlluminance"},
        FunctionInfo{67, D<&ISelfController::IsIlluminanceAvailable>, "IsIlluminanceAvailable"},
        FunctionInfo{68, D<&ISelfController::SetAutoSleepDisabled>, "SetAutoSleepDisabled"},
        FunctionInfo{69, D<&ISelfController::IsAutoSleepDisabled>, "IsAutoSleepDisabled"},
        FunctionInfo{70, nullptr, "ReportMultimediaError"},
        FunctionInfo{71, nullptr, "GetCurrentIlluminanceEx"},
        FunctionInfo{72, D<&ISelfController::SetInputDetectionPolicy>, "SetInputDetectionPolicy"},
        FunctionInfo{80, nullptr, "SetWirelessPriorityMode"},
        FunctionInfo{90, D<&ISelfController::GetAccumulatedSuspendedTickValue>, "GetAccumulatedSuspendedTickValue"},
        FunctionInfo{91, D<&ISelfController::GetAccumulatedSuspendedTickChangedEvent>, "GetAccumulatedSuspendedTickChangedEvent"},
        FunctionInfo{100, D<&ISelfController::SetAlbumImageTakenNotificationEnabled>, "SetAlbumImageTakenNotificationEnabled"},
        FunctionInfo{110, nullptr, "SetApplicationAlbumUserData"},
        FunctionInfo{120, D<&ISelfController::SaveCurrentScreenshot>, "SaveCurrentScreenshot"},
        FunctionInfo{130, D<&ISelfController::SetRecordVolumeMuted>, "SetRecordVolumeMuted"},
        FunctionInfo{230, D<&ISelfController::Unknown230>, "Unknown230"},
        FunctionInfo{240, D<&ISelfController::Unknown240>, "Unknown240"},
        FunctionInfo{1000, nullptr, "GetDebugStorageChannel"}
    );
    Kernel::KProcess* const m_process;
    const std::shared_ptr<Applet> m_applet;
};

} // namespace Service::AM
