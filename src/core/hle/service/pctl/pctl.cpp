// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/common_funcs.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/patch_manager.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/pctl/pctl.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::PCTL {

constexpr Result ResultNoFreeCommunication{ErrorModule::PCTL, 101};
constexpr Result ResultStereoVisionRestricted{ErrorModule::PCTL, 104};
constexpr Result ResultNoCapability{ErrorModule::PCTL, 131};
constexpr Result ResultNoRestrictionEnabled{ErrorModule::PCTL, 181};

enum class Capability : u32 {
    None = 0,
    Application = 1 << 0,
    SnsPost = 1 << 1,
    Recovery = 1 << 6,
    Status = 1 << 8,
    StereoVision = 1 << 9,
    System = 1 << 15,
};
DECLARE_ENUM_FLAG_OPERATORS(Capability);

struct ApplicationInfo {
    u64 application_id{};
    std::array<u8, 32> age_rating{};
    u32 parental_control_flag{};
    Capability capability{};
};
static_assert(sizeof(ApplicationInfo) == 0x30, "ApplicationInfo has incorrect size.");

// This is nn::pctl::RestrictionSettings
struct RestrictionSettings {
    u8 rating_age;
    bool sns_post_restriction;
    bool free_communication_restriction;
};
static_assert(sizeof(RestrictionSettings) == 0x3, "RestrictionSettings has incorrect size.");

// This is nn::pctl::PlayTimerSettingsOld
struct PlayTimerSettingsOld {
    std::array<u32, 13> settings;
};
static_assert(sizeof(PlayTimerSettingsOld) == 0x34, "PlayTimerSettingsOld has incorrect size.");

// This is nn::pctl::PlayTimerSettings
struct PlayTimerSettings {
    std::array<u32, 17> settings; //21.0.0+ now takes 0x44
};
static_assert(sizeof(PlayTimerSettings) == 0x44, "PlayTimerSettings has incorrect size.");

enum class PlayTimerDisplayState : u8 {
    TimesUp = 0,
    BedtimeAlarm = 1,
    RemainingTime = 2,
    Unknown3 = 3,
    Unknown4 = 4,
    Unknown5 = 5,
    TimerDisabled = 6,
    NotConfigured = 7,
};

// This is nn::pctl::PlayTimerRemainingTimeDisplayInfo
struct PlayTimerRemainingTimeDisplayInfo {
    PlayTimerDisplayState state;
    INSERT_PADDING_BYTES(0x7);
    u64 unknown_08;
    u64 remaining_time;
};
static_assert(sizeof(PlayTimerRemainingTimeDisplayInfo) == 0x18, "PlayTimerRemainingTimeDisplayInfo has incorrect size.");

class IParentalControlService final : public ServiceFramework<IParentalControlService> {
public:
    explicit IParentalControlService(Core::System& system_, Capability capability_,
                                                    u64 program_id_)
        : ServiceFramework{system_, "IParentalControlService"}, capability{capability_},
        program_id{program_id_},
        service_context{system_, "IParentalControlService"}, synchronization_event{service_context},
        unlinked_event{service_context}, request_suspension_event{service_context} {
    }

    ~IParentalControlService() override = default;

    bool CheckFreeCommunicationPermissionImpl() const {
        if (states.temporary_unlocked) {
            return true;
        }
        if ((states.application_info.parental_control_flag & 1) == 0) {
            return true;
        }
        if (pin_code[0] == '\0') {
            return true;
        }
        if (!settings.is_free_communication_default_on) {
            return true;
        }
        // TODO(ogniK): Check for blacklisted/exempted applications. Return false can happen here
        // but as we don't have multiproceses support yet, we can just assume our application is
        // valid for the time being
        return true;
    }

    bool ConfirmStereoVisionPermissionImpl() const {
        if (states.temporary_unlocked) {
            return true;
        }
        if (pin_code[0] == '\0') {
            return true;
        }
        if (!settings.is_stero_vision_restricted) {
            return false;
        }
        return true;
    }

    void SetStereoVisionRestrictionImpl(bool is_restricted) {
        if (settings.disabled) {
            return;
        }

        if (pin_code[0] == '\0') {
            return;
        }
        settings.is_stero_vision_restricted = is_restricted;
    }

