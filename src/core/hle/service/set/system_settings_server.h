// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

#include "common/polyfill_thread.h"
#include "common/uuid.h"
#include "core/hle/result.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/psc/time/common.h"
#include "core/hle/service/service.h"
#include "core/hle/service/set/setting_formats/appln_settings.h"
#include "core/hle/service/set/setting_formats/device_settings.h"
#include "core/hle/service/set/setting_formats/private_settings.h"
#include "core/hle/service/set/setting_formats/system_settings.h"
#include "core/hle/service/set/settings_types.h"

namespace Core {
class System;
}

namespace Service::Set {

Result GetFirmwareVersionImpl(FirmwareVersionFormat& out_firmware, Core::System& system,
                              GetFirmwareVersionType type);

class ISystemSettingsServer final : public ServiceFramework<ISystemSettingsServer> {
public:
    explicit ISystemSettingsServer(Core::System& system_);
    ~ISystemSettingsServer() override;

    Result GetSettingsItemValueImpl(std::span<u8> out_value, u64& out_size,
                                    const std::string& category, const std::string& name);

    template <typename T>
    Result GetSettingsItemValueImpl(T& out_value, const std::string& category,
                                    const std::string& name) {
        u64 data_size{};
        std::vector<u8> data(sizeof(T));
        R_TRY(GetSettingsItemValueImpl(data, data_size, category, name));
        std::memcpy(&out_value, data.data(), data_size);
        R_SUCCEED();
    }

public:
    Result SetLanguageCode(LanguageCode language_code);
    Result GetFirmwareVersion(
        OutLargeData<FirmwareVersionFormat, BufferAttr_HipcPointer> out_firmware_data);
    Result GetFirmwareVersion2(
        OutLargeData<FirmwareVersionFormat, BufferAttr_HipcPointer> out_firmware_data);
    Result GetLockScreenFlag(Out<bool> out_lock_screen_flag);
    Result SetLockScreenFlag(bool lock_screen_flag);
    Result GetExternalSteadyClockSourceId(Out<Common::UUID> out_clock_source_id);
    Result SetExternalSteadyClockSourceId(const Common::UUID& clock_source_id);
    Result GetUserSystemClockContext(Out<Service::PSC::Time::SystemClockContext> out_clock_context);
    Result SetUserSystemClockContext(const Service::PSC::Time::SystemClockContext& clock_context);
    Result GetAccountSettings(Out<AccountSettings> out_account_settings);
    Result SetAccountSettings(AccountSettings account_settings);
    Result GetEulaVersions(Out<s32> out_count,
                           OutArray<EulaVersion, BufferAttr_HipcMapAlias> out_eula_versions);
    Result SetEulaVersions(InArray<EulaVersion, BufferAttr_HipcMapAlias> eula_versions);
    Result GetColorSetId(Out<ColorSet> out_color_set_id);
    Result SetColorSetId(ColorSet color_set_id);
    Result GetNotificationSettings(Out<NotificationSettings> out_notification_settings);
    Result SetNotificationSettings(const NotificationSettings& notification_settings);
    Result GetAccountNotificationSettings(
        Out<s32> out_count, OutArray<AccountNotificationSettings, BufferAttr_HipcMapAlias>
                                out_account_notification_settings);
    Result SetAccountNotificationSettings(
        InArray<AccountNotificationSettings, BufferAttr_HipcMapAlias>
            account_notification_settings);
    Result GetVibrationMasterVolume(Out<f32> vibration_master_volume);
    Result SetVibrationMasterVolume(f32 vibration_master_volume);
    Result GetSettingsItemValueSize(
        Out<u64> out_size,
        InLargeData<SettingItemName, BufferAttr_HipcPointer> setting_category_buffer,
        InLargeData<SettingItemName, BufferAttr_HipcPointer> setting_name_buf);
    Result GetSettingsItemValue(
        Out<u64> out_size, OutBuffer<BufferAttr_HipcMapAlias> out_data,
        InLargeData<SettingItemName, BufferAttr_HipcPointer> setting_category_buffer,
        InLargeData<SettingItemName, BufferAttr_HipcPointer> setting_name_buffer);
    Result GetTvSettings(Out<TvSettings> out_tv_settings);
    Result SetTvSettings(TvSettings tv_settings);
    Result GetAudioOutputMode(Out<AudioOutputMode> out_output_mode, AudioOutputModeTarget target);
    Result SetAudioOutputMode(AudioOutputModeTarget target, AudioOutputMode output_mode);
    Result GetSpeakerAutoMuteFlag(Out<bool> out_force_mute_on_headphone_removed);
    Result SetSpeakerAutoMuteFlag(bool force_mute_on_headphone_removed);
    Result GetQuestFlag(Out<QuestFlag> out_quest_flag);
    Result SetQuestFlag(QuestFlag quest_flag);
    Result GetRebootlessSystemUpdateVersion(
        Out<RebootlessSystemUpdateVersion> out_rebootless_system_update);
    Result GetDeviceTimeZoneLocationName(Out<Service::PSC::Time::LocationName> out_name);
    Result SetDeviceTimeZoneLocationName(const Service::PSC::Time::LocationName& name);
    Result SetRegionCode(SystemRegionCode region_code);
    Result GetNetworkSystemClockContext(Out<Service::PSC::Time::SystemClockContext> out_context);
    Result SetNetworkSystemClockContext(const Service::PSC::Time::SystemClockContext& context);
    Result IsUserSystemClockAutomaticCorrectionEnabled(Out<bool> out_automatic_correction_enabled);
    Result SetUserSystemClockAutomaticCorrectionEnabled(bool automatic_correction_enabled);
    Result GetDebugModeFlag(Out<bool> is_debug_mode_enabled);
    Result GetPrimaryAlbumStorage(Out<PrimaryAlbumStorage> out_primary_album_storage);
    Result SetPrimaryAlbumStorage(PrimaryAlbumStorage primary_album_storage);
    Result GetBatteryLot(Out<BatteryLot> out_battery_lot);
    Result GetSerialNumber(Out<SerialNumber> out_console_serial);
    Result GetConsoleInformationUploadFlag(Out<bool> out_flag);
    Result SetConsoleInformationUploadFlag(bool flag);
    Result GetAutomaticApplicationDownloadFlag(Out<bool> out_flag);
    Result SetAutomaticApplicationDownloadFlag(bool flag);
    Result GetUsb30EnableFlag(Out<bool> out_usb30_enable_flag);
    Result SetUsb30EnableFlag(bool usb30_enable_flag);
    Result GetNfcEnableFlag(Out<bool> out_nfc_enable_flag);
    Result SetNfcEnableFlag(bool nfc_enable_flag);
    Result GetSleepSettings(Out<SleepSettings> out_sleep_settings);
    Result SetSleepSettings(SleepSettings sleep_settings);
    Result GetWirelessLanEnableFlag(Out<bool> out_wireless_lan_enable_flag);
    Result SetWirelessLanEnableFlag(bool wireless_lan_enable_flag);
    Result GetInitialLaunchSettings(Out<InitialLaunchSettings> out_initial_launch_settings);
    Result SetInitialLaunchSettings(InitialLaunchSettings initial_launch_settings);
    Result GetDeviceNickName(
        OutLargeData<std::array<u8, 0x80>, BufferAttr_HipcMapAlias> out_device_name);
    Result SetDeviceNickName(
        InLargeData<std::array<u8, 0x80>, BufferAttr_HipcMapAlias> device_name_buffer);
    Result GetProductModel(Out<u32> out_product_model);
    Result GetBluetoothEnableFlag(Out<bool> out_bluetooth_enable_flag);
    Result SetBluetoothEnableFlag(bool bluetooth_enable_flag);
    Result GetMiiAuthorId(Out<Common::UUID> out_mii_author_id);
    Result GetAutoUpdateEnableFlag(Out<bool> out_auto_update_enable_flag);
    Result SetAutoUpdateEnableFlag(bool auto_update_enable_flag);
    Result GetBatteryPercentageFlag(Out<bool> out_battery_percentage_flag);
    Result SetBatteryPercentageFlag(bool battery_percentage_flag);
    Result SetExternalSteadyClockInternalOffset(s64 offset);
    Result GetExternalSteadyClockInternalOffset(Out<s64> out_offset);
    Result GetPushNotificationActivityModeOnSleep(
        Out<s32> out_push_notification_activity_mode_on_sleep);
    Result SetPushNotificationActivityModeOnSleep(s32 push_notification_activity_mode_on_sleep);
    Result GetErrorReportSharePermission(
        Out<ErrorReportSharePermission> out_error_report_share_permission);
    Result SetErrorReportSharePermission(ErrorReportSharePermission error_report_share_permission);
    Result GetAppletLaunchFlags(Out<u32> out_applet_launch_flag);
    Result SetAppletLaunchFlags(u32 applet_launch_flag);
    Result GetKeyboardLayout(Out<KeyboardLayout> out_keyboard_layout);
    Result SetKeyboardLayout(KeyboardLayout keyboard_layout);
    Result GetDeviceTimeZoneLocationUpdatedTime(
        Out<Service::PSC::Time::SteadyClockTimePoint> out_time_point);
    Result SetDeviceTimeZoneLocationUpdatedTime(
        const Service::PSC::Time::SteadyClockTimePoint& time_point);
    Result GetUserSystemClockAutomaticCorrectionUpdatedTime(
        Out<Service::PSC::Time::SteadyClockTimePoint> out_time_point);
    Result SetUserSystemClockAutomaticCorrectionUpdatedTime(
        const Service::PSC::Time::SteadyClockTimePoint& out_time_point);
    Result GetChineseTraditionalInputMethod(
        Out<ChineseTraditionalInputMethod> out_chinese_traditional_input_method);
    Result GetHomeMenuScheme(Out<HomeMenuScheme> out_home_menu_scheme);
    Result GetHomeMenuSchemeModel(Out<u32> out_home_menu_scheme_model);
    Result GetTouchScreenMode(Out<TouchScreenMode> out_touch_screen_mode);
    Result GetPlatformRegion(Out<PlatformRegion> out_platform_region);
    Result SetPlatformRegion(PlatformRegion platform_region);
    Result SetTouchScreenMode(TouchScreenMode touch_screen_mode);
    Result GetFieldTestingFlag(Out<bool> out_field_testing_flag);
    Result GetPanelCrcMode(Out<s32> out_panel_crc_mode);
    Result SetPanelCrcMode(s32 panel_crc_mode);
    Result GetHttpAuthConfigs(Out<s32> out_count, OutBuffer<BufferAttr_HipcMapAlias> out_configs);
    Result GetAccountUserSettings(
        Out<u32> out_count,
        OutLargeData<AccountUserSettings, BufferAttr_HipcMapAlias> out_settings);
    Result GetDefaultAccountUserSettings(Out<AccountUserSettings> out_settings);

private:
    bool LoadSettingsFile(std::filesystem::path& path, auto&& default_func);
    bool StoreSettingsFile(std::filesystem::path& path, auto& settings);
    void SetupSettings();
    void StoreSettings();
    void SetSaveNeeded();

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, C<&ISystemSettingsServer::SetLanguageCode>, "SetLanguageCode"},
        FunctionInfo{1, nullptr, "SetNetworkSettings"},
        FunctionInfo{2, nullptr, "GetNetworkSettings"},
        FunctionInfo{3, C<&ISystemSettingsServer::GetFirmwareVersion>, "GetFirmwareVersion"},
        FunctionInfo{4, C<&ISystemSettingsServer::GetFirmwareVersion2>, "GetFirmwareVersion2"},
        FunctionInfo{5, nullptr, "GetFirmwareVersionDigest"},
        FunctionInfo{7, C<&ISystemSettingsServer::GetLockScreenFlag>, "GetLockScreenFlag"},
        FunctionInfo{8, C<&ISystemSettingsServer::SetLockScreenFlag>, "SetLockScreenFlag"},
        FunctionInfo{9, nullptr, "GetBacklightSettings"},
        FunctionInfo{10, nullptr, "SetBacklightSettings"},
        FunctionInfo{11, nullptr, "SetBluetoothDevicesSettings"},
        FunctionInfo{12, nullptr, "GetBluetoothDevicesSettings"},
        FunctionInfo{13, C<&ISystemSettingsServer::GetExternalSteadyClockSourceId>, "GetExternalSteadyClockSourceId"},
        FunctionInfo{14, C<&ISystemSettingsServer::SetExternalSteadyClockSourceId>, "SetExternalSteadyClockSourceId"},
        FunctionInfo{15, C<&ISystemSettingsServer::GetUserSystemClockContext>, "GetUserSystemClockContext"},
        FunctionInfo{16, C<&ISystemSettingsServer::SetUserSystemClockContext>, "SetUserSystemClockContext"},
        FunctionInfo{17, C<&ISystemSettingsServer::GetAccountSettings>, "GetAccountSettings"},
        FunctionInfo{18, C<&ISystemSettingsServer::SetAccountSettings>, "SetAccountSettings"},
        FunctionInfo{19, nullptr, "GetAudioVolume"},
        FunctionInfo{20, nullptr, "SetAudioVolume"},
        FunctionInfo{21, C<&ISystemSettingsServer::GetEulaVersions>, "GetEulaVersions"},
        FunctionInfo{22, C<&ISystemSettingsServer::SetEulaVersions>, "SetEulaVersions"},
        FunctionInfo{23, C<&ISystemSettingsServer::GetColorSetId>, "GetColorSetId"},
        FunctionInfo{24, C<&ISystemSettingsServer::SetColorSetId>, "SetColorSetId"},
        FunctionInfo{25, C<&ISystemSettingsServer::GetConsoleInformationUploadFlag>, "GetConsoleInformationUploadFlag"},
        FunctionInfo{26, C<&ISystemSettingsServer::SetConsoleInformationUploadFlag>, "SetConsoleInformationUploadFlag"},
        FunctionInfo{27, C<&ISystemSettingsServer::GetAutomaticApplicationDownloadFlag>, "GetAutomaticApplicationDownloadFlag"},
        FunctionInfo{28, C<&ISystemSettingsServer::SetAutomaticApplicationDownloadFlag>, "SetAutomaticApplicationDownloadFlag"},
        FunctionInfo{29, C<&ISystemSettingsServer::GetNotificationSettings>, "GetNotificationSettings"},
        FunctionInfo{30, C<&ISystemSettingsServer::SetNotificationSettings>, "SetNotificationSettings"},
        FunctionInfo{31, C<&ISystemSettingsServer::GetAccountNotificationSettings>, "GetAccountNotificationSettings"},
        FunctionInfo{32, C<&ISystemSettingsServer::SetAccountNotificationSettings>, "SetAccountNotificationSettings"},
        FunctionInfo{35, C<&ISystemSettingsServer::GetVibrationMasterVolume>, "GetVibrationMasterVolume"},
        FunctionInfo{36, C<&ISystemSettingsServer::SetVibrationMasterVolume>, "SetVibrationMasterVolume"},
        FunctionInfo{37, C<&ISystemSettingsServer::GetSettingsItemValueSize>, "GetSettingsItemValueSize"},
        FunctionInfo{38, C<&ISystemSettingsServer::GetSettingsItemValue>, "GetSettingsItemValue"},
        FunctionInfo{39, C<&ISystemSettingsServer::GetTvSettings>, "GetTvSettings"},
        FunctionInfo{40, C<&ISystemSettingsServer::SetTvSettings>, "SetTvSettings"},
        FunctionInfo{41, nullptr, "GetEdid"},
        FunctionInfo{42, nullptr, "SetEdid"},
        FunctionInfo{43, C<&ISystemSettingsServer::GetAudioOutputMode>, "GetAudioOutputMode"},
        FunctionInfo{44, C<&ISystemSettingsServer::SetAudioOutputMode>, "SetAudioOutputMode"},
        FunctionInfo{45, C<&ISystemSettingsServer::GetSpeakerAutoMuteFlag>, "GetSpeakerAutoMuteFlag"},
        FunctionInfo{46, C<&ISystemSettingsServer::SetSpeakerAutoMuteFlag>, "SetSpeakerAutoMuteFlag"},
        FunctionInfo{47, C<&ISystemSettingsServer::GetQuestFlag>, "GetQuestFlag"},
        FunctionInfo{48, C<&ISystemSettingsServer::SetQuestFlag>, "SetQuestFlag"},
        FunctionInfo{49, nullptr, "GetDataDeletionSettings"},
        FunctionInfo{50, nullptr, "SetDataDeletionSettings"},
        FunctionInfo{51, nullptr, "GetInitialSystemAppletProgramId"},
        FunctionInfo{52, nullptr, "GetOverlayDispProgramId"},
        FunctionInfo{53, C<&ISystemSettingsServer::GetDeviceTimeZoneLocationName>, "GetDeviceTimeZoneLocationName"},
        FunctionInfo{54, C<&ISystemSettingsServer::SetDeviceTimeZoneLocationName>, "SetDeviceTimeZoneLocationName"},
        FunctionInfo{55, nullptr, "GetWirelessCertificationFileSize"},
        FunctionInfo{56, nullptr, "GetWirelessCertificationFile"},
        FunctionInfo{57, C<&ISystemSettingsServer::SetRegionCode>, "SetRegionCode"},
        FunctionInfo{58, C<&ISystemSettingsServer::GetNetworkSystemClockContext>, "GetNetworkSystemClockContext"},
        FunctionInfo{59, C<&ISystemSettingsServer::SetNetworkSystemClockContext>, "SetNetworkSystemClockContext"},
        FunctionInfo{60, C<&ISystemSettingsServer::IsUserSystemClockAutomaticCorrectionEnabled>, "IsUserSystemClockAutomaticCorrectionEnabled"},
        FunctionInfo{61, C<&ISystemSettingsServer::SetUserSystemClockAutomaticCorrectionEnabled>, "SetUserSystemClockAutomaticCorrectionEnabled"},
        FunctionInfo{62, C<&ISystemSettingsServer::GetDebugModeFlag>, "GetDebugModeFlag"},
        FunctionInfo{63, C<&ISystemSettingsServer::GetPrimaryAlbumStorage>, "GetPrimaryAlbumStorage"},
        FunctionInfo{64, C<&ISystemSettingsServer::SetPrimaryAlbumStorage>, "SetPrimaryAlbumStorage"},
        FunctionInfo{65, C<&ISystemSettingsServer::GetUsb30EnableFlag>, "GetUsb30EnableFlag"},
        FunctionInfo{66, C<&ISystemSettingsServer::SetUsb30EnableFlag>, "SetUsb30EnableFlag"},
        FunctionInfo{67, C<&ISystemSettingsServer::GetBatteryLot>, "GetBatteryLot"},
        FunctionInfo{68, C<&ISystemSettingsServer::GetSerialNumber>, "GetSerialNumber"},
        FunctionInfo{69, C<&ISystemSettingsServer::GetNfcEnableFlag>, "GetNfcEnableFlag"},
        FunctionInfo{70, C<&ISystemSettingsServer::SetNfcEnableFlag>, "SetNfcEnableFlag"},
        FunctionInfo{71, C<&ISystemSettingsServer::GetSleepSettings>, "GetSleepSettings"},
        FunctionInfo{72, C<&ISystemSettingsServer::SetSleepSettings>, "SetSleepSettings"},
        FunctionInfo{73, C<&ISystemSettingsServer::GetWirelessLanEnableFlag>, "GetWirelessLanEnableFlag"},
        FunctionInfo{74, C<&ISystemSettingsServer::SetWirelessLanEnableFlag>, "SetWirelessLanEnableFlag"},
        FunctionInfo{75, C<&ISystemSettingsServer::GetInitialLaunchSettings>, "GetInitialLaunchSettings"},
        FunctionInfo{76, C<&ISystemSettingsServer::SetInitialLaunchSettings>, "SetInitialLaunchSettings"},
        FunctionInfo{77, C<&ISystemSettingsServer::GetDeviceNickName>, "GetDeviceNickName"},
        FunctionInfo{78, C<&ISystemSettingsServer::SetDeviceNickName>, "SetDeviceNickName"},
        FunctionInfo{79, C<&ISystemSettingsServer::GetProductModel>, "GetProductModel"},
        FunctionInfo{80, nullptr, "GetLdnChannel"},
        FunctionInfo{81, nullptr, "SetLdnChannel"},
        FunctionInfo{82, nullptr, "AcquireTelemetryDirtyFlagEventHandle"},
        FunctionInfo{83, nullptr, "GetTelemetryDirtyFlags"},
        FunctionInfo{84, nullptr, "GetPtmBatteryLot"},
        FunctionInfo{85, nullptr, "SetPtmBatteryLot"},
        FunctionInfo{86, nullptr, "GetPtmFuelGaugeParameter"},
        FunctionInfo{87, nullptr, "SetPtmFuelGaugeParameter"},
        FunctionInfo{88, C<&ISystemSettingsServer::GetBluetoothEnableFlag>, "GetBluetoothEnableFlag"},
        FunctionInfo{89, C<&ISystemSettingsServer::SetBluetoothEnableFlag>, "SetBluetoothEnableFlag"},
        FunctionInfo{90, C<&ISystemSettingsServer::GetMiiAuthorId>, "GetMiiAuthorId"},
        FunctionInfo{91, nullptr, "SetShutdownRtcValue"},
        FunctionInfo{92, nullptr, "GetShutdownRtcValue"},
        FunctionInfo{93, nullptr, "AcquireFatalDirtyFlagEventHandle"},
        FunctionInfo{94, nullptr, "GetFatalDirtyFlags"},
        FunctionInfo{95, C<&ISystemSettingsServer::GetAutoUpdateEnableFlag>, "GetAutoUpdateEnableFlag"},
        FunctionInfo{96, C<&ISystemSettingsServer::SetAutoUpdateEnableFlag>, "SetAutoUpdateEnableFlag"},
        FunctionInfo{97, nullptr, "GetNxControllerSettings"},
        FunctionInfo{98, nullptr, "SetNxControllerSettings"},
        FunctionInfo{99, C<&ISystemSettingsServer::GetBatteryPercentageFlag>, "GetBatteryPercentageFlag"},
        FunctionInfo{100, C<&ISystemSettingsServer::SetBatteryPercentageFlag>, "SetBatteryPercentageFlag"},
        FunctionInfo{101, nullptr, "GetExternalRtcResetFlag"},
        FunctionInfo{102, nullptr, "SetExternalRtcResetFlag"},
        FunctionInfo{103, nullptr, "GetUsbFullKeyEnableFlag"},
        FunctionInfo{104, nullptr, "SetUsbFullKeyEnableFlag"},
        FunctionInfo{105, C<&ISystemSettingsServer::SetExternalSteadyClockInternalOffset>, "SetExternalSteadyClockInternalOffset"},
        FunctionInfo{106, C<&ISystemSettingsServer::GetExternalSteadyClockInternalOffset>, "GetExternalSteadyClockInternalOffset"},
        FunctionInfo{107, nullptr, "GetBacklightSettingsEx"},
        FunctionInfo{108, nullptr, "SetBacklightSettingsEx"},
        FunctionInfo{109, nullptr, "GetHeadphoneVolumeWarningCount"},
        FunctionInfo{110, nullptr, "SetHeadphoneVolumeWarningCount"},
        FunctionInfo{111, nullptr, "GetBluetoothAfhEnableFlag"},
        FunctionInfo{112, nullptr, "SetBluetoothAfhEnableFlag"},
        FunctionInfo{113, nullptr, "GetBluetoothBoostEnableFlag"},
        FunctionInfo{114, nullptr, "SetBluetoothBoostEnableFlag"},
        FunctionInfo{115, nullptr, "GetInRepairProcessEnableFlag"},
        FunctionInfo{116, nullptr, "SetInRepairProcessEnableFlag"},
        FunctionInfo{117, nullptr, "GetHeadphoneVolumeUpdateFlag"},
        FunctionInfo{118, nullptr, "SetHeadphoneVolumeUpdateFlag"},
        FunctionInfo{119, nullptr, "NeedsToUpdateHeadphoneVolume"},
        FunctionInfo{120, C<&ISystemSettingsServer::GetPushNotificationActivityModeOnSleep>, "GetPushNotificationActivityModeOnSleep"},
        FunctionInfo{121, C<&ISystemSettingsServer::SetPushNotificationActivityModeOnSleep>, "SetPushNotificationActivityModeOnSleep"},
        FunctionInfo{122, nullptr, "GetServiceDiscoveryControlSettings"},
        FunctionInfo{123, nullptr, "SetServiceDiscoveryControlSettings"},
        FunctionInfo{124, C<&ISystemSettingsServer::GetErrorReportSharePermission>, "GetErrorReportSharePermission"},
        FunctionInfo{125, C<&ISystemSettingsServer::SetErrorReportSharePermission>, "SetErrorReportSharePermission"},
        FunctionInfo{126, C<&ISystemSettingsServer::GetAppletLaunchFlags>, "GetAppletLaunchFlags"},
        FunctionInfo{127, C<&ISystemSettingsServer::SetAppletLaunchFlags>, "SetAppletLaunchFlags"},
        FunctionInfo{128, nullptr, "GetConsoleSixAxisSensorAccelerationBias"},
        FunctionInfo{129, nullptr, "SetConsoleSixAxisSensorAccelerationBias"},
        FunctionInfo{130, nullptr, "GetConsoleSixAxisSensorAngularVelocityBias"},
        FunctionInfo{131, nullptr, "SetConsoleSixAxisSensorAngularVelocityBias"},
        FunctionInfo{132, nullptr, "GetConsoleSixAxisSensorAccelerationGain"},
        FunctionInfo{133, nullptr, "SetConsoleSixAxisSensorAccelerationGain"},
        FunctionInfo{134, nullptr, "GetConsoleSixAxisSensorAngularVelocityGain"},
        FunctionInfo{135, nullptr, "SetConsoleSixAxisSensorAngularVelocityGain"},
        FunctionInfo{136, C<&ISystemSettingsServer::GetKeyboardLayout>, "GetKeyboardLayout"},
        FunctionInfo{137, C<&ISystemSettingsServer::SetKeyboardLayout>, "SetKeyboardLayout"},
        FunctionInfo{138, nullptr, "GetWebInspectorFlag"},
        FunctionInfo{139, nullptr, "GetAllowedSslHosts"},
        FunctionInfo{140, nullptr, "GetHostFsMountPoint"},
        FunctionInfo{141, nullptr, "GetRequiresRunRepairTimeReviser"},
        FunctionInfo{142, nullptr, "SetRequiresRunRepairTimeReviser"},
        FunctionInfo{143, nullptr, "SetBlePairingSettings"},
        FunctionInfo{144, nullptr, "GetBlePairingSettings"},
        FunctionInfo{145, nullptr, "GetConsoleSixAxisSensorAngularVelocityTimeBias"},
        FunctionInfo{146, nullptr, "SetConsoleSixAxisSensorAngularVelocityTimeBias"},
        FunctionInfo{147, nullptr, "GetConsoleSixAxisSensorAngularAcceleration"},
        FunctionInfo{148, nullptr, "SetConsoleSixAxisSensorAngularAcceleration"},
        FunctionInfo{149, C<&ISystemSettingsServer::GetRebootlessSystemUpdateVersion>, "GetRebootlessSystemUpdateVersion"},
        FunctionInfo{150, C<&ISystemSettingsServer::GetDeviceTimeZoneLocationUpdatedTime>, "GetDeviceTimeZoneLocationUpdatedTime"},
        FunctionInfo{151, C<&ISystemSettingsServer::SetDeviceTimeZoneLocationUpdatedTime>, "SetDeviceTimeZoneLocationUpdatedTime"},
        FunctionInfo{152, C<&ISystemSettingsServer::GetUserSystemClockAutomaticCorrectionUpdatedTime>, "GetUserSystemClockAutomaticCorrectionUpdatedTime"},
        FunctionInfo{153, C<&ISystemSettingsServer::SetUserSystemClockAutomaticCorrectionUpdatedTime>, "SetUserSystemClockAutomaticCorrectionUpdatedTime"},
        FunctionInfo{154, nullptr, "GetAccountOnlineStorageSettings"},
        FunctionInfo{155, nullptr, "SetAccountOnlineStorageSettings"},
        FunctionInfo{156, nullptr, "GetPctlReadyFlag"},
        FunctionInfo{157, nullptr, "SetPctlReadyFlag"},
        FunctionInfo{158, nullptr, "GetAnalogStickUserCalibrationL"},
        FunctionInfo{159, nullptr, "SetAnalogStickUserCalibrationL"},
        FunctionInfo{160, nullptr, "GetAnalogStickUserCalibrationR"},
        FunctionInfo{161, nullptr, "SetAnalogStickUserCalibrationR"},
        FunctionInfo{162, nullptr, "GetPtmBatteryVersion"},
        FunctionInfo{163, nullptr, "SetPtmBatteryVersion"},
        FunctionInfo{164, nullptr, "GetUsb30HostEnableFlag"},
        FunctionInfo{165, nullptr, "SetUsb30HostEnableFlag"},
        FunctionInfo{166, nullptr, "GetUsb30DeviceEnableFlag"},
        FunctionInfo{167, nullptr, "SetUsb30DeviceEnableFlag"},
        FunctionInfo{168, nullptr, "GetThemeId"},
        FunctionInfo{169, nullptr, "SetThemeId"},
        FunctionInfo{170, C<&ISystemSettingsServer::GetChineseTraditionalInputMethod>, "GetChineseTraditionalInputMethod"},
        FunctionInfo{171, nullptr, "SetChineseTraditionalInputMethod"},
        FunctionInfo{172, nullptr, "GetPtmCycleCountReliability"},
        FunctionInfo{173, nullptr, "SetPtmCycleCountReliability"},
        FunctionInfo{174, C<&ISystemSettingsServer::GetHomeMenuScheme>, "GetHomeMenuScheme"},
        FunctionInfo{175, nullptr, "GetThemeSettings"},
        FunctionInfo{176, nullptr, "SetThemeSettings"},
        FunctionInfo{177, nullptr, "GetThemeKey"},
        FunctionInfo{178, nullptr, "SetThemeKey"},
        FunctionInfo{179, nullptr, "GetZoomFlag"},
        FunctionInfo{180, nullptr, "SetZoomFlag"},
        FunctionInfo{181, nullptr, "GetT"},
        FunctionInfo{182, nullptr, "SetT"},
        FunctionInfo{183, C<&ISystemSettingsServer::GetPlatformRegion>, "GetPlatformRegion"},
        FunctionInfo{184, C<&ISystemSettingsServer::SetPlatformRegion>, "SetPlatformRegion"},
        FunctionInfo{185, C<&ISystemSettingsServer::GetHomeMenuSchemeModel>, "GetHomeMenuSchemeModel"},
        FunctionInfo{186, nullptr, "GetMemoryUsageRateFlag"},
        FunctionInfo{187, C<&ISystemSettingsServer::GetTouchScreenMode>, "GetTouchScreenMode"},
        FunctionInfo{188, C<&ISystemSettingsServer::SetTouchScreenMode>, "SetTouchScreenMode"},
        FunctionInfo{189, nullptr, "GetButtonConfigSettingsFull"},
        FunctionInfo{190, nullptr, "SetButtonConfigSettingsFull"},
        FunctionInfo{191, nullptr, "GetButtonConfigSettingsEmbedded"},
        FunctionInfo{192, nullptr, "SetButtonConfigSettingsEmbedded"},
        FunctionInfo{193, nullptr, "GetButtonConfigSettingsLeft"},
        FunctionInfo{194, nullptr, "SetButtonConfigSettingsLeft"},
        FunctionInfo{195, nullptr, "GetButtonConfigSettingsRight"},
        FunctionInfo{196, nullptr, "SetButtonConfigSettingsRight"},
        FunctionInfo{197, nullptr, "GetButtonConfigRegisteredSettingsEmbedded"},
        FunctionInfo{198, nullptr, "SetButtonConfigRegisteredSettingsEmbedded"},
        FunctionInfo{199, nullptr, "GetButtonConfigRegisteredSettings"},
        FunctionInfo{200, nullptr, "SetButtonConfigRegisteredSettings"},
        FunctionInfo{201, C<&ISystemSettingsServer::GetFieldTestingFlag>, "GetFieldTestingFlag"},
        FunctionInfo{202, nullptr, "SetFieldTestingFlag"},
        FunctionInfo{203, C<&ISystemSettingsServer::GetPanelCrcMode>, "GetPanelCrcMode"},
        FunctionInfo{204, C<&ISystemSettingsServer::SetPanelCrcMode>, "SetPanelCrcMode"},
        FunctionInfo{205, nullptr, "GetNxControllerSettingsEx"},
        FunctionInfo{206, nullptr, "SetNxControllerSettingsEx"},
        FunctionInfo{207, nullptr, "GetHearingProtectionSafeguardFlag"},
        FunctionInfo{208, nullptr, "SetHearingProtectionSafeguardFlag"},
        FunctionInfo{209, nullptr, "GetHearingProtectionSafeguardRemainingTime"},
        FunctionInfo{210, nullptr, "SetHearingProtectionSafeguardRemainingTime"},
        FunctionInfo{221, nullptr, "GetForceMonauralOutputFlag"}, //17.0.0+
        FunctionInfo{222, nullptr, "SetForceMonauralOutputFlag"}, //17.0.0+
        FunctionInfo{251, nullptr, "GetAccountIdentificationSettings"}, //18.0.0+
        FunctionInfo{252, nullptr, "SetAccountIdentificationSettings"}, //18.0.0+
        FunctionInfo{263, nullptr, "AcquireVphymDirtyFlagEventHandle"}, //20.0.0+
        FunctionInfo{264, nullptr, "GetVphymDirtyFlags"}, //20.0.0+
        FunctionInfo{282, nullptr, "ConvertToProductModel"}, //20.0.0+
        FunctionInfo{283, nullptr, "ConvertToProductModelName"}, //20.0.0+
        FunctionInfo{289, nullptr, "GetDefaultAccountIdentificationFlagSet"}, //20.0.0+
        FunctionInfo{300, nullptr, "AcquirePushNotificationDirtyFlagEventHandle"}, //20.0.0+
        FunctionInfo{301, nullptr, "GetPushNotificationDirtyFlags"}, //20.0.0+
        FunctionInfo{306, nullptr, "GetPinCodeReregistrationGuideAccounts"}, //20.0.0+
        FunctionInfo{307, nullptr, "SetPinCodeReregistrationGuideAccounts"}, //20.0.0+
        FunctionInfo{315, C<&ISystemSettingsServer::GetHttpAuthConfigs>, "GetHttpAuthConfigs"}, //21.0.0+
        FunctionInfo{319, C<&ISystemSettingsServer::GetAccountUserSettings>, "GetAccountUserSettings"}, //21.0.0+
        FunctionInfo{320, nullptr, "SetAccountUserSettings"}, //21.0.0+
        FunctionInfo{321, C<&ISystemSettingsServer::GetDefaultAccountUserSettings>, "GetDefaultAccountUserSettings"} //21.0.0+
    );
    Core::System& m_system;
    SystemSettings m_system_settings{};
    PrivateSettings m_private_settings{};
    DeviceSettings m_device_settings{};
    ApplnSettings m_appln_settings{};
    std::mutex m_save_needed_mutex;
    bool m_save_needed{false};
};

} // namespace Service::Set
