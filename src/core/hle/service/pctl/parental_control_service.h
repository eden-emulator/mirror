// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator
// Project// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/os/event.h"
#include "core/hle/service/pctl/pctl_types.h"
#include "core/hle/service/service.h"

namespace Service::PCTL {

class IParentalControlService final : public ServiceFramework<IParentalControlService> {
public:
    explicit IParentalControlService(Core::System& system_, Capability capability_,
                                     u64 program_id_);
    ~IParentalControlService() override;

private:
    bool CheckFreeCommunicationPermissionImpl() const;
    bool ConfirmStereoVisionPermissionImpl() const;
    void SetStereoVisionRestrictionImpl(bool is_restricted);

    Result Initialize();
    Result CheckFreeCommunicationPermission();
    Result ConfirmLaunchApplicationPermission(InBuffer<BufferAttr_HipcPointer> restriction_bitset,
                                              u64 nacp_flag, u64 application_id);
    Result ConfirmResumeApplicationPermission(InBuffer<BufferAttr_HipcPointer> restriction_bitset,
                                              u64 nacp_flag, u64 application_id);
    Result ConfirmSnsPostPermission();
    Result ConfirmSystemSettingsPermission();
    Result IsRestrictionTemporaryUnlocked(Out<bool> out_is_temporary_unlocked);
    Result IsRestrictedSystemSettingsEntered(Out<bool> out_is_restricted_system_settings_entered);
    Result ConfirmStereoVisionPermission();
    Result EndFreeCommunication();
    Result IsFreeCommunicationAvailable();
    Result IsRestrictionEnabled(Out<bool> out_restriction_enabled);
    Result GetSafetyLevel(Out<u32> out_safety_level);
    Result GetCurrentSettings(Out<RestrictionSettings> out_settings);
    Result GetFreeCommunicationApplicationListCount(Out<s32> out_count);
    Result ConfirmStereoVisionRestrictionConfigurable();
    Result IsStereoVisionPermitted(Out<bool> out_is_permitted);
    Result GetPinCodeLength(Out<s32> out_length);
    Result IsPairingActive(Out<bool> out_is_pairing_active);
    Result GetSynchronizationEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result StartPlayTimer();
    Result StopPlayTimer();
    Result IsPlayTimerEnabled(Out<bool> out_is_play_timer_enabled);
    Result GetPlayTimerRemainingTime(Out<s32> out_remaining_time);
    Result IsRestrictedByPlayTimer(Out<bool> out_is_restricted_by_play_timer);
    Result GetPlayTimerSettingsOld(Out<PlayTimerSettingsOld> out_play_timer_settings);
    Result GetPlayTimerEventToRequestSuspension(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result IsPlayTimerAlarmDisabled(Out<bool> out_play_timer_alarm_disabled);
    Result GetPlayTimerRemainingTimeDisplayInfo(Out<PlayTimerRemainingTimeDisplayInfo> out_display_info);
    Result Unknown1460(u8 in_unk, Out<PlayTimerRemainingTimeDisplayInfo> out_display_info);
    Result GetUnlinkedEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result GetStereoVisionRestriction(Out<bool> out_stereo_vision_restriction);
    Result SetStereoVisionRestriction(bool stereo_vision_restriction);
    Result ResetConfirmedStereoVisionPermission();
    Result GetPlayTimerSettings(Out<PlayTimerSettings> out_play_timer_settings);
    Result SetPlayTimerSettings(PlayTimerSettings out_play_timer_settings);

    struct States {
        u64 current_tid{};
        ApplicationInfo application_info{};
        u64 tid_from_event{};
        bool launch_time_valid{};
        bool is_suspended{};
        bool temporary_unlocked{};
        bool free_communication{};
        bool stereo_vision{};
    };

    struct ParentalControlSettings {
        bool is_stero_vision_restricted{};
        bool is_free_communication_default_on{};
        bool disabled{};
    };

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{1, D<&IParentalControlService::Initialize>, "Initialize"},
        FunctionInfo{1001, D<&IParentalControlService::CheckFreeCommunicationPermission>, "CheckFreeCommunicationPermission"},
        FunctionInfo{1002, D<&IParentalControlService::ConfirmLaunchApplicationPermission>, "ConfirmLaunchApplicationPermission"},
        FunctionInfo{1003, D<&IParentalControlService::ConfirmResumeApplicationPermission>, "ConfirmResumeApplicationPermission"},
        FunctionInfo{1004, D<&IParentalControlService::ConfirmSnsPostPermission>, "ConfirmSnsPostPermission"},
        FunctionInfo{1005, D<&IParentalControlService::ConfirmSystemSettingsPermission>, "ConfirmSystemSettingsPermission"},
        FunctionInfo{1006, D<&IParentalControlService::IsRestrictionTemporaryUnlocked>, "IsRestrictionTemporaryUnlocked"},
        FunctionInfo{1007, nullptr, "RevertRestrictionTemporaryUnlocked"},
        FunctionInfo{1008, nullptr, "EnterRestrictedSystemSettings"},
        FunctionInfo{1009, nullptr, "LeaveRestrictedSystemSettings"},
        FunctionInfo{1010, D<&IParentalControlService::IsRestrictedSystemSettingsEntered>, "IsRestrictedSystemSettingsEntered"},
        FunctionInfo{1011, nullptr, "RevertRestrictedSystemSettingsEntered"},
        FunctionInfo{1012, nullptr, "GetRestrictedFeatures"},
        FunctionInfo{1013, D<&IParentalControlService::ConfirmStereoVisionPermission>, "ConfirmStereoVisionPermission"},
        FunctionInfo{1014, nullptr, "ConfirmPlayableApplicationVideoOld"},
        FunctionInfo{1015, nullptr, "ConfirmPlayableApplicationVideo"},
        FunctionInfo{1016, nullptr, "ConfirmShowNewsPermission"},
        FunctionInfo{1017, D<&IParentalControlService::EndFreeCommunication>, "EndFreeCommunication"},
        FunctionInfo{1018, D<&IParentalControlService::IsFreeCommunicationAvailable>, "IsFreeCommunicationAvailable"},
        FunctionInfo{1019, D<&IParentalControlService::ConfirmLaunchApplicationPermission>, "ConfirmLaunchApplicationPermission"},
        FunctionInfo{1020, nullptr, "ConfirmLaunchSharedApplicationPermission"}, //20.0.0+
        FunctionInfo{1021, nullptr, "TryBeginFreeCommunicationForStreamPlay"}, //21.0.0+
        FunctionInfo{1022, nullptr, "EndFreeCommunicationForStreamPlay"}, //21.0.0+
        FunctionInfo{1031, D<&IParentalControlService::IsRestrictionEnabled>, "IsRestrictionEnabled"},
        FunctionInfo{1032, D<&IParentalControlService::GetSafetyLevel>, "GetSafetyLevel"},
        FunctionInfo{1033, nullptr, "SetSafetyLevel"},
        FunctionInfo{1034, nullptr, "GetSafetyLevelSettings"},
        FunctionInfo{1035, D<&IParentalControlService::GetCurrentSettings>, "GetCurrentSettings"},
        FunctionInfo{1036, nullptr, "SetCustomSafetyLevelSettings"},
        FunctionInfo{1037, nullptr, "GetDefaultRatingOrganization"},
        FunctionInfo{1038, nullptr, "SetDefaultRatingOrganization"},
        FunctionInfo{1039, D<&IParentalControlService::GetFreeCommunicationApplicationListCount>, "GetFreeCommunicationApplicationListCount"},
        FunctionInfo{1042, nullptr, "AddToFreeCommunicationApplicationList"},
        FunctionInfo{1043, nullptr, "DeleteSettings"},
        FunctionInfo{1044, nullptr, "GetFreeCommunicationApplicationList"},
        FunctionInfo{1045, nullptr, "UpdateFreeCommunicationApplicationList"},
        FunctionInfo{1046, nullptr, "DisableFeaturesForReset"},
        FunctionInfo{1047, nullptr, "NotifyApplicationDownloadStarted"},
        FunctionInfo{1048, nullptr, "NotifyNetworkProfileCreated"},
        FunctionInfo{1049, nullptr, "ResetFreeCommunicationApplicationList"},
        FunctionInfo{1050, nullptr, "AddToFreeCommunicationApplicationList"}, //20.0.0+
        FunctionInfo{1051, nullptr, "NotifyApplicationDownloadStarted"}, //20.0.0+
        FunctionInfo{1061, D<&IParentalControlService::ConfirmStereoVisionRestrictionConfigurable>, "ConfirmStereoVisionRestrictionConfigurable"},
        FunctionInfo{1062, D<&IParentalControlService::GetStereoVisionRestriction>, "GetStereoVisionRestriction"},
        FunctionInfo{1063, D<&IParentalControlService::SetStereoVisionRestriction>, "SetStereoVisionRestriction"},
        FunctionInfo{1064, D<&IParentalControlService::ResetConfirmedStereoVisionPermission>, "ResetConfirmedStereoVisionPermission"},
        FunctionInfo{1065, D<&IParentalControlService::IsStereoVisionPermitted>, "IsStereoVisionPermitted"},
        FunctionInfo{1201, nullptr, "UnlockRestrictionTemporarily"},
        FunctionInfo{1202, nullptr, "UnlockSystemSettingsRestriction"},
        FunctionInfo{1203, nullptr, "SetPinCode"},
        FunctionInfo{1204, nullptr, "GenerateInquiryCode"},
        FunctionInfo{1205, nullptr, "CheckMasterKey"},
        FunctionInfo{1206, D<&IParentalControlService::GetPinCodeLength>, "GetPinCodeLength"},
        FunctionInfo{1207, nullptr, "GetPinCodeChangedEvent"},
        FunctionInfo{1208, nullptr, "GetPinCode"},
        FunctionInfo{1403, D<&IParentalControlService::IsPairingActive>, "IsPairingActive"},
        FunctionInfo{1406, nullptr, "GetSettingsLastUpdated"},
        FunctionInfo{1411, nullptr, "GetPairingAccountInfo"},
        FunctionInfo{1421, nullptr, "GetAccountNickname"},
        FunctionInfo{1424, nullptr, "GetAccountState"},
        FunctionInfo{1425, nullptr, "RequestPostEvents"},
        FunctionInfo{1426, nullptr, "GetPostEventInterval"},
        FunctionInfo{1427, nullptr, "SetPostEventInterval"},
        FunctionInfo{1432, D<&IParentalControlService::GetSynchronizationEvent>, "GetSynchronizationEvent"},
        FunctionInfo{1451, D<&IParentalControlService::StartPlayTimer>, "StartPlayTimer"},
        FunctionInfo{1452, D<&IParentalControlService::StopPlayTimer>, "StopPlayTimer"},
        FunctionInfo{1453, D<&IParentalControlService::IsPlayTimerEnabled>, "IsPlayTimerEnabled"},
        FunctionInfo{1454, D<&IParentalControlService::GetPlayTimerRemainingTime>, "GetPlayTimerRemainingTime"},
        FunctionInfo{1455, D<&IParentalControlService::IsRestrictedByPlayTimer>, "IsRestrictedByPlayTimer"},
        FunctionInfo{1456, D<&IParentalControlService::GetPlayTimerSettingsOld>, "GetPlayTimerSettingsOld"},
        FunctionInfo{1457, D<&IParentalControlService::GetPlayTimerEventToRequestSuspension>, "GetPlayTimerEventToRequestSuspension"},
        FunctionInfo{1458, D<&IParentalControlService::IsPlayTimerAlarmDisabled>, "IsPlayTimerAlarmDisabled"},
        FunctionInfo{1459, D<&IParentalControlService::GetPlayTimerRemainingTimeDisplayInfo>, "GetPlayTimerRemainingTimeDisplayInfo"},
        FunctionInfo{1460, D<&IParentalControlService::Unknown1460>, "Unknown1460"},
        FunctionInfo{1471, nullptr, "NotifyWrongPinCodeInputManyTimes"},
        FunctionInfo{1472, nullptr, "CancelNetworkRequest"},
        FunctionInfo{1473, D<&IParentalControlService::GetUnlinkedEvent>, "GetUnlinkedEvent"},
        FunctionInfo{1474, nullptr, "ClearUnlinkedEvent"},
        FunctionInfo{1475, nullptr, "GetExtendedPlayTimerEvent"}, // 18.0.0+
        FunctionInfo{1601, nullptr, "DisableAllFeatures"},
        FunctionInfo{1602, nullptr, "PostEnableAllFeatures"},
        FunctionInfo{1603, nullptr, "IsAllFeaturesDisabled"},
        FunctionInfo{1901, nullptr, "DeleteFromFreeCommunicationApplicationListForDebug"},
        FunctionInfo{1902, nullptr, "ClearFreeCommunicationApplicationListForDebug"},
        FunctionInfo{1903, nullptr, "GetExemptApplicationListCountForDebug"},
        FunctionInfo{1904, nullptr, "GetExemptApplicationListForDebug"},
        FunctionInfo{1905, nullptr, "UpdateExemptApplicationListForDebug"},
        FunctionInfo{1906, nullptr, "AddToExemptApplicationListForDebug"},
        FunctionInfo{1907, nullptr, "DeleteFromExemptApplicationListForDebug"},
        FunctionInfo{1908, nullptr, "ClearExemptApplicationListForDebug"},
        FunctionInfo{1941, nullptr, "DeletePairing"},
        FunctionInfo{1951, nullptr, "SetPlayTimerSettingsForDebug"},
        FunctionInfo{1952, nullptr, "GetPlayTimerSpentTimeForTest"},
        FunctionInfo{1953, nullptr, "SetPlayTimerAlarmDisabledForDebug"},
        FunctionInfo{1954, nullptr, "IsBedtimeAlarmEnabled"}, // 18.0.0+
        FunctionInfo{1955, nullptr, "GetBedtimeAlarmTime"}, // 18.0.0+
        FunctionInfo{1956, nullptr, "GetBedtimeAlarmTimeHour"}, // 18.0.0+
        FunctionInfo{1957, nullptr, "GetBedtimeAlarmTimeMinute"}, // 18.0.0+
        FunctionInfo{2001, nullptr, "RequestPairingAsync"},
        FunctionInfo{2002, nullptr, "FinishRequestPairing"},
        FunctionInfo{2003, nullptr, "AuthorizePairingAsync"},
        FunctionInfo{2004, nullptr, "FinishAuthorizePairing"},
        FunctionInfo{2005, nullptr, "RetrievePairingInfoAsync"},
        FunctionInfo{2006, nullptr, "FinishRetrievePairingInfo"},
        FunctionInfo{2007, nullptr, "UnlinkPairingAsync"},
        FunctionInfo{2008, nullptr, "FinishUnlinkPairing"},
        FunctionInfo{2009, nullptr, "GetAccountMiiImageAsync"},
        FunctionInfo{2010, nullptr, "FinishGetAccountMiiImage"},
        FunctionInfo{2011, nullptr, "GetAccountMiiImageContentTypeAsync"},
        FunctionInfo{2012, nullptr, "FinishGetAccountMiiImageContentType"},
        FunctionInfo{2013, nullptr, "SynchronizeParentalControlSettingsAsync"},
        FunctionInfo{2014, nullptr, "FinishSynchronizeParentalControlSettings"},
        FunctionInfo{2015, nullptr, "FinishSynchronizeParentalControlSettingsWithLastUpdated"},
        FunctionInfo{2016, nullptr, "RequestUpdateExemptionListAsync"}, //5.0.0+
        FunctionInfo{145601, D<&IParentalControlService::GetPlayTimerSettings>, "GetPlayTimerSettings"}, // 18.0.0+
        FunctionInfo{2017, nullptr, "AuthorizePairingAsync"}, //19.0.0+
        FunctionInfo{2019, nullptr, "RequestUpdateDeviceUsersBackground"}, //19.0.0+
        FunctionInfo{2021, nullptr, "RequestCopyPairingAsync"}, //20.0.0+
        FunctionInfo{2022, nullptr, "FinishRequestCopyPairing"}, //20.0.0+
        FunctionInfo{2023, nullptr, "IsFromPairingActiveDevice"}, //20.0.0+
        FunctionInfo{2024, nullptr, "RollbackCopyPairing"}, //21.0.0+
        FunctionInfo{3001, nullptr, "GetErrorContextChangedEvent"}, //20.0.0+
        FunctionInfo{145601, D<&IParentalControlService::GetPlayTimerSettings>, "GetPlayTimerSettings"}, // 18.0.0+
        FunctionInfo{195101, D<&IParentalControlService::SetPlayTimerSettings>, "SetPlayTimerSettingsForDebug"}, //18.0.0+
    );
    States states{};
    ParentalControlSettings settings{};
    RestrictionSettings restriction_settings{};
    std::array<char, 8> pin_code{};
    Capability capability{};
    u64 program_id{};
    // TODO: this is raw
    PlayTimerSettings raw_play_timer_settings{};

    KernelHelpers::ServiceContext service_context;
    Event synchronization_event;
    Event unlinked_event;
    Event request_suspension_event;
};

} // namespace Service::PCTL
