// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/file_sys/control_metadata.h"
#include "core/file_sys/nca_metadata.h"
#include "core/file_sys/registered_cache.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/ns/application_manager_interface.h"

#include "core/file_sys/content_archive.h"
#include "core/hle/service/ns/content_management_interface.h"
#include "core/hle/service/ns/read_only_application_control_data_interface.h"
#include "core/file_sys/patch_manager.h"
#include "frontend_common/firmware_manager.h"
#include "core/launch_timestamp_cache.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace Service::NS {

IApplicationManagerInterface::IApplicationManagerInterface(Core::System& system_)
    : ServiceFramework{system_, "IApplicationManagerInterface"},
      service_context{system, "IApplicationManagerInterface"},
      record_update_system_event{service_context}, sd_card_mount_status_event{service_context},
      gamecard_update_detection_event{service_context},
      gamecard_mount_status_event{service_context}, gamecard_mount_failure_event{service_context},
      gamecard_waken_ready_event{service_context}, unknown_event{service_context} {
    // clang-format off
    static const FunctionInfo functions[] = {
        FunctionInfo{0, D<&IApplicationManagerInterface::ListApplicationRecord>, "ListApplicationRecord"},
        FunctionInfo{1, nullptr, "GenerateApplicationRecordCount"},
        FunctionInfo{2, D<&IApplicationManagerInterface::GetApplicationRecordUpdateSystemEvent>, "GetApplicationRecordUpdateSystemEvent"},
        FunctionInfo{3, nullptr, "GetApplicationViewDeprecated"},
        FunctionInfo{4, D<&IApplicationManagerInterface::DeleteApplicationEntity>, "DeleteApplicationEntity"},
        FunctionInfo{5, D<&IApplicationManagerInterface::DeleteApplicationCompletely>, "DeleteApplicationCompletely"},
        FunctionInfo{6, nullptr, "IsAnyApplicationEntityRedundant"},
        FunctionInfo{7, nullptr, "DeleteRedundantApplicationEntity"},
        FunctionInfo{8, nullptr, "IsApplicationEntityMovable"},
        FunctionInfo{9, nullptr, "MoveApplicationEntity"},
        FunctionInfo{11, nullptr, "CalculateApplicationOccupiedSize"},
        FunctionInfo{16, &IApplicationManagerInterface::PushApplicationRecord, "PushApplicationRecord"},
        FunctionInfo{17, nullptr, "ListApplicationRecordContentMeta"},
        FunctionInfo{19, nullptr, "LaunchApplicationOld"},
        FunctionInfo{21, nullptr, "GetApplicationContentPath"},
        FunctionInfo{22, nullptr, "TerminateApplication"},
        FunctionInfo{23, nullptr, "ResolveApplicationContentPath"},
        FunctionInfo{26, nullptr, "BeginInstallApplication"},
        FunctionInfo{27, nullptr, "DeleteApplicationRecord"},
        FunctionInfo{30, nullptr, "RequestApplicationUpdateInfo"},
        FunctionInfo{31, nullptr, "RequestUpdateApplication"},
        FunctionInfo{32, nullptr, "CancelApplicationDownload"},
        FunctionInfo{33, nullptr, "ResumeApplicationDownload"},
        FunctionInfo{35, nullptr, "UpdateVersionList"},
        FunctionInfo{36, nullptr, "PushLaunchVersion"},
        FunctionInfo{37, nullptr, "ListRequiredVersion"},
        FunctionInfo{38, D<&IApplicationManagerInterface::CheckApplicationLaunchVersion>, "CheckApplicationLaunchVersion"},
        FunctionInfo{39, nullptr, "CheckApplicationLaunchRights"},
        FunctionInfo{40, D<&IApplicationManagerInterface::GetApplicationLogoData>, "GetApplicationLogoData"},
        FunctionInfo{41, nullptr, "CalculateApplicationDownloadRequiredSize"},
        FunctionInfo{42, nullptr, "CleanupSdCard"},
        FunctionInfo{43, D<&IApplicationManagerInterface::CheckSdCardMountStatus>, "CheckSdCardMountStatus"},
        FunctionInfo{44, D<&IApplicationManagerInterface::GetSdCardMountStatusChangedEvent>, "GetSdCardMountStatusChangedEvent"},
        FunctionInfo{45, nullptr, "GetGameCardAttachmentEvent"},
        FunctionInfo{46, nullptr, "GetGameCardAttachmentInfo"},
        FunctionInfo{47, D<&IApplicationManagerInterface::GetTotalSpaceSize>, "GetTotalSpaceSize"},
        FunctionInfo{48, D<&IApplicationManagerInterface::GetFreeSpaceSize>, "GetFreeSpaceSize"},
        FunctionInfo{49, nullptr, "GetSdCardRemovedEvent"},
        FunctionInfo{52, D<&IApplicationManagerInterface::GetGameCardUpdateDetectionEvent>, "GetGameCardUpdateDetectionEvent"},
        FunctionInfo{53, nullptr, "DisableApplicationAutoDelete"},
        FunctionInfo{54, nullptr, "EnableApplicationAutoDelete"},
        FunctionInfo{55, D<&IApplicationManagerInterface::GetApplicationDesiredLanguage>, "GetApplicationDesiredLanguage"},
        FunctionInfo{56, nullptr, "SetApplicationTerminateResult"},
        FunctionInfo{57, nullptr, "ClearApplicationTerminateResult"},
        FunctionInfo{58, nullptr, "GetLastSdCardMountUnexpectedResult"},
        FunctionInfo{59, D<&IApplicationManagerInterface::ConvertApplicationLanguageToLanguageCode>, "ConvertApplicationLanguageToLanguageCode"},
        FunctionInfo{60, nullptr, "ConvertLanguageCodeToApplicationLanguage"},
        FunctionInfo{61, nullptr, "GetBackgroundDownloadStressTaskInfo"},
        FunctionInfo{62, nullptr, "GetGameCardStopper"},
        FunctionInfo{63, nullptr, "IsSystemProgramInstalled"},
        FunctionInfo{64, nullptr, "StartApplyDeltaTask"},
        FunctionInfo{65, nullptr, "GetRequestServerStopper"},
        FunctionInfo{66, nullptr, "GetBackgroundApplyDeltaStressTaskInfo"},
        FunctionInfo{67, nullptr, "CancelApplicationApplyDelta"},
        FunctionInfo{68, nullptr, "ResumeApplicationApplyDelta"},
        FunctionInfo{69, nullptr, "CalculateApplicationApplyDeltaRequiredSize"},
        FunctionInfo{70, D<&IApplicationManagerInterface::ResumeAll>, "ResumeAll"},
        FunctionInfo{71, D<&IApplicationManagerInterface::GetStorageSize>, "GetStorageSize"},
        FunctionInfo{80, nullptr, "RequestDownloadApplication"},
        FunctionInfo{81, nullptr, "RequestDownloadAddOnContent"},
        FunctionInfo{82, nullptr, "DownloadApplication"},
        FunctionInfo{83, nullptr, "CheckApplicationResumeRights"},
        FunctionInfo{84, nullptr, "GetDynamicCommitEvent"},
        FunctionInfo{85, nullptr, "RequestUpdateApplication2"},
        FunctionInfo{86, nullptr, "EnableApplicationCrashReport"},
        FunctionInfo{87, nullptr, "IsApplicationCrashReportEnabled"},
        FunctionInfo{90, nullptr, "BoostSystemMemoryResourceLimit"},
        FunctionInfo{91, nullptr, "DeprecatedLaunchApplication"},
        FunctionInfo{92, nullptr, "GetRunningApplicationProgramId"},
        FunctionInfo{93, nullptr, "GetMainApplicationProgramIndex"},
        FunctionInfo{94, nullptr, "LaunchApplication"},
        FunctionInfo{95, nullptr, "GetApplicationLaunchInfo"},
        FunctionInfo{96, nullptr, "AcquireApplicationLaunchInfo"},
        FunctionInfo{97, nullptr, "GetMainApplicationProgramIndexByApplicationLaunchInfo"},
        FunctionInfo{98, nullptr, "EnableApplicationAllThreadDumpOnCrash"},
        FunctionInfo{99, nullptr, "LaunchDevMenu"},
        FunctionInfo{100, nullptr, "ResetToFactorySettings"},
        FunctionInfo{101, nullptr, "ResetToFactorySettingsWithoutUserSaveData"},
        FunctionInfo{102, nullptr, "ResetToFactorySettingsForRefurbishment"},
        FunctionInfo{103, nullptr, "ResetToFactorySettingsWithPlatformRegion"},
        FunctionInfo{104, nullptr, "ResetToFactorySettingsWithPlatformRegionAuthentication"},
        FunctionInfo{105, nullptr, "RequestResetToFactorySettingsSecurely"},
        FunctionInfo{106, nullptr, "RequestResetToFactorySettingsWithPlatformRegionAuthenticationSecurely"},
        FunctionInfo{200, nullptr, "CalculateUserSaveDataStatistics"},
        FunctionInfo{201, nullptr, "DeleteUserSaveDataAll"},
        FunctionInfo{210, nullptr, "DeleteUserSystemSaveData"},
        FunctionInfo{211, nullptr, "DeleteSaveData"},
        FunctionInfo{220, nullptr, "UnregisterNetworkServiceAccount"},
        FunctionInfo{221, D<&IApplicationManagerInterface::UnregisterNetworkServiceAccountWithUserSaveDataDeletion>, "UnregisterNetworkServiceAccountWithUserSaveDataDeletion"},
        FunctionInfo{300, nullptr, "GetApplicationShellEvent"},
        FunctionInfo{301, nullptr, "PopApplicationShellEventInfo"},
        FunctionInfo{302, nullptr, "LaunchLibraryApplet"},
        FunctionInfo{303, nullptr, "TerminateLibraryApplet"},
        FunctionInfo{304, nullptr, "LaunchSystemApplet"},
        FunctionInfo{305, nullptr, "TerminateSystemApplet"},
        FunctionInfo{306, nullptr, "LaunchOverlayApplet"},
        FunctionInfo{307, nullptr, "TerminateOverlayApplet"},
        FunctionInfo{400, D<&IApplicationManagerInterface::GetApplicationControlData>, "GetApplicationControlData"},
        FunctionInfo{401, nullptr, "InvalidateAllApplicationControlCache"},
        FunctionInfo{402, nullptr, "RequestDownloadApplicationControlData"},
        FunctionInfo{403, nullptr, "GetMaxApplicationControlCacheCount"},
        FunctionInfo{404, nullptr, "InvalidateApplicationControlCache"},
        FunctionInfo{405, nullptr, "ListApplicationControlCacheEntryInfo"},
        FunctionInfo{406, nullptr, "GetApplicationControlProperty"},
        FunctionInfo{407, &IApplicationManagerInterface::ListApplicationTitle, "ListApplicationTitle"},
        FunctionInfo{408, &IApplicationManagerInterface::ListApplicationIcon, "ListApplicationIcon"},
        FunctionInfo{411, nullptr, "Unknown411"}, //19.0.0+
        FunctionInfo{412, nullptr, "Unknown412"}, //19.0.0+
        FunctionInfo{413, nullptr, "Unknown413"}, //19.0.0+
        FunctionInfo{414, nullptr, "Unknown414"}, //19.0.0+
        FunctionInfo{415, nullptr, "Unknown415"}, //19.0.0+
        FunctionInfo{416, nullptr, "Unknown416"}, //19.0.0+
        FunctionInfo{417, nullptr, "InvalidateAllApplicationControlCacheOfTheStage"}, //19.0.0+
        FunctionInfo{418, nullptr, "InvalidateApplicationControlCacheOfTheStage"}, //19.0.0+
        FunctionInfo{419, D<&IApplicationManagerInterface::RequestDownloadApplicationControlDataInBackground>, "RequestDownloadApplicationControlDataInBackground"},
        FunctionInfo{420, nullptr, "CloneApplicationControlDataCacheForDebug"},
        FunctionInfo{421, nullptr, "Unknown421"}, //20.0.0+
        FunctionInfo{422, nullptr, "Unknown422"}, //20.0.0+
        FunctionInfo{423, nullptr, "Unknown423"}, //20.0.0+
        FunctionInfo{424, nullptr, "Unknown424"}, //20.0.0+
        FunctionInfo{425, nullptr, "Unknown425"}, //20.0.0+
        FunctionInfo{426, nullptr, "Unknown426"}, //20.0.0+
        FunctionInfo{427, nullptr, "Unknown427"}, //20.0.0+
        FunctionInfo{428, nullptr, "Unknown428"}, //21.0.0+
        FunctionInfo{429, nullptr, "Unknown429"}, //21.0.0+
        FunctionInfo{430, nullptr, "Unknown430"}, //21.0.0+
        FunctionInfo{502, nullptr, "RequestCheckGameCardRegistration"},
        FunctionInfo{503, nullptr, "RequestGameCardRegistrationGoldPoint"},
        FunctionInfo{504, nullptr, "RequestRegisterGameCard"},
        FunctionInfo{505, D<&IApplicationManagerInterface::GetGameCardMountFailureEvent>, "GetGameCardMountFailureEvent"},
        FunctionInfo{506, nullptr, "IsGameCardInserted"},
        FunctionInfo{507, nullptr, "EnsureGameCardAccess"},
        FunctionInfo{508, nullptr, "GetLastGameCardMountFailureResult"},
        FunctionInfo{509, nullptr, "ListApplicationIdOnGameCard"},
        FunctionInfo{510, nullptr, "GetGameCardPlatformRegion"},
        FunctionInfo{511, D<&IApplicationManagerInterface::GetGameCardWakenReadyEvent>, "GetGameCardWakenReadyEvent"},
        FunctionInfo{512, D<&IApplicationManagerInterface::IsGameCardApplicationRunning>, "IsGameCardApplicationRunning"},
        FunctionInfo{513, nullptr, "Unknown513"}, //20.0.0+
        FunctionInfo{514, nullptr, "Unknown514"}, //20.0.0+
        FunctionInfo{515, nullptr, "Unknown515"}, //20.0.0+
        FunctionInfo{516, nullptr, "Unknown516"}, //21.0.0+
        FunctionInfo{517, nullptr, "Unknown517"}, //21.0.0+
        FunctionInfo{518, nullptr, "Unknown518"}, //21.0.0+
        FunctionInfo{519, nullptr, "Unknown519"}, //21.0.0+
        FunctionInfo{600, nullptr, "CountApplicationContentMeta"},
        FunctionInfo{601, nullptr, "ListApplicationContentMetaStatus"},
        FunctionInfo{602, nullptr, "ListAvailableAddOnContent"},
        FunctionInfo{603, nullptr, "GetOwnedApplicationContentMetaStatus"},
        FunctionInfo{604, nullptr, "RegisterContentsExternalKey"},
        FunctionInfo{605, nullptr, "ListApplicationContentMetaStatusWithRightsCheck"},
        FunctionInfo{606, nullptr, "GetContentMetaStorage"},
        FunctionInfo{607, nullptr, "ListAvailableAddOnContent"},
        FunctionInfo{609, nullptr, "ListAvailabilityAssuredAddOnContent"},
        FunctionInfo{610, nullptr, "GetInstalledContentMetaStorage"},
        FunctionInfo{611, nullptr, "PrepareAddOnContent"},
        FunctionInfo{700, nullptr, "PushDownloadTaskList"},
        FunctionInfo{701, nullptr, "ClearTaskStatusList"},
        FunctionInfo{702, nullptr, "RequestDownloadTaskList"},
        FunctionInfo{703, nullptr, "RequestEnsureDownloadTask"},
        FunctionInfo{704, nullptr, "ListDownloadTaskStatus"},
        FunctionInfo{705, nullptr, "RequestDownloadTaskListData"},
        FunctionInfo{800, nullptr, "RequestVersionList"},
        FunctionInfo{801, nullptr, "ListVersionList"},
        FunctionInfo{802, nullptr, "RequestVersionListData"},
        FunctionInfo{900, nullptr, "GetApplicationRecord"},
        FunctionInfo{901, nullptr, "GetApplicationRecordProperty"},
        FunctionInfo{902, nullptr, "EnableApplicationAutoUpdate"},
        FunctionInfo{903, nullptr, "DisableApplicationAutoUpdate"},
        FunctionInfo{904, D<&IApplicationManagerInterface::TouchApplication>, "TouchApplication"},
        FunctionInfo{905, nullptr, "RequestApplicationUpdate"},
        FunctionInfo{906, D<&IApplicationManagerInterface::IsApplicationUpdateRequested>, "IsApplicationUpdateRequested"},
        FunctionInfo{907, nullptr, "WithdrawApplicationUpdateRequest"},
        FunctionInfo{908, nullptr, "ListApplicationRecordInstalledContentMeta"},
        FunctionInfo{909, nullptr, "WithdrawCleanupAddOnContentsWithNoRightsRecommendation"},
        FunctionInfo{910, nullptr, "HasApplicationRecord"},
        FunctionInfo{911, nullptr, "SetPreInstalledApplication"},
        FunctionInfo{912, nullptr, "ClearPreInstalledApplicationFlag"},
        FunctionInfo{913, nullptr, "ListAllApplicationRecord"},
        FunctionInfo{914, nullptr, "HideApplicationRecord"},
        FunctionInfo{915, nullptr, "ShowApplicationRecord"},
        FunctionInfo{916, nullptr, "IsApplicationAutoDeleteDisabled"},
        FunctionInfo{916, nullptr, "Unknown916"}, //20.0.0+
        FunctionInfo{917, nullptr, "Unknown917"}, //20.0.0+
        FunctionInfo{918, nullptr, "Unknown918"}, //20.0.0+
        FunctionInfo{919, nullptr, "Unknown919"}, //20.0.0+
        FunctionInfo{920, nullptr, "Unknown920"}, //20.0.0+
        FunctionInfo{921, nullptr, "Unknown921"}, //20.0.0+
        FunctionInfo{922, nullptr, "Unknown922"}, //20.0.0+
        FunctionInfo{923, nullptr, "Unknown923"}, //20.0.0+
        FunctionInfo{928, nullptr, "Unknown928"}, //20.0.0+
        FunctionInfo{929, nullptr, "Unknown929"}, //20.0.0+
        FunctionInfo{930, nullptr, "Unknown930"}, //20.0.0+
        FunctionInfo{931, nullptr, "Unknown931"}, //20.0.0+
        FunctionInfo{933, nullptr, "Unknown933"}, //20.0.0+
        FunctionInfo{934, nullptr, "Unknown934"}, //21.0.0+
        FunctionInfo{935, nullptr, "Unknown935"}, //21.0.0+
        FunctionInfo{936, D<&IApplicationManagerInterface::Unknown936>, "Unknown936"}, //21.0.0+
        FunctionInfo{1000, nullptr, "RequestVerifyApplicationDeprecated"},
        FunctionInfo{1001, nullptr, "CorruptApplicationForDebug"},
        FunctionInfo{1002, nullptr, "RequestVerifyAddOnContentsRights"},
        FunctionInfo{1003, nullptr, "RequestVerifyApplication"},
        FunctionInfo{1004, nullptr, "CorruptContentForDebug"},
        FunctionInfo{1200, nullptr, "NeedsUpdateVulnerability"},
        FunctionInfo{1300, D<&IApplicationManagerInterface::IsAnyApplicationEntityInstalled>, "IsAnyApplicationEntityInstalled"},
        FunctionInfo{1301, nullptr, "DeleteApplicationContentEntities"},
        FunctionInfo{1302, nullptr, "CleanupUnrecordedApplicationEntity"},
        FunctionInfo{1303, nullptr, "CleanupAddOnContentsWithNoRights"},
        FunctionInfo{1304, nullptr, "DeleteApplicationContentEntity"},
        FunctionInfo{1305, nullptr, "TryDeleteRunningApplicationEntity"},
        FunctionInfo{1306, nullptr, "TryDeleteRunningApplicationCompletely"},
        FunctionInfo{1307, nullptr, "TryDeleteRunningApplicationContentEntities"},
        FunctionInfo{1308, nullptr, "DeleteApplicationCompletelyForDebug"},
        FunctionInfo{1309, nullptr, "CleanupUnavailableAddOnContents"},
        FunctionInfo{1310, nullptr, "RequestMoveApplicationEntity"},
        FunctionInfo{1311, nullptr, "EstimateSizeToMove"},
        FunctionInfo{1312, nullptr, "HasMovableEntity"},
        FunctionInfo{1313, nullptr, "CleanupOrphanContents"},
        FunctionInfo{1314, nullptr, "CheckPreconditionSatisfiedToMove"},
        FunctionInfo{1400, nullptr, "PrepareShutdown"},
        FunctionInfo{1500, nullptr, "FormatSdCard"},
        FunctionInfo{1501, nullptr, "NeedsSystemUpdateToFormatSdCard"},
        FunctionInfo{1502, nullptr, "GetLastSdCardFormatUnexpectedResult"},
        FunctionInfo{1504, nullptr, "InsertSdCard"},
        FunctionInfo{1505, nullptr, "RemoveSdCard"},
        FunctionInfo{1506, nullptr, "GetSdCardStartupStatus"},
        FunctionInfo{1508, nullptr, "Unknown1508"}, //20.0.0+
        FunctionInfo{1509, nullptr, "Unknown1509"}, //20.0.0+
        FunctionInfo{1510, nullptr, "Unknown1510"}, //20.0.0+
        FunctionInfo{1511, nullptr, "Unknown1511"}, //20.0.0+
        FunctionInfo{1512, nullptr, "Unknown1512"}, //20.0.0+
        FunctionInfo{1600, nullptr, "GetSystemSeedForPseudoDeviceId"},
        FunctionInfo{1601, nullptr, "ResetSystemSeedForPseudoDeviceId"},
        FunctionInfo{1700, nullptr, "ListApplicationDownloadingContentMeta"},
        FunctionInfo{1701, D<&IApplicationManagerInterface::GetApplicationViewDeprecated>, "GetApplicationViewDeprecated"},
        FunctionInfo{1702, nullptr, "GetApplicationDownloadTaskStatus"},
        FunctionInfo{1703, nullptr, "GetApplicationViewDownloadErrorContext"},
        FunctionInfo{1704, D<&IApplicationManagerInterface::GetApplicationViewWithPromotionInfo>, "GetApplicationViewWithPromotionInfo"},
        FunctionInfo{1705, nullptr, "IsPatchAutoDeletableApplication"},
        FunctionInfo{1706, D<&IApplicationManagerInterface::GetApplicationView>, "GetApplicationView"},
        FunctionInfo{1800, nullptr, "IsNotificationSetupCompleted"},
        FunctionInfo{1801, nullptr, "GetLastNotificationInfoCount"},
        FunctionInfo{1802, nullptr, "ListLastNotificationInfo"},
        FunctionInfo{1803, nullptr, "ListNotificationTask"},
        FunctionInfo{1900, nullptr, "IsActiveAccount"},
        FunctionInfo{1901, nullptr, "RequestDownloadApplicationPrepurchasedRights"},
        FunctionInfo{1902, nullptr, "GetApplicationTicketInfo"},
        FunctionInfo{1903, nullptr, "RequestDownloadApplicationPrepurchasedRightsForAccount"},
        FunctionInfo{2000, nullptr, "GetSystemDeliveryInfo"},
        FunctionInfo{2001, nullptr, "SelectLatestSystemDeliveryInfo"},
        FunctionInfo{2002, nullptr, "VerifyDeliveryProtocolVersion"},
        FunctionInfo{2003, nullptr, "GetApplicationDeliveryInfo"},
        FunctionInfo{2004, nullptr, "HasAllContentsToDeliver"},
        FunctionInfo{2005, nullptr, "CompareApplicationDeliveryInfo"},
        FunctionInfo{2006, nullptr, "CanDeliverApplication"},
        FunctionInfo{2007, nullptr, "ListContentMetaKeyToDeliverApplication"},
        FunctionInfo{2008, nullptr, "NeedsSystemUpdateToDeliverApplication"},
        FunctionInfo{2009, nullptr, "EstimateRequiredSize"},
        FunctionInfo{2010, nullptr, "RequestReceiveApplication"},
        FunctionInfo{2011, nullptr, "CommitReceiveApplication"},
        FunctionInfo{2012, nullptr, "GetReceiveApplicationProgress"},
        FunctionInfo{2013, nullptr, "RequestSendApplication"},
        FunctionInfo{2014, nullptr, "GetSendApplicationProgress"},
        FunctionInfo{2015, nullptr, "CompareSystemDeliveryInfo"},
        FunctionInfo{2016, nullptr, "ListNotCommittedContentMeta"},
        FunctionInfo{2017, nullptr, "CreateDownloadTask"},
        FunctionInfo{2018, nullptr, "GetApplicationDeliveryInfoHash"},
        FunctionInfo{2019, nullptr, "Unknown2019"}, //20.0.0+
        FunctionInfo{2050, D<&IApplicationManagerInterface::GetApplicationRightsOnClient>, "GetApplicationRightsOnClient"},
        FunctionInfo{2051, nullptr, "InvalidateRightsIdCache"},
        FunctionInfo{2052, nullptr, "Unknown2052"}, //20.0.0+
        FunctionInfo{2053, nullptr, "Unknown2053"}, //20.0.0+
        FunctionInfo{2100, D<&IApplicationManagerInterface::GetApplicationTerminateResult>, "GetApplicationTerminateResult"},
        FunctionInfo{2101, nullptr, "GetRawApplicationTerminateResult"},
        FunctionInfo{2150, nullptr, "CreateRightsEnvironment"},
        FunctionInfo{2151, nullptr, "DestroyRightsEnvironment"},
        FunctionInfo{2152, nullptr, "ActivateRightsEnvironment"},
        FunctionInfo{2153, nullptr, "DeactivateRightsEnvironment"},
        FunctionInfo{2154, nullptr, "ForceActivateRightsContextForExit"},
        FunctionInfo{2155, nullptr, "UpdateRightsEnvironmentStatus"},
        FunctionInfo{2156, nullptr, "CreateRightsEnvironmentForMicroApplication"},
        FunctionInfo{2160, nullptr, "AddTargetApplicationToRightsEnvironment"},
        FunctionInfo{2161, nullptr, "SetUsersToRightsEnvironment"},
        FunctionInfo{2170, nullptr, "GetRightsEnvironmentStatus"},
        FunctionInfo{2171, nullptr, "GetRightsEnvironmentStatusChangedEvent"},
        FunctionInfo{2180, nullptr, "RequestExtendRightsInRightsEnvironment"},
        FunctionInfo{2181, nullptr, "GetResultOfExtendRightsInRightsEnvironment"},
        FunctionInfo{2182, nullptr, "SetActiveRightsContextUsingStateToRightsEnvironment"},
        FunctionInfo{2183, nullptr, "Unknown2183"}, //20.1.0+
        FunctionInfo{2190, nullptr, "GetRightsEnvironmentHandleForApplication"},
        FunctionInfo{2199, nullptr, "GetRightsEnvironmentCountForDebug"},
        FunctionInfo{2200, nullptr, "GetGameCardApplicationCopyIdentifier"},
        FunctionInfo{2201, nullptr, "GetInstalledApplicationCopyIdentifier"},
        FunctionInfo{2250, nullptr, "RequestReportActiveELicence"},
        FunctionInfo{2300, nullptr, "ListEventLog"},
        FunctionInfo{2350, nullptr, "PerformAutoUpdateByApplicationId"},
        FunctionInfo{2351, nullptr, "RequestNoDownloadRightsErrorResolution"},
        FunctionInfo{2352, nullptr, "RequestResolveNoDownloadRightsError"},
        FunctionInfo{2353, nullptr, "GetApplicationDownloadTaskInfo"},
        FunctionInfo{2354, nullptr, "PrioritizeApplicationBackgroundTask"},
        FunctionInfo{2355, nullptr, "PreferStorageEfficientUpdate"},
        FunctionInfo{2356, nullptr, "RequestStorageEfficientUpdatePreferable"},
        FunctionInfo{2357, nullptr, "EnableMultiCoreDownload"},
        FunctionInfo{2358, nullptr, "DisableMultiCoreDownload"},
        FunctionInfo{2359, nullptr, "IsMultiCoreDownloadEnabled"},
        FunctionInfo{2360, nullptr, "GetApplicationDownloadTaskCount"}, //19.0.0+
        FunctionInfo{2361, nullptr, "GetMaxApplicationDownloadTaskCount"}, //19.0.0+
        FunctionInfo{2362, nullptr, "Unknown2362"}, //20.0.0+
        FunctionInfo{2363, nullptr, "Unknown2363"}, //20.0.0+
        FunctionInfo{2364, nullptr, "Unknown2364"}, //20.0.0+
        FunctionInfo{2365, nullptr, "Unknown2365"}, //20.0.0+
        FunctionInfo{2366, nullptr, "Unknown2366"}, //20.0.0+
        FunctionInfo{2367, nullptr, "Unknown2367"}, //20.0.0+
        FunctionInfo{2368, nullptr, "Unknown2368"}, //20.0.0+
        FunctionInfo{2369, nullptr, "Unknown2369"}, //21.0.0+
        FunctionInfo{2400, nullptr, "GetPromotionInfo"},
        FunctionInfo{2401, nullptr, "CountPromotionInfo"},
        FunctionInfo{2402, nullptr, "ListPromotionInfo"},
        FunctionInfo{2403, nullptr, "ImportPromotionJsonForDebug"},
        FunctionInfo{2404, nullptr, "ClearPromotionInfoForDebug"},
        FunctionInfo{2500, nullptr, "ConfirmAvailableTime"},
        FunctionInfo{2510, nullptr, "CreateApplicationResource"},
        FunctionInfo{2511, nullptr, "GetApplicationResource"},
        FunctionInfo{2513, nullptr, "LaunchMicroApplication"},
        FunctionInfo{2514, nullptr, "ClearTaskOfAsyncTaskManager"},
        FunctionInfo{2515, nullptr, "CleanupAllPlaceHolderAndFragmentsIfNoTask"},
        FunctionInfo{2516, nullptr, "EnsureApplicationCertificate"},
        FunctionInfo{2517, nullptr, "CreateApplicationInstance"},
        FunctionInfo{2518, nullptr, "UpdateQualificationForDebug"},
        FunctionInfo{2519, nullptr, "IsQualificationTransitionSupported"},
        FunctionInfo{2520, D<&IApplicationManagerInterface::IsQualificationTransitionSupportedByProcessId>, "IsQualificationTransitionSupportedByProcessId"},
        FunctionInfo{2521, nullptr, "GetRightsUserChangedEvent"},
        FunctionInfo{2522, nullptr, "IsRomRedirectionAvailable"},
        FunctionInfo{2523, nullptr, "GetProgramId"}, //17.0.0+
        FunctionInfo{2524, nullptr, "Unknown2524"}, //19.0.0+
        FunctionInfo{2525, nullptr, "Unknown2525"}, //20.0.0+
        FunctionInfo{2800, nullptr, "GetApplicationIdOfPreomia"},
        FunctionInfo{3000, nullptr, "RegisterDeviceLockKey"},
        FunctionInfo{3001, nullptr, "UnregisterDeviceLockKey"},
        FunctionInfo{3002, nullptr, "VerifyDeviceLockKey"},
        FunctionInfo{3003, nullptr, "HideApplicationIcon"},
        FunctionInfo{3004, nullptr, "ShowApplicationIcon"},
        FunctionInfo{3005, nullptr, "HideApplicationTitle"},
        FunctionInfo{3006, nullptr, "ShowApplicationTitle"},
        FunctionInfo{3007, nullptr, "EnableGameCard"},
        FunctionInfo{3008, nullptr, "DisableGameCard"},
        FunctionInfo{3009, nullptr, "EnableLocalContentShare"},
        FunctionInfo{3010, nullptr, "DisableLocalContentShare"},
        FunctionInfo{3011, nullptr, "IsApplicationIconHidden"},
        FunctionInfo{3012, nullptr, "IsApplicationTitleHidden"},
        FunctionInfo{3013, nullptr, "IsGameCardEnabled"},
        FunctionInfo{3014, nullptr, "IsLocalContentShareEnabled"},
        FunctionInfo{3050, nullptr, "ListAssignELicenseTaskResult"},
        FunctionInfo{3104, nullptr, "GetApplicationNintendoLogo"}, //18.0.0+
        FunctionInfo{3105, nullptr, "GetApplicationStartupMovie"}, //18.0.0+
        FunctionInfo{4000, nullptr, "Unknown4000"}, //20.0.0+
        FunctionInfo{4004, nullptr, "Unknown4004"}, //20.0.0+
        FunctionInfo{4006, nullptr, "Unknown4006"}, //20.0.0+
        FunctionInfo{4007, nullptr, "Unknown4007"}, //20.0.0+
        FunctionInfo{4008, nullptr, "Unknown4008"}, //20.0.0+
        FunctionInfo{4009, nullptr, "Unknown4009"}, //20.0.0+
        FunctionInfo{4010, nullptr, "Unknown4010"}, //20.0.0+
        FunctionInfo{4011, nullptr, "Unknown4011"}, //20.0.0+
        FunctionInfo{4012, nullptr, "Unknown4012"}, //20.0.0+
        FunctionInfo{4013, nullptr, "Unknown4013"}, //20.0.0+
        FunctionInfo{4015, nullptr, "Unknown4015"}, //20.0.0+
        FunctionInfo{4017, nullptr, "Unknown4017"}, //20.0.0+
        FunctionInfo{4019, nullptr, "Unknown4019"}, //20.0.0+
        FunctionInfo{4020, nullptr, "Unknown4020"}, //20.0.0+
        FunctionInfo{4021, nullptr, "Unknown4021"}, //20.0.0+
        FunctionInfo{4022, D<&IApplicationManagerInterface::Unknown4022>, "Unknown4022"}, //20.0.0+
        FunctionInfo{4023, D<&IApplicationManagerInterface::Unknown4023>, "Unknown4023"}, //20.0.0+
        FunctionInfo{4024, nullptr, "Unknown4024"}, //20.0.0+
        FunctionInfo{4025, nullptr, "Unknown4025"}, //20.0.0+
        FunctionInfo{4026, nullptr, "Unknown4026"}, //20.0.0+
        FunctionInfo{4027, nullptr, "Unknown4027"}, //20.0.0+
        FunctionInfo{4028, nullptr, "Unknown4028"}, //20.0.0+
        FunctionInfo{4029, nullptr, "Unknown4029"}, //20.0.0+
        FunctionInfo{4030, nullptr, "Unknown4030"}, //20.0.0+
        FunctionInfo{4031, nullptr, "Unknown4031"}, //20.0.0+
        FunctionInfo{4032, nullptr, "Unknown4032"}, //20.0.0+
        FunctionInfo{4033, nullptr, "Unknown4033"}, //20.0.0+
        FunctionInfo{4034, nullptr, "Unknown4034"}, //20.0.0+
        FunctionInfo{4035, nullptr, "Unknown4035"}, //20.0.0+
        FunctionInfo{4037, nullptr, "Unknown4037"}, //20.0.0+
        FunctionInfo{4038, nullptr, "Unknown4038"}, //20.0.0+
        FunctionInfo{4039, nullptr, "Unknown4039"}, //20.0.0+
        FunctionInfo{4040, nullptr, "Unknown4040"}, //20.0.0+
        FunctionInfo{4041, nullptr, "Unknown4041"}, //20.0.0+
        FunctionInfo{4042, D<&IApplicationManagerInterface::Unknown4042>, "Unknown4042"}, //20.0.0+
        FunctionInfo{4043, nullptr, "Unknown4043"}, //20.0.0+
        FunctionInfo{4044, nullptr, "Unknown4044"}, //20.0.0+
        FunctionInfo{4045, nullptr, "Unknown4045"}, //20.0.0+
        FunctionInfo{4046, nullptr, "Unknown4046"}, //20.0.0+
        FunctionInfo{4049, nullptr, "Unknown4049"}, //20.0.0+
        FunctionInfo{4050, nullptr, "Unknown4050"}, //20.0.0+
        FunctionInfo{4051, nullptr, "Unknown4051"}, //20.0.0+
        FunctionInfo{4052, nullptr, "Unknown4052"}, //20.0.0+
        FunctionInfo{4053, D<&IApplicationManagerInterface::Unknown4053>, "Unknown4053"}, //20.0.0+
        FunctionInfo{4054, nullptr, "Unknown4054"}, //20.0.0+
        FunctionInfo{4055, nullptr, "Unknown4055"}, //20.0.0+
        FunctionInfo{4056, nullptr, "Unknown4056"}, //20.0.0+
        FunctionInfo{4057, nullptr, "Unknown4057"}, //20.0.0+
        FunctionInfo{4058, nullptr, "Unknown4058"}, //20.0.0+
        FunctionInfo{4059, nullptr, "Unknown4059"}, //20.0.0+
        FunctionInfo{4060, nullptr, "Unknown4060"}, //20.0.0+
        FunctionInfo{4061, nullptr, "Unknown4061"}, //20.0.0+
        FunctionInfo{4062, nullptr, "Unknown4062"}, //20.0.0+
        FunctionInfo{4063, nullptr, "Unknown4063"}, //20.0.0+
        FunctionInfo{4064, nullptr, "Unknown4064"}, //20.0.0+
        FunctionInfo{4065, nullptr, "Unknown4065"}, //20.0.0+
        FunctionInfo{4066, nullptr, "Unknown4066"}, //20.0.0+
        FunctionInfo{4067, nullptr, "Unknown4067"}, //20.0.0+
        FunctionInfo{4068, nullptr, "Unknown4068"}, //20.0.0+
        FunctionInfo{4069, nullptr, "Unknown4069"}, //20.0.0+
        FunctionInfo{4070, nullptr, "Unknown4070"}, //20.0.0+
        FunctionInfo{4071, nullptr, "Unknown4071"}, //20.0.0+
        FunctionInfo{4072, nullptr, "Unknown4072"}, //20.0.0+
        FunctionInfo{4073, nullptr, "Unknown4073"}, //20.0.0+
        FunctionInfo{4074, nullptr, "Unknown4074"}, //20.0.0+
        FunctionInfo{4075, nullptr, "Unknown4075"}, //20.0.0+
        FunctionInfo{4076, nullptr, "Unknown4076"}, //20.0.0+
        FunctionInfo{4077, nullptr, "Unknown4077"}, //20.0.0+
        FunctionInfo{4078, nullptr, "Unknown4078"}, //20.0.0+
        FunctionInfo{4079, nullptr, "Unknown4079"}, //20.0.0+
        FunctionInfo{4080, nullptr, "Unknown4080"}, //20.0.0+
        FunctionInfo{4081, nullptr, "Unknown4081"}, //20.0.0+
        FunctionInfo{4083, nullptr, "Unknown4083"}, //20.0.0+
        FunctionInfo{4084, nullptr, "Unknown4084"}, //20.0.0+
        FunctionInfo{4085, nullptr, "Unknown4085"}, //20.0.0+
        FunctionInfo{4086, nullptr, "Unknown4086"}, //20.0.0+
        FunctionInfo{4087, nullptr, "Unknown4087"}, //20.0.0+
        FunctionInfo{4088, D<&IApplicationManagerInterface::Unknown4022>, "Unknown4088"}, //20.0.0+
        FunctionInfo{4089, nullptr, "Unknown4089"}, //20.0.0+
        FunctionInfo{4090, nullptr, "Unknown4090"}, //20.0.0+
        FunctionInfo{4091, nullptr, "Unknown4091"}, //20.0.0+
        FunctionInfo{4092, nullptr, "Unknown4092"}, //20.0.0+
        FunctionInfo{4093, nullptr, "Unknown4093"}, //20.0.0+
        FunctionInfo{4094, nullptr, "Unknown4094"}, //20.0.0+
        FunctionInfo{4095, nullptr, "Unknown4095"}, //20.0.0+
        FunctionInfo{4096, nullptr, "Unknown4096"}, //20.0.0+
        FunctionInfo{4097, nullptr, "Unknown4097"}, //20.0.0+
        FunctionInfo{4099, nullptr, "Unknown4099"}, //21.0.0+
        FunctionInfo{4105, D<&IApplicationManagerInterface::Unknown4105>, "Unknown4105"}, //23.0.0+
        FunctionInfo{5000, nullptr, "Unknown5000"}, //18.0.0+
        FunctionInfo{5001, nullptr, "Unknown5001"}, //18.0.0+
        FunctionInfo{9999, nullptr, "GetApplicationCertificate"}, //10.0.0-10.2.0
    };
    // clang-format on

    RegisterHandlers(functions);
}

