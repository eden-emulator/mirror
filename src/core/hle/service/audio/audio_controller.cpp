// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "audio_core/audio_core.h"
#include "core/hle/service/audio/audio_controller.h"
#include "core/hle/service/audio/audio_out_manager.h"
#include "core/core.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/set/system_settings_server.h"
#include "core/hle/service/sm/sm.h"
#include "common/settings.h"
#include <algorithm>

namespace Service::Audio {

IAudioController::IAudioController(Core::System& system_)
    : ServiceFramework{system_, "audctl"}, service_context{system, "audctl"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        FunctionInfo{0, D<&IAudioController::GetTargetVolume>, "GetTargetVolume"},
        FunctionInfo{1, D<&IAudioController::SetTargetVolume>, "SetTargetVolume"},
        FunctionInfo{2, D<&IAudioController::GetTargetVolumeMin>, "GetTargetVolumeMin"},
        FunctionInfo{3, D<&IAudioController::GetTargetVolumeMax>, "GetTargetVolumeMax"},
        FunctionInfo{4, D<&IAudioController::IsTargetMute>, "IsTargetMute"},
        FunctionInfo{5, D<&IAudioController::SetTargetMute>, "SetTargetMute"},
        FunctionInfo{6, nullptr, "IsTargetConnected"}, //20.0.0+
        FunctionInfo{7, nullptr, "SetDefaultTarget"},
        FunctionInfo{8, nullptr, "GetDefaultTarget"},
        FunctionInfo{9, D<&IAudioController::GetAudioOutputMode>, "GetAudioOutputMode"},
        FunctionInfo{10, D<&IAudioController::SetAudioOutputMode>, "SetAudioOutputMode"},
        FunctionInfo{11, nullptr, "SetForceMutePolicy"},
        FunctionInfo{12, D<&IAudioController::GetForceMutePolicy>, "GetForceMutePolicy"},
        FunctionInfo{13, D<&IAudioController::GetOutputModeSetting>, "GetOutputModeSetting"},
        FunctionInfo{14, D<&IAudioController::SetOutputModeSetting>, "SetOutputModeSetting"},
        FunctionInfo{15, nullptr, "SetOutputTarget"},
        FunctionInfo{16, nullptr, "SetInputTargetForceEnabled"},
        FunctionInfo{17, D<&IAudioController::SetHeadphoneOutputLevelMode>, "SetHeadphoneOutputLevelMode"},
        FunctionInfo{18, D<&IAudioController::GetHeadphoneOutputLevelMode>, "GetHeadphoneOutputLevelMode"},
        FunctionInfo{19, nullptr, "AcquireAudioVolumeUpdateEventForPlayReport"},
        FunctionInfo{20, nullptr, "AcquireAudioOutputDeviceUpdateEventForPlayReport"},
        FunctionInfo{21, nullptr, "GetAudioOutputTargetForPlayReport"},
        FunctionInfo{22, D<&IAudioController::NotifyHeadphoneVolumeWarningDisplayedEvent>, "NotifyHeadphoneVolumeWarningDisplayedEvent"},
        FunctionInfo{23, nullptr, "SetSystemOutputMasterVolume"},
        FunctionInfo{24, nullptr, "GetSystemOutputMasterVolume"},
        FunctionInfo{25, nullptr, "GetAudioVolumeDataForPlayReport"},
        FunctionInfo{26, nullptr, "UpdateHeadphoneSettings"},
        FunctionInfo{27, nullptr, "SetVolumeMappingTableForDev"},
        FunctionInfo{28, nullptr, "GetAudioOutputChannelCountForPlayReport"},
        FunctionInfo{29, nullptr, "BindAudioOutputChannelCountUpdateEventForPlayReport"},
        FunctionInfo{30, D<&IAudioController::SetSpeakerAutoMuteEnabled>, "SetSpeakerAutoMuteEnabled"},
        FunctionInfo{31, D<&IAudioController::IsSpeakerAutoMuteEnabled>, "IsSpeakerAutoMuteEnabled"},
        FunctionInfo{32, D<&IAudioController::GetActiveOutputTarget>, "GetActiveOutputTarget"},
        FunctionInfo{33, nullptr, "GetTargetDeviceInfo"},
        FunctionInfo{34, D<&IAudioController::AcquireTargetNotification>, "AcquireTargetNotification"},
        FunctionInfo{35, nullptr, "SetHearingProtectionSafeguardTimerRemainingTimeForDebug"},
        FunctionInfo{36, nullptr, "GetHearingProtectionSafeguardTimerRemainingTimeForDebug"},
        FunctionInfo{37, nullptr, "SetHearingProtectionSafeguardEnabled"},
        FunctionInfo{38, nullptr, "IsHearingProtectionSafeguardEnabled"},
        FunctionInfo{39, nullptr, "IsHearingProtectionSafeguardMonitoringOutputForDebug"},
        FunctionInfo{40, nullptr, "GetSystemInformationForDebug"},
        FunctionInfo{41, nullptr, "SetVolumeButtonLongPressTime"},
        FunctionInfo{42, nullptr, "SetNativeVolumeForDebug"},
        FunctionInfo{43, nullptr, "Unknown43"}, //21.0.0+
        FunctionInfo{5000, D<&IAudioController::Unknown5000>, "Unknown5000"}, //19.0.0+
        FunctionInfo{10000, nullptr, "NotifyAudioOutputTargetForPlayReport"},
        FunctionInfo{10001, nullptr, "NotifyAudioOutputChannelCountForPlayReport"},
        FunctionInfo{10002, nullptr, "NotifyUnsupportedUsbOutputDeviceAttachedForPlayReport"},
        FunctionInfo{10100, nullptr, "GetAudioVolumeDataForPlayReport"},
        FunctionInfo{10101, nullptr, "BindAudioVolumeUpdateEventForPlayReport"},
        FunctionInfo{10102, nullptr, "BindAudioOutputTargetUpdateEventForPlayReport"},
        FunctionInfo{10103, nullptr, "GetAudioOutputTargetForPlayReport"},
        FunctionInfo{10104, nullptr, "GetAudioOutputChannelCountForPlayReport"},
        FunctionInfo{10105, nullptr, "BindAudioOutputChannelCountUpdateEventForPlayReport"}, //14.0.0-19.0.1
        FunctionInfo{10106, nullptr, "GetDefaultAudioOutputTargetForPlayReport"}, //14.0.0-19.0.1
        FunctionInfo{10200, nullptr, "Unknown10200"}, //20.0.0+
        FunctionInfo{50000, nullptr, "SetAnalogInputBoostGainForPrototyping"}, //15.0.0-18.1.0
        FunctionInfo{50001, nullptr, "OverrideDefaultTargetForDebug"}, //19.0.0-19.0.1
        FunctionInfo{50003, nullptr, "SetForceOverrideExternalDeviceNameForDebug"}, //19.0.0+
        FunctionInfo{50004, nullptr, "ClearForceOverrideExternalDeviceNameForDebug"} //19.0.0+
    };
    // clang-format on

