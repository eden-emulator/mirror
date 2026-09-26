// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::OLSC {

class INativeHandleHolder;
class IStopperObject;

class ITransferTaskListController final : public ServiceFramework<ITransferTaskListController> {
public:
    explicit ITransferTaskListController(Core::System& system_);
    ~ITransferTaskListController() override;

private:
    Result GetTransferTaskEndEventNativeHandleHolder(Out<SharedPointer<INativeHandleHolder>> out_holder);
    Result GetTransferTaskStartEventNativeHandleHolder(Out<SharedPointer<INativeHandleHolder>> out_holder);
    Result StopNextTransferTaskExecution(Out<SharedPointer<IStopperObject>> out_stopper);
    Result GetTransferTaskCount(Out<u32> out_count, u8 unknown);
    Result GetTransferTaskProgress();
    Result GetCurrentTransferTaskInfo(Out<std::array<u8, 0x30>> out_info, u8 unknown);
    Result FindTransferTaskInfo(Out<std::array<u8, 0x30>> out_info, InBuffer<BufferAttr_HipcAutoSelect> in);

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
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
        FunctionInfo{30, nullptr, "Unknown30"}, //20.1.0+
    );
};

} // namespace Service::OLSC