IApplicationManagerInterface::~IApplicationManagerInterface() = default;

Result IApplicationManagerInterface::UnregisterNetworkServiceAccountWithUserSaveDataDeletion(Common::UUID user_id) {
    LOG_DEBUG(Service_NS, "called, user_id={}", user_id.FormattedString());
    R_SUCCEED();
}

Result IApplicationManagerInterface::GetApplicationLogoData(
    Out<s64> out_size, OutBuffer<BufferAttr_HipcMapAlias> out_buffer, u64 application_id,
    InBuffer<BufferAttr_HipcMapAlias> logo_path_buffer) {
    const std::string path_view{reinterpret_cast<const char*>(logo_path_buffer.data()),
                                logo_path_buffer.size()};

    // Find null terminator and trim the path
    auto null_pos = path_view.find('\0');
    std::string path = (null_pos != std::string::npos) ? path_view.substr(0, null_pos) : path_view;

    LOG_DEBUG(Service_NS, "called, application_id={:016X}, logo_path={}", application_id, path);

    auto& content_provider = system.GetContentProviderUnion();

    auto program = content_provider.GetEntry(application_id, FileSys::ContentRecordType::Program);
    if (!program) {
        LOG_WARNING(Service_NS, "Application program not found for id={:016X}", application_id);
        R_RETURN(ResultUnknown);
    }

    const auto logo_dir = program->GetLogoPartition();
    if (!logo_dir) {
        LOG_WARNING(Service_NS, "Logo partition not found for id={:016X}", application_id);
        R_RETURN(ResultUnknown);
    }

    const auto file = logo_dir->GetFile(path);
    if (!file) {
        LOG_WARNING(Service_NS, "Logo path not found: {} for id={:016X}", path,
                    application_id);
        R_RETURN(ResultUnknown);
    }

    const auto data = file->ReadAllBytes();
    if (data.size() > out_buffer.size()) {
        LOG_WARNING(Service_NS, "Logo buffer too small: have={}, need={}", out_buffer.size(),
                    data.size());
        R_RETURN(ResultUnknown);
    }

    std::memcpy(out_buffer.data(), data.data(), data.size());
    *out_size = static_cast<s64>(data.size());

    R_SUCCEED();
}

