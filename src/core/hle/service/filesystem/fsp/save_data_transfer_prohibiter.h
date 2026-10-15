// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Service::FileSystem {

class ISaveDataTransferProhibiter : public ServiceFramework<ISaveDataTransferProhibiter> {
public:
    explicit ISaveDataTransferProhibiter(Core::System& system_);
    ~ISaveDataTransferProhibiter() override;
};

} // namespace Service::FileSystem
