// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/vi/application_display_service.h"
#include "core/hle/service/vi/container.h"
#include "core/hle/service/vi/service_creator.h"
#include "core/hle/service/vi/system_root_service.h"
#include "core/hle/service/vi/vi.h"
#include "core/hle/service/vi/vi_types.h"

namespace Service::VI {

ServiceFrameworkBase::FunctionInfoBase const* ISystemRootService::FindRequest(u32 key) {
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{1, C<&ISystemRootService::GetDisplayService>, "GetDisplayService"},
        FunctionInfo{3, nullptr, "GetDisplayServiceWithProxyNameExchange"}
    );
    return HandlerTableGenerateWithFind(key, functions);
}

ISystemRootService::ISystemRootService(Core::System& system_, std::shared_ptr<Container> container)
    : ServiceFramework{system_, "vi:s"}, m_container{std::move(container)}
{}

ISystemRootService::~ISystemRootService() = default;

Result ISystemRootService::GetDisplayService(
    Out<SharedPointer<IApplicationDisplayService>> out_application_display_service, Policy policy) {
    LOG_DEBUG(Service_VI, "called");
    R_RETURN(GetApplicationDisplayService(out_application_display_service, system, m_container,
                                          Permission::System, policy));
}

} // namespace Service::VI