Result IApplicationManagerInterface::GetApplicationControlData(
    OutBuffer<BufferAttr_HipcMapAlias> out_buffer, Out<u32> out_actual_size,
    ApplicationControlSource application_control_source, u64 application_id) {
    LOG_DEBUG(Service_NS, "called");
    R_RETURN(IReadOnlyApplicationControlDataInterface(system).GetApplicationControlData(
        out_buffer, out_actual_size, application_control_source, application_id));
}

Result IApplicationManagerInterface::GetApplicationDesiredLanguage(
    Out<ApplicationLanguage> out_desired_language, u32 supported_languages) {
    LOG_DEBUG(Service_NS, "called");
    R_RETURN(IReadOnlyApplicationControlDataInterface(system).GetApplicationDesiredLanguage(
        out_desired_language, supported_languages));
}

Result IApplicationManagerInterface::ConvertApplicationLanguageToLanguageCode(
    Out<u64> out_language_code, ApplicationLanguage application_language) {
    LOG_DEBUG(Service_NS, "called");
    R_RETURN(
        IReadOnlyApplicationControlDataInterface(system).ConvertApplicationLanguageToLanguageCode(
            out_language_code, application_language));
}

Result IApplicationManagerInterface::ListApplicationRecord(
    OutArray<ApplicationRecord, BufferAttr_HipcMapAlias> out_records, Out<s32> out_count,
    s32 offset) {
    const auto limit = out_records.size();

    LOG_DEBUG(Service_NS, "called");
    const auto& cache = system.GetContentProviderUnion();
    const auto installed_games = cache.ListEntriesFilterOrigin(
        std::nullopt, FileSys::TitleType::Application, FileSys::ContentRecordType::Program);

    std::vector<ApplicationRecord> records;
    records.reserve(installed_games.size());

    for (const auto& [slot, game] : installed_games) {
         if (game.title_id == 0 || game.title_id < 0x0100000000001FFFull) {
             continue;
         }
         if ((game.title_id & 0xFFF) != 0) {
             continue; // skip sub-programs (e.g., 001)
         }

         ApplicationRecord record{};
         record.application_id = game.title_id;
         record.last_event = ApplicationEvent::Installed;
         record.attributes = 0;
         record.last_updated = Core::LaunchTimestampCache::GetLaunchTimestamp(game.title_id);

         records.push_back(record);
     }

     std::sort(records.begin(), records.end(), [](const ApplicationRecord& lhs, const ApplicationRecord& rhs) {
         if (lhs.last_updated == rhs.last_updated) {
             return lhs.application_id < rhs.application_id;
         }
         return lhs.last_updated > rhs.last_updated;
     });

    size_t i = 0;
    const size_t start = static_cast<size_t>(std::max(0, offset));
    for (size_t idx = start; idx < records.size() && i < limit; ++idx) {
        out_records[i++] = records[idx];
    }

    *out_count = static_cast<s32>(i);
    R_SUCCEED();
}

