// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/olsc/remote_storage_controller.h"

namespace Service::OLSC {

    ServiceFrameworkBase::FunctionInfoBase const* IRemoteStorageController::FindRequest(u32 key) {
        static constexpr auto functions = CreateStaticMap(
            FunctionInfo{0, nullptr, "GetSaveDataArchiveInfoBySaveDataId"},
            FunctionInfo{1, nullptr, "GetSaveDataArchiveInfoByApplicationId"},
            FunctionInfo{3, nullptr, "GetSaveDataArchiveCount"},
            FunctionInfo{6, nullptr, "CleanupSaveDataArchives"},
            FunctionInfo{7, nullptr, "CreateSaveDataArchiveCacheUpdationTask"},
            FunctionInfo{8, nullptr, "CreateSaveDataArchiveCacheUpdationForSpecifiedApplicationTask"},
            FunctionInfo{9, nullptr, "Delete"},
            FunctionInfo{10, nullptr, "GetSeriesInfo"},
            FunctionInfo{11, nullptr, "CreateDeleteDataTask"},
            FunctionInfo{12, nullptr, "DeleteSeriesInfo"},
            FunctionInfo{13, nullptr, "CreateRegisterNotificationTokenTask"},
            FunctionInfo{14, D<&IRemoteStorageController::GetDataNewnessByApplicationId>, "GetDataNewnessByApplicationId"},
            FunctionInfo{15, nullptr, "RegisterUploadSaveDataTransferTaskForAutonomyRegistration"},
            FunctionInfo{16, nullptr, "CreateCleanupToDeleteSaveDataArchiveInfoTask"},
            FunctionInfo{17, nullptr, "ListDataInfo"},
            FunctionInfo{18, D<&IRemoteStorageController::GetDataInfo>, "GetDataInfoV1"},
            FunctionInfo{19, nullptr, "GetDataInfoCacheUpdateNativeHandleHolder"},
            FunctionInfo{20, nullptr, "CreateSaveDataArchiveInfoCacheForSaveDataBackupUpdationTask"},
            FunctionInfo{21, nullptr, "ListSecondarySaves"},
            FunctionInfo{22, D<&IRemoteStorageController::GetSecondarySave>, "GetSecondarySave"},
            FunctionInfo{23, nullptr, "TouchSecondarySave"},
            FunctionInfo{24, nullptr, "GetSecondarySaveDataInfo"},
            FunctionInfo{25, nullptr, "RegisterDownloadSaveDataTransferTaskForAutonomyRegistration"},
            FunctionInfo{26, nullptr, "Unknown26"}, //20.0.0+
            FunctionInfo{27, D<&IRemoteStorageController::GetDataInfo>, "GetDataInfoV2"}, //20.0.0+
            FunctionInfo{28, nullptr, "Unknown28"}, //20.0.0+
            FunctionInfo{29, nullptr, "Unknown29"}, //21.0.0+
            FunctionInfo{800, nullptr, "Unknown800"}, //20.0.0+
            FunctionInfo{900, nullptr, "SetLoadedDataMissing"},
            FunctionInfo{901, nullptr, "Unknown901"} //20.2.0+
        );
        return HandlerTableGenerateWithFind(key, functions);
    }

IRemoteStorageController::IRemoteStorageController(Core::System& system_)
    : ServiceFramework{system_, "IRemoteStorageController"} {
}

IRemoteStorageController::~IRemoteStorageController() = default;

Result IRemoteStorageController::GetSecondarySave(Out<bool> out_has_secondary_save,
                                                  Out<std::array<u64, 3>> out_unknown,
                                                  u64 application_id) {
    LOG_ERROR(Service_OLSC, "(STUBBED) called, application_id={:016X}", application_id);
    *out_has_secondary_save = false;
    *out_unknown = {};
    R_SUCCEED();
}

Result IRemoteStorageController::GetDataNewnessByApplicationId(Out<u8> out_newness,
                                                              u64 application_id) {
    LOG_WARNING(Service_OLSC, "(STUBBED) called, application_id={:016X}", application_id);
    *out_newness = 0;
    R_SUCCEED();
}

Result IRemoteStorageController::GetDataInfo(Out<std::array<u8, 0x38>> out_data, u64 application_id) {
    LOG_WARNING(Service_OLSC, "(STUBBED) called, application_id={:016X}", application_id);
    out_data->fill(0);
    R_SUCCEED();
}

} // namespace Service::OLSC