    RegisterHandlers(functions);

    m_set_sys =
        system.ServiceManager().GetService<Service::Set::ISystemSettingsServer>("set:sys", true);
    notification_event = service_context.CreateEvent("IAudioController:NotificationEvent");

    // Probably shouldn't do this in constructor?
    try {
        const int ui_volume = Settings::values.volume.GetValue();
        const int mapped = static_cast<int>(std::lround((static_cast<double>(ui_volume) / 100.0) * 15.0));
        const auto active_idx = static_cast<size_t>(m_active_target);
        if (active_idx < m_target_volumes.size()) {
            m_target_volumes[active_idx] = std::clamp(mapped, 0, 15);
        }

        const bool muted = Settings::values.audio_muted.GetValue();
        for (auto& m : m_target_muted) {
            m = muted;
        }

        if (!muted && active_idx < m_target_volumes.size()) {
            const float vol_f = static_cast<float>(m_target_volumes[active_idx]) / 15.0f;
            try {
                auto& sink = system.AudioCore().GetOutputSink();
                sink.SetSystemVolume(vol_f);
                sink.SetDeviceVolume(vol_f);
            } catch (...) {
                LOG_WARNING(Audio, "Failed to apply initial sink volume from settings");
            }

            if (auto audout_mgr = system.ServiceManager().GetService<Service::Audio::IAudioOutManager>("audout:u")) {
                audout_mgr->SetAllAudioOutVolume(static_cast<float>(m_target_volumes[active_idx]) / 15.0f);
            }
        }
    } catch (...) {
        // Catch if something fails, since this is constructor
    }
}

IAudioController::~IAudioController() {
    service_context.CloseEvent(notification_event);
};

