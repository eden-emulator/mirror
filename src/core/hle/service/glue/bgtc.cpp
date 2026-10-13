// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/logging.h"
#include "core/core.h"
#include "core/hle/service/glue/bgtc.h"
#include "core/hle/service/ipc_helpers.h"

namespace Service::Glue {

BGTC_T::BGTC_T(Core::System& system_) : ServiceFramework{system_, "bgtc:t"} {}

BGTC_T::~BGTC_T() = default;

void BGTC_T::OpenTaskService(HLERequestContext& ctx) {
    LOG_DEBUG(Service_BGTC, "called");

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<ITaskService>(ctx, system);
}

ITaskService::ITaskService(Core::System& system_) : ServiceFramework{system_, "ITaskService"} {
}

ITaskService::~ITaskService() = default;

BGTC_SC::BGTC_SC(Core::System& system_) : ServiceFramework{system_, "bgtc:sc"} {
}

BGTC_SC::~BGTC_SC() = default;

} // namespace Service::Glue
