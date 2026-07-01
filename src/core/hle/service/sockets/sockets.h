// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/common_funcs.h"
#include "common/common_types.h"
#include "core/internal_network/socket_types.h"

namespace Core {
class System;
}

namespace Service::Sockets {

void LoopProcess(Core::System& system);

} // namespace Service::Sockets