Result IApplicationManagerInterface::GetApplicationRecordUpdateSystemEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Service_NS, "(STUBBED) called");

    record_update_system_event.Signal(system.Kernel());
    *out_event = record_update_system_event.GetHandle();

    R_SUCCEED();
}

Result IApplicationManagerInterface::GetGameCardMountFailureEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Service_NS, "(STUBBED) called");
    *out_event = gamecard_mount_failure_event.GetHandle();
    R_SUCCEED();
}

Result IApplicationManagerInterface::GetGameCardWakenReadyEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Service_NS, "(STUBBED) called");
    *out_event = gamecard_waken_ready_event.GetHandle();
    R_SUCCEED();
}

Result IApplicationManagerInterface::IsGameCardApplicationRunning(Out<bool> out_is_running) {
    LOG_WARNING(Service_NS, "(STUBBED) called");
    *out_is_running = false;
    R_SUCCEED();
}

Result IApplicationManagerInterface::Unknown936(Out<u64> out_result) {
    LOG_WARNING(Service_NS, "(STUBBED) called.");
    *out_result = 0;
    R_SUCCEED();
}

Result IApplicationManagerInterface::IsAnyApplicationEntityInstalled(
    Out<bool> out_is_any_application_entity_installed) {
    LOG_WARNING(Service_NS, "(STUBBED) called");
    *out_is_any_application_entity_installed = true;
    R_SUCCEED();
}

