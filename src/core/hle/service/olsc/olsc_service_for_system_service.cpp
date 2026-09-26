// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/olsc/daemon_controller.h"
#include "core/hle/service/olsc/olsc_service_for_system_service.h"
#include "core/hle/service/olsc/remote_storage_controller.h"
#include "core/hle/service/olsc/transfer_task_list_controller.h"

namespace Service::OLSC {

IOlscServiceForSystemService::IOlscServiceForSystemService(Core::System& system_)
    : ServiceFramework{system_, "olsc:s"} {
}

IOlscServiceForSystemService::~IOlscServiceForSystemService() = default;

Result IOlscServiceForSystemService::GetTransferTaskListController(
    Out<SharedPointer<ITransferTaskListController>> out_interface) {
    LOG_INFO(Service_OLSC, "called");
    *out_interface = std::make_shared<ITransferTaskListController>(system);
    R_SUCCEED();
}

Result IOlscServiceForSystemService::GetRemoteStorageController(
    Out<SharedPointer<IRemoteStorageController>> out_interface) {
    LOG_INFO(Service_OLSC, "called");
    *out_interface = std::make_shared<IRemoteStorageController>(system);
    R_SUCCEED();
}

Result IOlscServiceForSystemService::GetDaemonController(
    Out<SharedPointer<IDaemonController>> out_interface) {
    LOG_INFO(Service_OLSC, "called");
    *out_interface = std::make_shared<IDaemonController>(system);
    R_SUCCEED();
}

Result IOlscServiceForSystemService::GetDataTransferPolicy(
    Out<DataTransferPolicy> out_policy, u64 application_id) {
    LOG_WARNING(Service_OLSC, "(STUBBED) called");
    DataTransferPolicy policy{};
    policy.upload_policy = 0;
    policy.download_policy = 0;
    *out_policy = policy;
    R_SUCCEED();
}

Result IOlscServiceForSystemService::GetTransferTaskErrorInfo(Out<TransferTaskErrorInfo> out_info,
                                                             Common::UUID uuid, u64 application_id) {
    LOG_WARNING(Service_OLSC, "(STUBBED) called, uuid={} application_id={:016X}",
                uuid.FormattedString(), application_id);

    TransferTaskErrorInfo info{};
    info.uid = uuid;
    info.application_id = application_id;
    info.unknown_0x18 = 0;
    info.reserved_0x19.fill(0);
    info.unknown_0x20 = 0;
    info.error_code = 0;
    info.reserved_0x2C = 0;

    *out_info = info;
    R_SUCCEED();
}

Result IOlscServiceForSystemService::GetOlscServiceForSystemService(
    Out<SharedPointer<IOlscServiceForSystemService>> out_interface) {
    LOG_INFO(Service_OLSC, "called");
    *out_interface = std::static_pointer_cast<IOlscServiceForSystemService>(shared_from_this());
    R_SUCCEED();
}

} // namespace Service::OLSC
