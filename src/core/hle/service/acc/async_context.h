// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::Account {

class IAsyncContext : public ServiceFramework<IAsyncContext> {
public:
    explicit IAsyncContext(Core::System& system_);
    ~IAsyncContext() override;

    void GetSystemEvent(HLERequestContext& ctx);
    void Cancel(HLERequestContext& ctx);
    void HasDone(HLERequestContext& ctx);
    void GetResult(HLERequestContext& ctx);

protected:
    virtual bool IsComplete() const = 0;
    virtual void Cancel() = 0;
    virtual Result GetResult() const = 0;

    void MarkComplete();

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, &IAsyncContext::GetSystemEvent, "GetSystemEvent"},
        FunctionInfo{1, &IAsyncContext::Cancel, "Cancel"},
        FunctionInfo{2, &IAsyncContext::HasDone, "HasDone"},
        FunctionInfo{3, &IAsyncContext::GetResult, "GetResult"}
    );

    KernelHelpers::ServiceContext service_context;

    std::atomic<bool> is_complete{false};
    Kernel::KEvent* completion_event;
};

} // namespace Service::Account
