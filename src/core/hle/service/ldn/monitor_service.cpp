// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ldn/ldn_results.h"
#include "core/hle/service/ldn/monitor_service.h"

namespace Service::LDN {

std::optional<ServiceFrameworkBase::FunctionInfoBase> IMonitorService::FindRequest(u32 key) {
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, C<&IMonitorService::GetStateForMonitor>, "GetStateForMonitor"},
        FunctionInfo{1, nullptr, "GetNetworkInfoForMonitor"},
        FunctionInfo{2, nullptr, "GetIpv4AddressForMonitor"},
        FunctionInfo{3, nullptr, "GetDisconnectReasonForMonitor"},
        FunctionInfo{4, nullptr, "GetSecurityParameterForMonitor"},
        FunctionInfo{5, nullptr, "GetNetworkConfigForMonitor"},
        FunctionInfo{100, C<&IMonitorService::InitializeMonitor>, "InitializeMonitor"},
        FunctionInfo{101, C<&IMonitorService::FinalizeMonitor>, "FinalizeMonitor"}
    );
    return HandlerTableGenerateWithFind(key, functions);
}

IMonitorService::IMonitorService(Core::System& system_)
    : ServiceFramework{system_, "IMonitorService"} {
}

IMonitorService::~IMonitorService() = default;

Result IMonitorService::GetStateForMonitor(Out<State> out_state) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    *out_state = State::None;
    R_SUCCEED();
}

Result IMonitorService::InitializeMonitor() {
    LOG_INFO(Service_LDN, "called");
    R_SUCCEED();
}

Result IMonitorService::FinalizeMonitor() {
    LOG_INFO(Service_LDN, "called");
    R_SUCCEED();
}

} // namespace Service::LDN
