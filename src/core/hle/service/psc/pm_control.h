// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Service::PSC {

class IPmControl final : public ServiceFramework<IPmControl> {
public:
    explicit IPmControl(Core::System& system_);
    ~IPmControl() override;

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static const auto functions = CreateStaticMap(
        FunctionInfo{0, nullptr, "Initialize"},
        FunctionInfo{1, nullptr, "DispatchRequest"},
        FunctionInfo{2, nullptr, "GetResult"},
        FunctionInfo{3, nullptr, "GetState"},
        FunctionInfo{4, nullptr, "Cancel"},
        FunctionInfo{5, nullptr, "PrintModuleInformation"},
        FunctionInfo{6, nullptr, "GetModuleInformation"},
        FunctionInfo{10, nullptr, "AcquireStateLock"},
        FunctionInfo{11, nullptr, "HasStateLock"}
    );
};

} // namespace Service::PSC