    Result Initialize() {
        LOG_DEBUG(Service_PCTL, "called");

        if (False(capability & (Capability::Application | Capability::System))) {
            LOG_ERROR(Service_PCTL, "Invalid capability! capability={:X}", capability);
            R_THROW(PCTL::ResultNoCapability);
        }

        // TODO(ogniK): Recovery flag initialization for pctl:r

        if (program_id != 0) {
            const FileSys::PatchManager pm{program_id, system.GetFileSystemController(),
                                        system.GetContentProvider()};
            const auto control = pm.GetControlMetadata();
            if (control.first) {
                states.tid_from_event = 0;
                states.launch_time_valid = false;
                states.is_suspended = false;
                states.free_communication = false;
                states.stereo_vision = false;
                states.application_info = ApplicationInfo{
                    .application_id = program_id,
                    .age_rating = control.first->GetRatingAge(),
                    .parental_control_flag = control.first->GetParentalControlFlag(),
                    .capability = capability,
                };

                if (False(capability & (Capability::System | Capability::Recovery))) {
                    // TODO(ogniK): Signal application launch event
                }
            }
        }

        R_SUCCEED();
    }

    Result CheckFreeCommunicationPermission() {
        LOG_DEBUG(Service_PCTL, "called");

        if (!CheckFreeCommunicationPermissionImpl()) {
            R_THROW(PCTL::ResultNoFreeCommunication);
        } else {
            states.free_communication = true;
            R_SUCCEED();
        }
    }

    Result ConfirmLaunchApplicationPermission(
        InBuffer<BufferAttr_HipcPointer> restriction_bitset, u64 nacp_flag, u64 application_id) {
        LOG_WARNING(Service_PCTL, "(STUBBED) called, nacp_flag={:#x} application_id={:016X}", nacp_flag,
                    application_id);
        R_SUCCEED();
    }

    Result ConfirmResumeApplicationPermission(
        InBuffer<BufferAttr_HipcPointer> restriction_bitset, u64 nacp_flag, u64 application_id) {
        LOG_WARNING(Service_PCTL, "(STUBBED) called, nacp_flag={:#x} application_id={:016X}", nacp_flag,
                    application_id);
        R_SUCCEED();
    }

    Result ConfirmSnsPostPermission() {
        LOG_WARNING(Service_PCTL, "(STUBBED) called");
        R_THROW(PCTL::ResultNoFreeCommunication);
    }

    Result ConfirmSystemSettingsPermission() {
        LOG_WARNING(Service_PCTL, "(STUBBED) called");
        R_SUCCEED();
    }

    Result IsRestrictionTemporaryUnlocked(
        Out<bool> out_is_temporary_unlocked) {
        *out_is_temporary_unlocked = false;
        LOG_WARNING(Service_PCTL, "(STUBBED) called, is_temporary_unlocked={}",
                    *out_is_temporary_unlocked);
        R_SUCCEED();
    }

    Result IsRestrictedSystemSettingsEntered(
        Out<bool> out_is_restricted_system_settings_entered) {
        *out_is_restricted_system_settings_entered = false;
        LOG_WARNING(Service_PCTL, "(STUBBED) called, is_temporary_unlocked={}",
                    *out_is_restricted_system_settings_entered);
        R_SUCCEED();
    }

    Result ConfirmStereoVisionPermission() {
        LOG_DEBUG(Service_PCTL, "called");
        states.stereo_vision = true;
        R_SUCCEED();
    }

    Result EndFreeCommunication() {
        LOG_WARNING(Service_PCTL, "(STUBBED) called");
        R_SUCCEED();
    }

    Result IsFreeCommunicationAvailable() {
        LOG_DEBUG(Service_PCTL, "(STUBBED) called");

        if (!CheckFreeCommunicationPermissionImpl()) {
            R_THROW(PCTL::ResultNoFreeCommunication);
        } else {
            R_SUCCEED();
        }
    }

    Result IsRestrictionEnabled(Out<bool> out_restriction_enabled) {
        LOG_DEBUG(Service_PCTL, "called");

        if (False(capability & (Capability::Status | Capability::Recovery))) {
            LOG_ERROR(Service_PCTL, "Application does not have Status or Recovery capabilities!");
            *out_restriction_enabled = false;
            R_THROW(PCTL::ResultNoCapability);
        }

        *out_restriction_enabled = pin_code[0] != '\0';
        R_SUCCEED();
    }

    Result GetSafetyLevel(Out<u32> out_safety_level) {
        *out_safety_level = 0;
        LOG_WARNING(Service_PCTL, "(STUBBED) called, safety_level={}", *out_safety_level);
        R_SUCCEED();
    }

