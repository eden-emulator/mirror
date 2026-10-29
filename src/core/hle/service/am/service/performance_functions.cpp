// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/hle/service/am/service/performance_functions.h"

namespace Service::AM {

IPerformanceFunctions::IPerformanceFunctions(Core::System& system)
    : ServiceFramework{system, "IPerformanceFunctions"} {
    static const FunctionInfo functions[] = {
        {0, nullptr, "IsCpuOverclockEnabled"},
        {1, nullptr, "SetCpuOverclockEnabled"},
    };
    RegisterHandlers(functions);
}

IPerformanceFunctions::~IPerformanceFunctions() = default;

} // namespace Service::AM
