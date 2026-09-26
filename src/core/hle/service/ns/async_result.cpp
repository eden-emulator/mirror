// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ns/async_result.h"

#include <cstring>

namespace Service::NS {

ServiceFrameworkBase::FunctionInfoBase const* IAsyncResult::FindRequest(u32 key) {
    static const auto functions = CreateStaticMap(
        FunctionInfo{0, nullptr, "Get"},
        FunctionInfo{1, D<&IAsyncResult::Cancel>, "Cancel"},
        FunctionInfo{2, nullptr, "GetErrorContext"} // 4.0.0+
    );
    return HandlerTableGenerateWithFind(key, functions);
}

IAsyncResult::IAsyncResult(Core::System& system_, Service::Event* event_)
    : ServiceFramework{system_, "nn::ns::detail::IAsyncResult"}, event{event_} {
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