    Result GetCurrentSettings(Out<RestrictionSettings> out_settings) {
        LOG_INFO(Service_PCTL, "called");
        *out_settings = restriction_settings;
        R_SUCCEED();
    }

    Result GetFreeCommunicationApplicationListCount(Out<s32> out_count) {
        *out_count = 4;
        LOG_WARNING(Service_PCTL, "(STUBBED) called, count={}", *out_count);
        R_SUCCEED();
    }

    Result ConfirmStereoVisionRestrictionConfigurable() {
        LOG_DEBUG(Service_PCTL, "called");

        if (False(capability & Capability::StereoVision)) {
            LOG_ERROR(Service_PCTL, "Application does not have StereoVision capability!");
            R_THROW(PCTL::ResultNoCapability);
        }

        if (pin_code[0] == '\0') {
            R_THROW(PCTL::ResultNoRestrictionEnabled);
        }

        R_SUCCEED();
    }

    Result IsStereoVisionPermitted(Out<bool> out_is_permitted) {
        LOG_DEBUG(Service_PCTL, "called");

        if (!ConfirmStereoVisionPermissionImpl()) {
            *out_is_permitted = false;
            R_THROW(PCTL::ResultStereoVisionRestricted);
        } else {
            *out_is_permitted = true;
            R_SUCCEED();
        }
    }

    Result GetPinCodeLength(Out<s32> out_length) {
        *out_length = 0;
        LOG_WARNING(Service_PCTL, "(STUBBED) called, length={}", *out_length);
        R_SUCCEED();
    }

    Result IsPairingActive(Out<bool> out_is_pairing_active) {
        *out_is_pairing_active = false;
        LOG_WARNING(Service_PCTL, "(STUBBED) called, is_pairing_active={}", *out_is_pairing_active);
        R_SUCCEED();
    }

    Result GetSynchronizationEvent(
        OutCopyHandle<Kernel::KReadableEvent> out_event) {
        LOG_INFO(Service_PCTL, "called");
        *out_event = synchronization_event.GetHandle();
        R_SUCCEED();
    }

    Result StartPlayTimer() {
        LOG_WARNING(Service_PCTL, "(STUBBED) called");
        R_SUCCEED();
    }

    Result StopPlayTimer() {
        LOG_WARNING(Service_PCTL, "(STUBBED) called");
        R_SUCCEED();
    }

    Result IsPlayTimerEnabled(Out<bool> out_is_play_timer_enabled) {
        *out_is_play_timer_enabled = false;
        LOG_WARNING(Service_PCTL, "(STUBBED) called, enabled={}", *out_is_play_timer_enabled);
        R_SUCCEED();
    }

    Result GetPlayTimerRemainingTime(Out<s32> out_remaining_time) {
        LOG_WARNING(Service_PCTL, "(STUBBED) called");
        *out_remaining_time = std::numeric_limits<s32>::max();
        R_SUCCEED();
    }

    Result IsRestrictedByPlayTimer(Out<bool> out_is_restricted_by_play_timer) {
        *out_is_restricted_by_play_timer = false;
        LOG_WARNING(Service_PCTL, "(STUBBED) called, restricted={}", *out_is_restricted_by_play_timer);
        R_SUCCEED();
    }

    Result GetPlayTimerSettingsOld(
        Out<PlayTimerSettingsOld> out_play_timer_settings) {
        LOG_WARNING(Service_PCTL, "(STUBBED) called");
        *out_play_timer_settings = {};
        R_SUCCEED();
    }

    Result GetPlayTimerSettings(Out<PlayTimerSettings> out_play_timer_settings) {
        LOG_WARNING(Service_PCTL, "(STUBBED) called");
        *out_play_timer_settings = raw_play_timer_settings;
        R_SUCCEED();
    }

    Result SetPlayTimerSettings(PlayTimerSettings play_timer_settings) {
        LOG_WARNING(Service_PCTL, "(STUBBED) called");
        raw_play_timer_settings = play_timer_settings;
        R_SUCCEED();
    }

    Result GetPlayTimerEventToRequestSuspension(
        OutCopyHandle<Kernel::KReadableEvent> out_event) {
        LOG_INFO(Service_PCTL, "called");
        *out_event = request_suspension_event.GetHandle();
        R_SUCCEED();
    }

    Result IsPlayTimerAlarmDisabled(Out<bool> out_play_timer_alarm_disabled) {
        *out_play_timer_alarm_disabled = false;
        LOG_INFO(Service_PCTL, "called, is_play_timer_alarm_disabled={}",
                *out_play_timer_alarm_disabled);
        R_SUCCEED();
    }

