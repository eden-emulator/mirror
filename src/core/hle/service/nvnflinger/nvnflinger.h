// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace Core {
class System;
}

namespace Service::Nvnflinger {

void LoopProcess(Core::System& system);

} // namespace Service::Nvnflinger