Result IApplicationManagerInterface::DeleteApplicationEntity(u64 application_id) {
    LOG_DEBUG(Service_NS, "called, application_id={:016X}", application_id);

    auto& fsc = system.GetFileSystemController();
    if (auto* const user_cache = fsc.GetUserNANDContents(); user_cache != nullptr) {
        user_cache->RemoveExistingEntry(application_id);
        user_cache->Refresh();
    }
    if (auto* const sdmc_cache = fsc.GetSDMCContents(); sdmc_cache != nullptr) {
        sdmc_cache->RemoveExistingEntry(application_id);
        sdmc_cache->Refresh();
    }

    record_update_system_event.Signal(system.Kernel());
    R_SUCCEED();
}

Result IApplicationManagerInterface::DeleteApplicationCompletely(u64 application_id) {
    R_RETURN(DeleteApplicationEntity(application_id));
}

Result IApplicationManagerInterface::GetApplicationViewDeprecated(
    OutArray<ApplicationViewV19, BufferAttr_HipcMapAlias> out_application_views,
    InArray<u64, BufferAttr_HipcMapAlias> application_ids) {
    const auto size = (std::min)(out_application_views.size(), application_ids.size());
    LOG_WARNING(Service_NS, "(STUBBED) called, size={}", application_ids.size());

    for (size_t i = 0; i < size; i++) {
        ApplicationViewV19 view{};
        view.application_id = application_ids[i];
        view.version = 0x70000;
        view.flags = 0x401f17;

        out_application_views[i] = view;
    }

    R_SUCCEED();
}

