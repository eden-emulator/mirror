// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::Set {

class IFirmwareDebugSettingsServer final : public ServiceFramework<IFirmwareDebugSettingsServer> {
public:
    explicit IFirmwareDebugSettingsServer(Core::System& system_);
    ~IFirmwareDebugSettingsServer() override;

    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{2, nullptr, "SetSettingsItemValue"},
        FunctionInfo{3, nullptr, "ResetSettingsItemValue"},
        FunctionInfo{4, nullptr, "CreateSettingsItemKeyIterator"},
        FunctionInfo{10, nullptr, "ReadSettings"},
        FunctionInfo{11, nullptr, "ResetSettings"},
        FunctionInfo{20, nullptr, "SetWebInspectorFlag"},
        FunctionInfo{21, nullptr, "SetAllowedSslHosts"},
        FunctionInfo{22, nullptr, "SetHostFsMountPoint"},
        FunctionInfo{23, nullptr, "SetMemoryUsageRateFlag"},
        FunctionInfo{24, nullptr, "CommitSettings"}, //20.0.0+
        FunctionInfo{27, nullptr, "SetHttpAuthConfigs"} //21.0.0+
    );
};

} // namespace Service::Set
