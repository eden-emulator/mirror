// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/uuid.h"
#include "core/hle/service/glue/glue_manager.h"
#include "core/hle/service/service.h"

namespace Service::Account {

class ProfileManager;

void LoopProcess(Core::System& system);

} // namespace Service::Account
