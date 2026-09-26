// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Service::NS {

class IDownloadTaskInterface final : public ServiceFramework<IDownloadTaskInterface> {
public:
    explicit IDownloadTaskInterface(Core::System& system_);
    ~IDownloadTaskInterface() override;

private:
    Result EnableAutoCommit();
    Result DisableAutoCommit();

    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{701, nullptr, "ClearTaskStatusList"},
        FunctionInfo{702, nullptr, "RequestDownloadTaskList"},
        FunctionInfo{703, nullptr, "RequestEnsureDownloadTask"},
        FunctionInfo{704, nullptr, "ListDownloadTaskStatus"},
        FunctionInfo{705, nullptr, "RequestDownloadTaskListData"},
        FunctionInfo{706, nullptr, "TryCommitCurrentApplicationDownloadTask"},
        FunctionInfo{707, D<&IDownloadTaskInterface::EnableAutoCommit>, "EnableAutoCommit"},
        FunctionInfo{708, D<&IDownloadTaskInterface::DisableAutoCommit>, "DisableAutoCommit"},
        FunctionInfo{709, nullptr, "TriggerDynamicCommitEvent"}
    );
};

} // namespace Service::NS
