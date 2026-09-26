// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ldn/ldn_types.h"
#include "core/hle/service/ldn/sf_service_monitor.h"

namespace Service::LDN {

std::optional<ServiceFrameworkBase::FunctionInfoBase> ISfServiceMonitor::FindRequest(u32 key) {
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, C<&ISfServiceMonitor::Initialize>, "Initialize"},
        FunctionInfo{256, nullptr, "AttachNetworkInterfaceStateChangeEvent"},
        FunctionInfo{264, nullptr, "GetNetworkInterfaceLastError"},
        FunctionInfo{272, nullptr, "GetRole"},
        FunctionInfo{280, nullptr, "GetAdvertiseData"},
        FunctionInfo{281, nullptr, "GetAdvertiseData2"},
        FunctionInfo{288, C<&ISfServiceMonitor::GetGroupInfo>, "GetGroupInfo"},
        FunctionInfo{296, nullptr, "GetGroupInfo2"},
        FunctionInfo{304, nullptr, "GetGroupOwner"},
        FunctionInfo{312, nullptr, "GetIpConfig"},
        FunctionInfo{320, nullptr, "GetLinkLevel"},
        FunctionInfo{328, nullptr, "AttachJoinEvent"},
        FunctionInfo{336, nullptr, "GetMembers"}
    );
    return HandlerTableGenerateWithFind(key, functions);
}

ISfServiceMonitor::ISfServiceMonitor(Core::System& system_)
    : ServiceFramework{system_, "ISfServiceMonitor"} {
}

ISfServiceMonitor::~ISfServiceMonitor() = default;

Result ISfServiceMonitor::Initialize(Out<u32> out_value) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");

    *out_value = 0;
    R_SUCCEED();
}

Result ISfServiceMonitor::GetGroupInfo(
    OutLargeData<GroupInfo, BufferAttr_HipcAutoSelect> out_group_info, GroupInfo in_group_info) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");

    *out_group_info = in_group_info;
    R_SUCCEED();
}

} // namespace Service::LDN
