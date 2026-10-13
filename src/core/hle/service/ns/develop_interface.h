// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Service::NS {

class IDevelopInterface final : public ServiceFramework<IDevelopInterface> {
public:
    explicit IDevelopInterface(Core::System& system_);
    ~IDevelopInterface() override;

    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, nullptr, "LaunchProgram"},
        FunctionInfo{1, nullptr, "TerminateProcess"},
        FunctionInfo{2, nullptr, "TerminateProgram"},
        FunctionInfo{4, nullptr, "GetShellEvent"},
        FunctionInfo{5, nullptr, "GetShellEventInfo"},
        FunctionInfo{6, nullptr, "TerminateApplication"},
        FunctionInfo{7, nullptr, "PrepareLaunchProgramFromHost"},
        FunctionInfo{8, nullptr, "LaunchApplicationFromHost"},
        FunctionInfo{9, nullptr, "LaunchApplicationWithStorageIdForDevelop"},
        FunctionInfo{10, nullptr, "IsSystemMemoryResourceLimitBoosted"},
        FunctionInfo{11, nullptr, "GetRunningApplicationProcessIdForDevelop"},
        FunctionInfo{12, nullptr, "SetCurrentApplicationRightsEnvironmentCanBeActiveForDevelop"},
        FunctionInfo{13, nullptr, "CreateApplicationResourceForDevelop"},
        FunctionInfo{14, nullptr, "IsPreomiaForDevelop"},
        FunctionInfo{15, nullptr, "GetApplicationProgramIdFromHost"},
        FunctionInfo{16, nullptr, "RefreshCachedDebugValues"},
        FunctionInfo{17, nullptr, "PrepareLaunchApplicationFromHost"},
        FunctionInfo{18, nullptr, "GetLaunchEvent"},
        FunctionInfo{19, nullptr, "GetLaunchResult"}
    );
};

} // namespace Service::NS