Result IAudioController::GetTargetVolumeMin(Out<s32> out_target_min_volume) {
    LOG_DEBUG(Audio, "called.");

    // This service function is currently hardcoded on the
    // actual console to this value (as of 8.0.0).
    *out_target_min_volume = 0;
    R_SUCCEED();
}

Result IAudioController::GetTargetVolumeMax(Out<s32> out_target_max_volume) {
    LOG_DEBUG(Audio, "called.");

    // This service function is currently hardcoded on the
    // actual console to this value (as of 8.0.0).
    *out_target_max_volume = 15;
    R_SUCCEED();
}

Result IAudioController::GetAudioOutputMode(Out<Set::AudioOutputMode> out_output_mode,
                                            Set::AudioOutputModeTarget target) {
    const auto result = m_set_sys->GetAudioOutputMode(out_output_mode, target);

    LOG_INFO(Service_SET, "called, target={}, output_mode={}", target, *out_output_mode);
    R_RETURN(result);
}

Result IAudioController::SetAudioOutputMode(Set::AudioOutputModeTarget target,
                                            Set::AudioOutputMode output_mode) {
    LOG_INFO(Service_SET, "called, target={}, output_mode={}", target, output_mode);

    R_RETURN(m_set_sys->SetAudioOutputMode(target, output_mode));
}

Result IAudioController::GetForceMutePolicy(Out<ForceMutePolicy> out_mute_policy) {
    LOG_WARNING(Audio, "(STUBBED) called");

    // Removed on FW 13.2.1+
    *out_mute_policy = ForceMutePolicy::Disable;
    R_SUCCEED();
}

Result IAudioController::GetOutputModeSetting(Out<Set::AudioOutputMode> out_output_mode,
                                              Set::AudioOutputModeTarget target) {
    LOG_WARNING(Audio, "(STUBBED) called, target={}", target);

    *out_output_mode = Set::AudioOutputMode::ch_7_1;
    R_SUCCEED();
}

Result IAudioController::SetOutputModeSetting(Set::AudioOutputModeTarget target,
                                              Set::AudioOutputMode output_mode) {
    LOG_INFO(Service_SET, "called, target={}, output_mode={}", target, output_mode);
    R_SUCCEED();
}

Result IAudioController::SetHeadphoneOutputLevelMode(HeadphoneOutputLevelMode output_level_mode) {
    LOG_WARNING(Audio, "(STUBBED) called, output_level_mode={}", output_level_mode);
    R_SUCCEED();
}

Result IAudioController::GetHeadphoneOutputLevelMode(
    Out<HeadphoneOutputLevelMode> out_output_level_mode) {
    LOG_INFO(Audio, "called");

    *out_output_level_mode = HeadphoneOutputLevelMode::Normal;
    R_SUCCEED();
}

Result IAudioController::NotifyHeadphoneVolumeWarningDisplayedEvent() {
    LOG_WARNING(Service_Audio, "(STUBBED) called");
    R_SUCCEED();
}

Result IAudioController::SetSpeakerAutoMuteEnabled(bool is_speaker_auto_mute_enabled) {
    LOG_INFO(Audio, "called, is_speaker_auto_mute_enabled={}", is_speaker_auto_mute_enabled);

    R_RETURN(m_set_sys->SetSpeakerAutoMuteFlag(is_speaker_auto_mute_enabled));
}

Result IAudioController::IsSpeakerAutoMuteEnabled(Out<bool> out_is_speaker_auto_mute_enabled) {
    const auto result = m_set_sys->GetSpeakerAutoMuteFlag(out_is_speaker_auto_mute_enabled);

    LOG_INFO(Audio, "called, is_speaker_auto_mute_enabled={}", *out_is_speaker_auto_mute_enabled);
    R_RETURN(result);
}

Result IAudioController::AcquireTargetNotification(
    OutCopyHandle<Kernel::KReadableEvent> out_notification_event) {
    LOG_WARNING(Service_AM, "(STUBBED) called");

    *out_notification_event = &notification_event->GetReadableEvent();
    R_SUCCEED();
}
Result IAudioController::Unknown5000(Out<SharedPointer<IAudioController>> out_audio_controller) {
    LOG_DEBUG(Audio, "Creating duplicate audio controller interface");

    // Return a new reference to this controller instance
    *out_audio_controller = std::static_pointer_cast<IAudioController>(shared_from_this());

    R_SUCCEED();
}

