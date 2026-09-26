// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::FileSystem {

class FSP_PR final : public ServiceFramework<FSP_PR> {
public:
    explicit FSP_PR(Core::System& system_);
    ~FSP_PR() override;

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, nullptr, "RegisterProgram"},
        FunctionInfo{1, nullptr, "UnregisterProgram"},
        FunctionInfo{2, nullptr, "SetCurrentProcess"},
        FunctionInfo{256, nullptr, "SetEnabledProgramVerification"}
    );
};

} // namespace Service::FileSystem