Result IApplicationManagerInterface::GetApplicationViewWithPromotionInfo(
    OutBuffer<BufferAttr_HipcMapAlias> out_buffer,
    Out<u32> out_count,
    InArray<u64, BufferAttr_HipcMapAlias> application_ids) {
    const auto requested = application_ids.size();
    LOG_WARNING(Service_NS, "called, size={}", requested);

    const auto fw_pair = FirmwareManager::GetFirmwareVersion(system);
    const bool is_fw20 = fw_pair.first.major >= 20;

    const size_t per_entry_size = is_fw20 ? (sizeof(ApplicationViewV20) + sizeof(PromotionInfo))
                                             : (sizeof(ApplicationViewV19) + sizeof(PromotionInfo));
    const size_t capacity_entries = out_buffer.size() / per_entry_size;
    const size_t to_write_entries = (std::min)(requested, capacity_entries);

    u8* dst = out_buffer.data();
    for (size_t i = 0; i < to_write_entries; ++i) {
        ApplicationViewWithPromotionData data{};
        data.view.application_id = application_ids[i];
        data.view.version = 0x70000;
        data.view.unk = 0;
        data.view.flags = 0x401f17;
        data.view.download_state = {};
        data.view.download_progress = {};
        data.promotion = {};

        const size_t written = WriteApplicationViewWithPromotion(dst, out_buffer.size() - (dst - out_buffer.data()), data, is_fw20);
        if (written == 0) {
            break;
        }
        dst += written;
    }

    *out_count = static_cast<u32>(dst - out_buffer.data()) / static_cast<u32>(per_entry_size);
    R_SUCCEED();
}

