// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Service::NS {

class IApplicationVersionInterface final : public ServiceFramework<IApplicationVersionInterface> {
public:
    explicit IApplicationVersionInterface(Core::System& system_);
    ~IApplicationVersionInterface() override;

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, nullptr, "GetLaunchRequiredVersion"},
        FunctionInfo{1, nullptr, "UpgradeLaunchRequiredVersion"},
        FunctionInfo{35, nullptr, "UpdateVersionList"},
        FunctionInfo{36, nullptr, "PushLaunchVersion"},
        FunctionInfo{37, nullptr, "ListRequiredVersion"},
        FunctionInfo{800, nullptr, "RequestVersionList"},
        FunctionInfo{801, nullptr, "ListVersionList"},
        FunctionInfo{802, nullptr, "RequestVersionListData"},
        FunctionInfo{900, nullptr, "ImportAutoUpdatePolicyJsonForDebug"},
        FunctionInfo{901, nullptr, "ListDefaultAutoUpdatePolicy"},
        FunctionInfo{902, nullptr, "ListAutoUpdatePolicyForSpecificApplication"},
        FunctionInfo{1000, nullptr, "PerformAutoUpdate"},
        FunctionInfo{1001, nullptr, "ListAutoUpdateSchedule"}
    );
};

} // namespace Service::NS