Result IAudioController::GetTargetVolume(Out<s32> out_target_volume, Set::AudioOutputModeTarget target) {
    LOG_DEBUG(Audio, "GetTargetVolume called, target={}", target);

    const auto idx = static_cast<size_t>(target);
    if (idx >= m_target_volumes.size()) {
        LOG_ERROR(Audio, "GetTargetVolume invalid target {}", idx);
        R_THROW(ResultInvalidArgument);
    }

    *out_target_volume = m_target_volumes[idx];
    R_SUCCEED();
}

Result IAudioController::SetTargetVolume(Set::AudioOutputModeTarget target, s32 target_volume) {
    LOG_INFO(Audio, "SetTargetVolume called, target={}, volume={}", target, target_volume);

    const auto idx = static_cast<size_t>(target);
    if (idx >= m_target_volumes.size()) {
        LOG_ERROR(Audio, "SetTargetVolume invalid target {}", idx);
        R_THROW(ResultInvalidArgument);
    }

    if (target_volume < 0) {
        target_volume = 0;
    } else if (target_volume > 15) {
        target_volume = 15;
    }

    m_target_volumes[idx] = target_volume;

    if (m_active_target == target && !m_target_muted[idx]) {
        const float vol = static_cast<float>(target_volume) / 15.0f;
        // try catch this, as we don't know how it handles it when no output is set.
        // we already have audio issues when no output is set, so catch.
        try {
            auto& sink = system.AudioCore().GetOutputSink();
            sink.SetSystemVolume(vol);
            sink.SetDeviceVolume(vol);
        } catch (...) {
            LOG_WARNING(Audio, "Failed to set sink system volume");
        }

        if (auto audout_mgr = system.ServiceManager().GetService<IAudioOutManager>("audout:u")) {
            audout_mgr->SetAllAudioOutVolume(vol);
        }
    }

    if (m_active_target == target) {
        const int ui_volume = static_cast<int>(std::lround((static_cast<double>(target_volume) / 15.0) * 100.0));
        Settings::values.volume.SetValue(static_cast<u8>(std::clamp(ui_volume, 0, 100)));
    }

    R_SUCCEED();
}

Result IAudioController::IsTargetMute(Out<bool> out_is_target_muted, Set::AudioOutputModeTarget target) {
    LOG_DEBUG(Audio, "called, target={}", target);

    const auto idx = static_cast<size_t>(target);
    if (idx >= m_target_muted.size()) {
        LOG_ERROR(Audio, "invalid target {}", idx);
        R_THROW(ResultInvalidArgument);
    }

    *out_is_target_muted = m_target_muted[idx];
    R_SUCCEED();
}

Result IAudioController::SetTargetMute(bool is_muted, Set::AudioOutputModeTarget target) {
    LOG_INFO(Audio, "called, target={}, muted={}", target, is_muted);

    const auto idx = static_cast<size_t>(target);
    if (idx >= m_target_muted.size()) {
        LOG_ERROR(Audio, "invalid target {}", idx);
        R_THROW(ResultInvalidArgument);
    }

    m_target_muted[idx] = is_muted;

    if (m_active_target == target) {
        try {
            auto& sink = system.AudioCore().GetOutputSink();
            if (is_muted) {
                sink.SetSystemVolume(0.0f);
                sink.SetDeviceVolume(0.0f);
            } else {
                const float vol = static_cast<float>(m_target_volumes[idx]) / 15.0f;
                sink.SetSystemVolume(vol);
                sink.SetDeviceVolume(vol);
            }
        } catch (...) {
            LOG_WARNING(Audio, "Failed to set sink system volume on mute change");
        }

        // Also update any open audout sessions via the audout:u service.
        if (auto audout_mgr = system.ServiceManager().GetService<IAudioOutManager>("audout:u")) {
            if (is_muted) {
                audout_mgr->SetAllAudioOutVolume(0.0f);
            } else {
                const float vol = static_cast<float>(m_target_volumes[idx]) / 15.0f;
                audout_mgr->SetAllAudioOutVolume(vol);
            }
        }
    }

    Settings::values.audio_muted.SetValue(is_muted);

    R_SUCCEED();
}

Result IAudioController::GetActiveOutputTarget(Out<Set::AudioOutputModeTarget> out_active_target) {
    LOG_DEBUG(Audio, "GetActiveOutputTarget called");
    *out_active_target = m_active_target;
    R_SUCCEED();
}
} // namespace Service::Audio