Result IApplicationManagerInterface::GetApplicationView(
    OutArray<ApplicationViewV20, BufferAttr_HipcMapAlias> out_application_views,
    InArray<u64, BufferAttr_HipcMapAlias> application_ids) {
    const auto size = (std::min)(out_application_views.size(), application_ids.size());
    LOG_WARNING(Service_NS, "(STUBBED) called, size={}", application_ids.size());

    for (size_t i = 0; i < size; i++) {
        ApplicationViewV20 view{};
        view.application_id = application_ids[i];
        view.version = 0x70000;
        view.unk = 0;
        view.flags = 0x401f17;

        out_application_views[i] = view;
    }

    R_SUCCEED();
}

Result IApplicationManagerInterface::GetApplicationRightsOnClient(
    OutArray<ApplicationRightsOnClient, BufferAttr_HipcMapAlias> out_rights, Out<u32> out_count,
    u32 flags, u64 application_id, Uid account_id) {
    LOG_WARNING(Service_NS, "(STUBBED) called, flags={}, application_id={:016X}, account_id={}",
                flags, application_id, account_id.uuid.FormattedString());

    if (!out_rights.empty()) {
        ApplicationRightsOnClient rights{};
        rights.application_id = application_id;
        rights.uid = account_id.uuid;
        rights.flags = 0;
        rights.flags2 = 0;

        out_rights[0] = rights;
        *out_count = 1;
    } else {
        *out_count = 0;
    }

    R_SUCCEED();
}

