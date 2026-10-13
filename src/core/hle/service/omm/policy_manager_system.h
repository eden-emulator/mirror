// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::OMM {

class IPolicyManagerSystem final : public ServiceFramework<IPolicyManagerSystem> {
public:
    explicit IPolicyManagerSystem(Core::System& system_);
    ~IPolicyManagerSystem() override;

    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, nullptr, "GetAutoPowerDownEvent"},
        FunctionInfo{1, nullptr, "IsAutoPowerDownRequested"},
        FunctionInfo{2, nullptr, "Unknown2"},
        FunctionInfo{3, nullptr, "SetHandlingContext"},
        FunctionInfo{4, nullptr, "LoadAndApplySettings"},
        FunctionInfo{5, nullptr, "ReportUserIsActive"}
    );
};

} // namespace Service::OMM
