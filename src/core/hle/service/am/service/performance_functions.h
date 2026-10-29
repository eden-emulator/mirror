// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Service::AM {

class IPerformanceFunctions final : public ServiceFramework<IPerformanceFunctions> {
public:
    explicit IPerformanceFunctions(Core::System& system);
    ~IPerformanceFunctions() override;
};

} // namespace Service::AM
