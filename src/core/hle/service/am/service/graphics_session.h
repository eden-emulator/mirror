// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>
#include "core/hle/result.h"

namespace Core { class System; }
namespace Kernel { class KClientSession; }
namespace Service {
class HLERequestContext;
class SessionRequestHandler;
}
namespace Service::AM {
Result CreateGraphicsSession(Core::System& system, HLERequestContext& ctx,
                             std::shared_ptr<SessionRequestHandler> handler,
                             Kernel::KClientSession** out_client);
} // namespace Service::AM
