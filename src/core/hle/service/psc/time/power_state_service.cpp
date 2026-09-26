// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/psc/time/power_state_service.h"

namespace Service::PSC::Time {

ServiceFrameworkBase::FunctionInfoBase const* IPowerStateRequestHandler::FindRequest(u32 key) {
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, D<&IPowerStateRequestHandler::GetPowerStateRequestEventReadableHandle>, "GetPowerStateRequestEventReadableHandle"},
        FunctionInfo{1, D<&IPowerStateRequestHandler::GetAndClearPowerStateRequest>, "GetAndClearPowerStateRequest"}
    );
    return HandlerTableGenerateWithFind(key, functions);
}

IPowerStateRequestHandler::IPowerStateRequestHandler(
    Core::System& system_, PowerStateRequestManager& power_state_request_manager)
    : ServiceFramework{system_, "time:p"}
    , m_power_state_request_manager{power_state_request_manager}
{}

Result IPowerStateRequestHandler::GetPowerStateRequestEventReadableHandle(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_Time, "called.");

    *out_event = &m_power_state_request_manager.GetReadableEvent();
    R_SUCCEED();
}

Result IPowerStateRequestHandler::GetAndClearPowerStateRequest(Out<bool> out_cleared,
                                                               Out<u32> out_priority) {
    LOG_DEBUG(Service_Time, "called.");

    u32 priority{};
    auto cleared = m_power_state_request_manager.GetAndClearPowerStateRequest(priority);
    *out_cleared = cleared;

    if (cleared) {
        *out_priority = priority;
    }
    R_SUCCEED();
}

} // namespace Service::PSC::Time
