// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>

#include "common/uuid.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::OLSC {

class IDaemonController;
class IRemoteStorageController;
class ITransferTaskListController;

struct DataTransferPolicy {
    u8 upload_policy;
    u8 download_policy;
};

struct TransferTaskErrorInfo {
    Common::UUID uid;
    u64 application_id;
    u8 unknown_0x18;
    std::array<u8, 7> reserved_0x19;
    u64 unknown_0x20;
    u32 error_code;
    u32 reserved_0x2C;
};
static_assert(sizeof(TransferTaskErrorInfo) == 0x30, "TransferTaskErrorInfo has incorrect size.");

class IOlscServiceForSystemService final : public ServiceFramework<IOlscServiceForSystemService> {
public:
    explicit IOlscServiceForSystemService(Core::System& system_);
    ~IOlscServiceForSystemService() override;

private:
    Result GetTransferTaskListController(
        Out<SharedPointer<ITransferTaskListController>> out_interface);
    Result GetRemoteStorageController(Out<SharedPointer<IRemoteStorageController>> out_interface);
    Result GetDaemonController(Out<SharedPointer<IDaemonController>> out_interface);
    Result GetDataTransferPolicy(Out<DataTransferPolicy> out_policy, u64 application_id);
    Result GetTransferTaskErrorInfo(Out<TransferTaskErrorInfo> out_info, Common::UUID uid, u64 application_id);
    Result GetOlscServiceForSystemService(Out<SharedPointer<IOlscServiceForSystemService>> out_interface);

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, D<&IOlscServiceForSystemService::GetTransferTaskListController>, "GetTransferTaskListController"},
        FunctionInfo{1, D<&IOlscServiceForSystemService::GetRemoteStorageController>, "GetRemoteStorageController"},
        FunctionInfo{2, D<&IOlscServiceForSystemService::GetDaemonController>, "GetDaemonController"},
        FunctionInfo{10, nullptr, "PrepareDeleteUserProperty"},
        FunctionInfo{11, nullptr, "DeleteUserSaveDataProperty"},
        FunctionInfo{12, nullptr, "InvalidateMountCache"},
        FunctionInfo{13, nullptr, "DeleteDeviceSaveDataProperty"},
        FunctionInfo{100, nullptr, "ListLastTransferTaskErrorInfo"},
        FunctionInfo{101, nullptr, "GetLastErrorInfoCount"},
        FunctionInfo{102, nullptr, "RemoveLastErrorInfoOld"},
        FunctionInfo{103, nullptr, "GetLastErrorInfo"},
        FunctionInfo{104, nullptr, "GetLastErrorEventHolder"},
        FunctionInfo{105, D<&IOlscServiceForSystemService::GetTransferTaskErrorInfo>, "GetTransferTaskErrorInfo"},
        FunctionInfo{200, D<&IOlscServiceForSystemService::GetDataTransferPolicy>, "GetDataTransferPolicy"},
        FunctionInfo{201, nullptr, "DeleteDataTransferPolicyCache"},
        FunctionInfo{202, nullptr, "Unknown202"},
        FunctionInfo{203, nullptr, "RequestUpdateDataTransferPolicyCacheAsync"},
        FunctionInfo{204, nullptr, "ClearDataTransferPolicyCache"},
        FunctionInfo{205, nullptr, "RequestGetDataTransferPolicyAsync"},
        FunctionInfo{206, nullptr, "Unknown206"}, //21.0.0+
        FunctionInfo{300, nullptr, "GetUserSaveDataProperty"},
        FunctionInfo{301, nullptr, "SetUserSaveDataProperty"},
        FunctionInfo{302, nullptr, "Unknown302"}, //21.0.0+
        FunctionInfo{400, nullptr, "CleanupSaveDataBackupContextForSpecificApplications"},
        FunctionInfo{900, nullptr, "DeleteAllTransferTask"},
        FunctionInfo{902, nullptr, "DeleteAllSeriesInfo"},
        FunctionInfo{903, nullptr, "DeleteAllSdaInfoCache"},
        FunctionInfo{904, nullptr, "DeleteAllApplicationSetting"},
        FunctionInfo{905, nullptr, "DeleteAllTransferTaskErrorInfo"},
        FunctionInfo{906, nullptr, "RegisterTransferTaskErrorInfo"},
        FunctionInfo{907, nullptr, "AddSaveDataArchiveInfoCache"},
        FunctionInfo{908, nullptr, "DeleteSeriesInfo"},
        FunctionInfo{909, nullptr, "GetSeriesInfo"},
        FunctionInfo{910, nullptr, "RemoveTransferTaskErrorInfo"},
        FunctionInfo{911, nullptr, "DeleteAllSeriesInfoForSaveDataBackup"},
        FunctionInfo{912, nullptr, "DeleteSeriesInfoForSaveDataBackup"},
        FunctionInfo{913, nullptr, "GetSeriesInfoForSaveDataBackup"},
        FunctionInfo{914, nullptr, "Unknown914"}, //20.2.0+
        FunctionInfo{1000, nullptr, "UpdateIssueOld"},
        FunctionInfo{1010, nullptr, "Unknown1010"},
        FunctionInfo{1011, nullptr, "Unknown1011"},
        FunctionInfo{1012, nullptr, "Unknown1012"},
        FunctionInfo{1013, nullptr, "Unkown1013"},
        FunctionInfo{1014, nullptr, "Unknown1014"},
        FunctionInfo{1020, nullptr, "Unknown1020"},
        FunctionInfo{1021, nullptr, "Unknown1021"},
        FunctionInfo{1022, nullptr, "Unknown1022"},
        FunctionInfo{1023, nullptr, "Unknown1023"},
        FunctionInfo{1024, nullptr, "Unknown1024"},
        FunctionInfo{1100, nullptr, "RepairUpdateIssueInfoCacheAync"},
        FunctionInfo{1110, nullptr, "RepairGetIssueInfo"},
        FunctionInfo{1111, nullptr, "RepairListIssueInfo"},
        FunctionInfo{1112, nullptr, "RepairListOperationPermissionInfo"},
        FunctionInfo{1113, nullptr, "RepairListDataInfoForRepairedSaveDataDownload"},
        FunctionInfo{1114, nullptr, "RepairListDataInfoForOriginalSaveDataDownload"},
        FunctionInfo{1120, nullptr, "RepairUploadSaveDataAsync"},
        FunctionInfo{1121, nullptr, "RepairUploadSaveDataAsync1"},
        FunctionInfo{1122, nullptr, "RepairDownloadRepairedSaveDataAsync"},
        FunctionInfo{1123, nullptr, "RepairDownloadOriginalSaveDataAsync"},
        FunctionInfo{1124, nullptr, "RepairGetOperationProgressInfo"},
        FunctionInfo{10000, D<&IOlscServiceForSystemService::GetOlscServiceForSystemService>, "GetOlscServiceForSystemService"}
    );
};

} // namespace Service::OLSC
