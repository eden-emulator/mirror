// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/hle/service/ldn/sf_service.h"

namespace Service::LDN {

ISfService::ISfService(Core::System& system_) : ServiceFramework{system_, "ISfService"} {
}

ISfService::~ISfService() = default;

} // namespace Service::LDN
