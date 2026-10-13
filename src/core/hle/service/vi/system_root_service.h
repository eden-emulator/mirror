// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::VI {

class Container;
class IApplicationDisplayService;
enum class Policy : u32;

class ISystemRootService final : public ServiceFramework<ISystemRootService> {
public:
    explicit ISystemRootService(Core::System& system_, std::shared_ptr<Container> container);
    ~ISystemRootService() override;

private:
    Result GetDisplayService(
        Out<SharedPointer<IApplicationDisplayService>> out_application_display_service,
        Policy policy);

    std::optional<FunctionInfoBase> FindRequest(u32 key) override;
    const std::shared_ptr<Container> m_container;
};

} // namespace Service::VI
