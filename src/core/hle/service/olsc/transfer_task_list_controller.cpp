// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/olsc/native_handle_holder.h"
#include "core/hle/service/olsc/stopper_object.h"
#include "core/hle/service/olsc/transfer_task_list_controller.h"

namespace Service::OLSC {

    ServiceFrameworkBase::FunctionInfoBase const* ITransferTaskListController::FindRequest(u32 key) {
        static constexpr auto functions = CreateStaticMap(
            FunctionInfo{0, nullptr, "GetTransferTaskCountForOcean"},
            FunctionInfo{1, nullptr, "GetTransferTaskInfoForOcean"},
            FunctionInfo{2, nullptr, "ListTransferTaskInfoForOcean"},
            FunctionInfo{3, nullptr, "DeleteTransferTaskForOcean"},
            FunctionInfo{4, nullptr, "RaiseTransferTaskPriorityForOcean"},
            FunctionInfo{5, D<&ITransferTaskListController::GetTransferTaskEndEventNativeHandleHolder>, "GetTransferTaskEndEventNativeHandleHolder"},
            FunctionInfo{6, nullptr, "GetTransferTaskProgressForOcean"},
            FunctionInfo{7, nullptr, "GetTransferTaskLastResultForOcean"},
            FunctionInfo{8, D<&ITransferTaskListController::StopNextTransferTaskExecution>, "StopNextTransferTaskExecution"},
            FunctionInfo{9, D<&ITransferTaskListController::GetTransferTaskStartEventNativeHandleHolder>, "GetTransferTaskStartEventNativeHandleHolder"},
            FunctionInfo{10, nullptr, "SuspendTransferTaskForOcean"},
            FunctionInfo{11, nullptr, "GetCurrentTransferTaskInfoForOcean"},
            FunctionInfo{12, nullptr, "FindTransferTaskInfoForOcean"},
            FunctionInfo{13, nullptr, "CancelCurrentRepairTransferTask"},
            FunctionInfo{14, nullptr, "GetRepairTransferTaskProgress"},
            FunctionInfo{15, nullptr, "EnsureExecutableForRepairTransferTask"},
            FunctionInfo{16, D<&ITransferTaskListController::GetTransferTaskCount>, "GetTransferTaskCount"},
            FunctionInfo{17, nullptr, "GetTransferTaskInfo"},
            FunctionInfo{18, nullptr, "ListTransferTaskInfo"},
            FunctionInfo{19, nullptr, "DeleteTransferTask"},
            FunctionInfo{20, nullptr, "RaiseTransferTaskPriority"},
            FunctionInfo{21, D<&ITransferTaskListController::GetTransferTaskProgress>, "GetTransferTaskProgress"}, //10.1.0+
            FunctionInfo{22, nullptr, "GetTransferTaskLastResult"},
            FunctionInfo{23, nullptr, "SuspendTransferTask"},
            FunctionInfo{24, D<&ITransferTaskListController::GetCurrentTransferTaskInfo>, "GetCurrentTransferTaskInfo"},
            FunctionInfo{25, D<&ITransferTaskListController::FindTransferTaskInfo>, "FindTransferTaskInfo"},
            FunctionInfo{26, nullptr, "Unknown26"}, //20.1.0+
            FunctionInfo{27, nullptr, "Unknown27"}, //20.1.0+
            FunctionInfo{28, nullptr, "Unknown28"}, //20.1.0+
            FunctionInfo{29, nullptr, "Unknown29"}, //20.1.0+
            FunctionInfo{30, nullptr, "Unknown30"} //20.1.0+
        );
        return HandlerTableGenerateWithFind(key, functions);
    }

ITransferTaskListController::ITransferTaskListController(Core::System& system_)
    : ServiceFramework{system_, "ITransferTaskListController"} {
}

ITransferTaskListController::~ITransferTaskListController() = default;

Result ITransferTaskListController::GetTransferTaskEndEventNativeHandleHolder(
    Out<SharedPointer<INativeHandleHolder>> out_holder) {
    LOG_WARNING(Service_OLSC, "(STUBBED) called");
    *out_holder = std::make_shared<INativeHandleHolder>(system);
    R_SUCCEED();
}

Result ITransferTaskListController::StopNextTransferTaskExecution(
    Out<SharedPointer<IStopperObject>> out_stopper) {
    LOG_WARNING(Service_OLSC, "(STUBBED) called");
    *out_stopper = std::make_shared<IStopperObject>(system);
    R_SUCCEED();
}

Result ITransferTaskListController::GetTransferTaskStartEventNativeHandleHolder(
    Out<SharedPointer<INativeHandleHolder>> out_holder) {
    LOG_WARNING(Service_OLSC, "(STUBBED) called");
    *out_holder = std::make_shared<INativeHandleHolder>(system);
    R_SUCCEED();
}

Result ITransferTaskListController::GetCurrentTransferTaskInfo(Out<std::array<u8, 0x30>> out_info,
                                                              u8 unknown) {
    LOG_WARNING(Service_OLSC, "(STUBBED) called, unknown={:#x}", unknown);
    out_info->fill(0);
    R_SUCCEED();
}

Result ITransferTaskListController::GetTransferTaskProgress() {
    LOG_WARNING(Service_OLSC, "(STUBBED) called.");
    R_SUCCEED();
}

Result ITransferTaskListController::FindTransferTaskInfo(Out<std::array<u8, 0x30>> out_info,
                                                        InBuffer<BufferAttr_HipcAutoSelect> in) {
    LOG_WARNING(Service_OLSC, "(STUBBED) called, in_size={}", in.size());
    out_info->fill(0);
    R_SUCCEED();
}

Result ITransferTaskListController::GetTransferTaskCount(Out<u32> out_count, u8 unknown) {
    LOG_WARNING(Service_OLSC, "(STUBBED) called, unknown={:#x}", unknown);
    *out_count = 0;
    R_SUCCEED();
}

} // namespace Service::OLSC
