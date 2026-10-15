// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::Capture {

class IScreenShotControlService final : public ServiceFramework<IScreenShotControlService> {
public:
    explicit IScreenShotControlService(Core::System& system_);
    ~IScreenShotControlService() override;
};

} // namespace Service::Capture
