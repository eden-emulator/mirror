// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ns/i_async_result.h"

#include <cstring>

namespace Service::NS {

IAsyncResult::IAsyncResult(Core::System& system_, Service::Event* event_)
    : ServiceFramework{system_, "nn::ns::detail::IAsyncResult"}, event{event_} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, nullptr, "Get"},
        {1, D<&IAsyncResult::Cancel>, "Cancel"},
        {2, nullptr, "GetErrorContext"}, // 4.0.0+
    };
    // clang-format on

    RegisterHandlers(functions);
}

IAsyncResult::~IAsyncResult() = default;

Result IAsyncResult::Cancel() {
    LOG_DEBUG(Service_NS, "called");
    if (event != nullptr) {
        event->Signal(system.Kernel());
    }

    R_SUCCEED();
}

} // namespace Service::NS