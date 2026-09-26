// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/hle/service/bcat/news/overwrite_event_holder.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::News {

ServiceFrameworkBase::FunctionInfoBase const* IOverwriteEventHolder::FindRequest(u32 key) {
    static const auto functions = CreateStaticMap(
        FunctionInfo{0, D<&IOverwriteEventHolder::Get>, "Get"}
    );
    return HandlerTableGenerateWithFind(key, functions);
}

IOverwriteEventHolder::IOverwriteEventHolder(Core::System& system_)
    : ServiceFramework{system_, "IOverwriteEventHolder"}
    , service_context{system_, "IOverwriteEventHolder"} {
    overwrite_event = service_context.CreateEvent("IOverwriteEventHolder::OverwriteEvent");
}

IOverwriteEventHolder::~IOverwriteEventHolder() {
    service_context.CloseEvent(overwrite_event);
}

Result IOverwriteEventHolder::Get(OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_INFO(Service_BCAT, "called");

    *out_event = &overwrite_event->GetReadableEvent();
    R_SUCCEED();
}

} // namespace Service::News