    Result GetPlayTimerRemainingTimeDisplayInfo(Out<PlayTimerRemainingTimeDisplayInfo> out_display_info) {
        LOG_DEBUG(Service_PCTL, "called");
        *out_display_info = {.state = PlayTimerDisplayState::NotConfigured};
        R_SUCCEED();
    }

    Result Unknown1460(u8 in_unk, Out<PlayTimerRemainingTimeDisplayInfo> out_display_info) {
        LOG_DEBUG(Service_PCTL, "called, in_unk={}", in_unk);
        *out_display_info = {.state = PlayTimerDisplayState::NotConfigured};
        R_SUCCEED();
    }

    Result GetUnlinkedEvent(OutCopyHandle<Kernel::KReadableEvent> out_event) {
        LOG_INFO(Service_PCTL, "called");
        *out_event = unlinked_event.GetHandle();
        R_SUCCEED();
    }

    Result GetStereoVisionRestriction(
        Out<bool> out_stereo_vision_restriction) {
        LOG_DEBUG(Service_PCTL, "called");

        if (False(capability & Capability::StereoVision)) {
            LOG_ERROR(Service_PCTL, "Application does not have StereoVision capability!");
            *out_stereo_vision_restriction = false;
            R_THROW(PCTL::ResultNoCapability);
        }

        *out_stereo_vision_restriction = settings.is_stero_vision_restricted;
        R_SUCCEED();
    }

    Result SetStereoVisionRestriction(bool stereo_vision_restriction) {
        LOG_DEBUG(Service_PCTL, "called, can_use={}", stereo_vision_restriction);

        if (False(capability & Capability::StereoVision)) {
            LOG_ERROR(Service_PCTL, "Application does not have StereoVision capability!");
            R_THROW(PCTL::ResultNoCapability);
        }

        SetStereoVisionRestrictionImpl(stereo_vision_restriction);
        R_SUCCEED();
    }

    Result ResetConfirmedStereoVisionPermission() {
        LOG_DEBUG(Service_PCTL, "called");

        states.stereo_vision = false;

        R_SUCCEED();
    }

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
    static const auto functions = CreateStaticMap(
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
        FunctionInfo{195101, D<&IParentalControlService::SetPlayTimerSettings>, "SetPlayTimerSettingsForDebug"} //18.0.0+
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

class IParentalControlServiceFactory : public ServiceFramework<IParentalControlServiceFactory> {
public:
    explicit IParentalControlServiceFactory(Core::System& system_, const char* name_, Capability capability_)
        : ServiceFramework{system_, name_}, capability{capability_}
    {}

    ~IParentalControlServiceFactory() override = default;

    Result CreateService(Out<SharedPointer<IParentalControlService>> out_service, ClientProcessId process_id) {
        LOG_DEBUG(Service_PCTL, "called, process_id={}", process_id.pid);
        *out_service = std::make_shared<IParentalControlService>(
            system, capability, system.ResolveCallerProgramId(*process_id));
        R_SUCCEED();
    }

    Result CreateServiceWithoutInitialize(Out<SharedPointer<IParentalControlService>> out_service, ClientProcessId process_id) {
        LOG_DEBUG(Service_PCTL, "called, process_id={}", process_id.pid);
        *out_service = std::make_shared<IParentalControlService>(
            system, capability, system.ResolveCallerProgramId(*process_id));
        R_SUCCEED();
    }

private:
    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static const auto functions = CreateStaticMap(
        FunctionInfo{0, D<&IParentalControlServiceFactory::CreateService>, "CreateService"},
        FunctionInfo{1, D<&IParentalControlServiceFactory::CreateServiceWithoutInitialize>, "CreateServiceWithoutInitialize"}
    );
    Capability capability{};
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("pctl", std::make_shared<IParentalControlServiceFactory>(system, "pctl", Capability::Application | Capability::SnsPost | Capability::Status | Capability::StereoVision));
    // TODO(ogniK): Implement remaining capabilities
    server_manager->RegisterNamedService("pctl:a", std::make_shared<IParentalControlServiceFactory>(system, "pctl:a", Capability::None));
    server_manager->RegisterNamedService("pctl:r", std::make_shared<IParentalControlServiceFactory>(system, "pctl:r", Capability::None));
    server_manager->RegisterNamedService("pctl:s", std::make_shared<IParentalControlServiceFactory>(system, "pctl:s", Capability::None));
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::PCTL
