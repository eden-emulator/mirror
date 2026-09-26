// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/polyfill_thread.h"
#include "core/hle/service/sm/sm.h"
// Include these for proper template instancing
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service {

/// @brief The purpose of this class is to own any objects that need to be shared across the other service
/// implementations. Will be torn down when the global system instance is shutdown.
class Services final {
public:
    explicit Services(std::shared_ptr<SM::ServiceManager>& sm, Core::System& system,
                      std::stop_token token);
    ~Services() = default;
};

} // namespace Service
