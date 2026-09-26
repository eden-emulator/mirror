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

class IApplicationRootService final : public ServiceFramework<IApplicationRootService> {
public:
    explicit IApplicationRootService(Core::System& system_, std::shared_ptr<Container> container);
    ~IApplicationRootService() override;

private:
    Result GetDisplayService(
        Out<SharedPointer<IApplicationDisplayService>> out_application_display_service,
        Policy policy);

private:
    std::optional<FunctionInfoBase> FindRequest(u32 key) override;
    const std::shared_ptr<Container> m_container;
};

} // namespace Service::VI
