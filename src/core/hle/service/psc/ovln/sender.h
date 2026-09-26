// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/psc/ovln/ovln_types.h"
#include "core/hle/service/service.h"

namespace Service::PSC {

class ISender final : public ServiceFramework<ISender> {
public:
    explicit ISender(Core::System& system_);
    ~ISender() override;

private:
    Result Send(const OverlayNotification& notification, MessageFlags flags);

    std::optional<FunctionInfoBase> FindRequest(u32 key) override;
};

} // namespace Service::PSC
