// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/olsc/olsc_service_for_application.h"

namespace Service::OLSC {

IOlscServiceForApplication::IOlscServiceForApplication(Core::System& system_)
    : ServiceFramework{system_, "olsc:u"} {
    // clang-format off
        FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
            FunctionInfo{0, D<&IOlscServiceForApplication::Initialize>, "Initialize"},
            FunctionInfo{10, nullptr, "VerifySaveDataBackupLicenseAsync"},
            FunctionInfo{13, D<&IOlscServiceForApplication::GetSaveDataBackupSetting>, "GetSaveDataBackupSetting"},
            FunctionInfo{14, D<&IOlscServiceForApplication::SetSaveDataBackupSettingEnabled>, "SetSaveDataBackupSettingEnabled"},
            FunctionInfo{15, nullptr, "SetCustomData"},
            FunctionInfo{16, nullptr, "DeleteSaveDataBackupSetting"},
            FunctionInfo{18, nullptr, "GetSaveDataBackupInfoCache"},
            FunctionInfo{19, nullptr, "UpdateSaveDataBackupInfoCacheAsync"},
            FunctionInfo{22, nullptr, "DeleteSaveDataBackupAsync"},
            FunctionInfo{25, nullptr, "ListDownloadableSaveDataBackupInfoAsync"},
            FunctionInfo{26, nullptr, "DownloadSaveDataBackupAsync"},
            FunctionInfo{27, nullptr, "UploadSaveDataBackupAsync"},
            FunctionInfo{9010, nullptr, "VerifySaveDataBackupLicenseAsyncForDebug"},
            FunctionInfo{9013, nullptr, "GetSaveDataBackupSettingForDebug"},
            FunctionInfo{9014, nullptr, "SetSaveDataBackupSettingEnabledForDebug"},
            FunctionInfo{9015, nullptr, "SetCustomDataForDebug"},
            FunctionInfo{9016, nullptr, "DeleteSaveDataBackupSettingForDebug"},
            FunctionInfo{9018, nullptr, "GetSaveDataBackupInfoCacheForDebug"},
            FunctionInfo{9019, nullptr, "UpdateSaveDataBackupInfoCacheAsyncForDebug"},
            FunctionInfo{9022, nullptr, "DeleteSaveDataBackupAsyncForDebug"},
            FunctionInfo{9025, nullptr, "ListDownloadableSaveDataBackupInfoAsyncForDebug"},
            FunctionInfo{9026, nullptr, "DownloadSaveDataBackupAsyncForDebug"}
        );
}

IOlscServiceForApplication::~IOlscServiceForApplication() = default;

Result IOlscServiceForApplication::Initialize(ClientProcessId process_id) {
    LOG_WARNING(Service_OLSC, "(STUBBED) called");
    initialized = true;
    R_SUCCEED();
}

Result IOlscServiceForApplication::GetSaveDataBackupSetting(Out<u8> out_save_data_backup_setting) {
    LOG_WARNING(Service_OLSC, "(STUBBED) called");
    // backup_setting is set to 0 since real value is unknown
    *out_save_data_backup_setting = 0;
    R_SUCCEED();
}

Result IOlscServiceForApplication::SetSaveDataBackupSettingEnabled(bool enabled,
                                                                   NS::Uid account_id) {
    LOG_WARNING(Service_OLSC, "(STUBBED) called, enabled={}, account_id={}", enabled,
                account_id.uuid.FormattedString());
    R_SUCCEED();
}

} // namespace Service::OLSC
