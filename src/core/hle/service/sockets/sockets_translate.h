// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <utility>

#include "common/common_types.h"
#include "core/hle/service/sockets/sockets.h"
#include "core/internal_network/network.h"

namespace Service::Sockets {

/// Translate guest error to string
const char* Translate(Network::GetAddrInfoError value);

} // namespace Service::Sockets