Result IApplicationManagerInterface::CheckSdCardMountStatus() {
    LOG_DEBUG(Service_NS, "called");
    R_RETURN(IContentManagementInterface(system).CheckSdCardMountStatus());
}

Result IApplicationManagerInterface::GetSdCardMountStatusChangedEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Service_NS, "(STUBBED) called");
    *out_event = sd_card_mount_status_event.GetHandle();
    R_SUCCEED();
}

Result IApplicationManagerInterface::GetTotalSpaceSize(Out<s64> out_total_space_size, FileSys::StorageId storage_id) {
    LOG_DEBUG(Service_NS, "called");
    R_RETURN(IContentManagementInterface(system).GetTotalSpaceSize(out_total_space_size, storage_id));
}

Result IApplicationManagerInterface::GetFreeSpaceSize(Out<s64> out_free_space_size, FileSys::StorageId storage_id) {
    LOG_DEBUG(Service_NS, "called");
    R_RETURN(IContentManagementInterface(system).GetFreeSpaceSize(out_free_space_size, storage_id));
}

Result IApplicationManagerInterface::GetGameCardUpdateDetectionEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Service_NS, "(STUBBED) called");
    *out_event = gamecard_update_detection_event.GetHandle();
    R_SUCCEED();
}

Result IApplicationManagerInterface::ResumeAll() {
    LOG_WARNING(Service_NS, "(STUBBED) called");
    R_SUCCEED();
}

Result IApplicationManagerInterface::IsQualificationTransitionSupportedByProcessId(
    Out<bool> out_is_supported, u64 process_id) {
    LOG_WARNING(Service_NS, "(STUBBED) called, process_id={}", process_id);
    *out_is_supported = true;
    R_SUCCEED();
}

Result IApplicationManagerInterface::GetStorageSize(Out<s64> out_total_space_size,
                                                    Out<s64> out_free_space_size,
                                                    FileSys::StorageId storage_id) {
    LOG_INFO(Service_NS, "called, storage_id={}", storage_id);
    *out_total_space_size = system.GetFileSystemController().GetTotalSpaceSize(storage_id);
    *out_free_space_size = system.GetFileSystemController().GetFreeSpaceSize(storage_id);
    R_SUCCEED();
}

Result IApplicationManagerInterface::TouchApplication(u64 application_id) {
    LOG_WARNING(Service_NS, "(STUBBED) called. application_id={:016X}", application_id);
    R_SUCCEED();
}

Result IApplicationManagerInterface::IsApplicationUpdateRequested(Out<bool> out_update_required,
                                                                  Out<u32> out_update_version,
                                                                  u64 application_id) {
    LOG_WARNING(Service_NS, "(STUBBED) called. application_id={:016X}", application_id);
    *out_update_required = false;
    *out_update_version = 0;
    R_SUCCEED();
}

Result IApplicationManagerInterface::CheckApplicationLaunchVersion(u64 application_id) {
    LOG_WARNING(Service_NS, "(STUBBED) called. application_id={:016X}", application_id);
    R_SUCCEED();
}

Result IApplicationManagerInterface::GetApplicationTerminateResult(Out<Result> out_result,
                                                                   u64 application_id) {
    LOG_WARNING(Service_NS, "(STUBBED) called. application_id={:016X}", application_id);
    *out_result = ResultSuccess;
    R_SUCCEED();
}

Result IApplicationManagerInterface::RequestDownloadApplicationControlDataInBackground(
    u64 control_source, u64 application_id) {
    LOG_INFO(Service_NS, "called, control_source={} app={:016X}",
             control_source, application_id);

    unknown_event.Signal(system.Kernel());
    R_SUCCEED();
}

Result IApplicationManagerInterface::Unknown4022(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Service_NS, "(STUBBED) called");
    unknown_event.Signal(system.Kernel());
    *out_event = unknown_event.GetHandle();
    R_SUCCEED();
}

Result IApplicationManagerInterface::Unknown4023(Out<u64> out_result) {
    LOG_WARNING(Service_NS, "(STUBBED) called.");
    *out_result = 0;
    R_SUCCEED();
}

Result IApplicationManagerInterface::Unknown4042(OutInterface<IAsyncResult> out_interface,
                                                 OutCopyHandle<Kernel::KReadableEvent> out_event,
                                                 u64 arg1, u64 arg2) {
    LOG_WARNING(Service_NS, "(STUBBED) called, arg1={:016X}, arg2={:016X}", arg1, arg2);
    *out_event = unknown_event.GetHandle();
    *out_interface = std::make_shared<IAsyncResult>(system, &unknown_event);
    R_SUCCEED();
}

Result IApplicationManagerInterface::Unknown4053() {
    LOG_WARNING(Service_NS, "(STUBBED) called.");
    R_SUCCEED();
}

Result IApplicationManagerInterface::Unknown4105() {
    LOG_WARNING(Service_NS, "(STUBBED) called.");
    R_SUCCEED();
}

void IApplicationManagerInterface::PushApplicationRecord(HLERequestContext& ctx) {
    const auto record = ctx.ReadBuffer();
    u64 application_id{};
    if (record.size() >= sizeof(application_id)) {
        std::memcpy(&application_id, record.data(), sizeof(application_id));
    }

    LOG_DEBUG(Service_NS, "called, application_id={:016X}, size={}", application_id, record.size());

    auto& fsc = system.GetFileSystemController();
    if (auto* const user_cache = fsc.GetUserNANDContents(); user_cache != nullptr) {
        user_cache->Refresh();
    }
    if (auto* const sdmc_cache = fsc.GetSDMCContents(); sdmc_cache != nullptr) {
        sdmc_cache->Refresh();
    }

    record_update_system_event.Signal(system.Kernel());

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IApplicationManagerInterface::ListApplicationTitle(HLERequestContext& ctx) {
    LOG_DEBUG(Service_NS, "called");
    IReadOnlyApplicationControlDataInterface(system).ListApplicationTitle(ctx);
}

void IApplicationManagerInterface::ListApplicationIcon(HLERequestContext& ctx) {
    LOG_DEBUG(Service_NS, "called");
    IReadOnlyApplicationControlDataInterface(system).ListApplicationIcon(ctx);
}

} // namespace Service::NS
