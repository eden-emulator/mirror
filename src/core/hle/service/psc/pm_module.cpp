// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/psc/pm_module.h"

namespace Service::PSC {

IPmModule::IPmModule(Core::System& system_) : ServiceFramework{system_, "IPmModule"} {
    // clang-format off
        FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
            FunctionInfo{0, nullptr, "Initialize"},
            FunctionInfo{1, nullptr, "GetRequest"},
            FunctionInfo{2, nullptr, "Acknowledge"},
            FunctionInfo{3, nullptr, "Finalize"},
            FunctionInfo{4, nullptr, "AcknowledgeEx"}
        );
}

IPmModule::~IPmModule() = default;

} // namespace Service::PSC
