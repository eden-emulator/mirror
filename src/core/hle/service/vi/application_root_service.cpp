// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/vi/application_display_service.h"
#include "core/hle/service/vi/application_root_service.h"
#include "core/hle/service/vi/container.h"
#include "core/hle/service/vi/service_creator.h"
#include "core/hle/service/vi/vi.h"
#include "core/hle/service/vi/vi_types.h"

namespace Service::VI {

ServiceFrameworkBase::FunctionInfoBase const* IApplicationRootService::FindRequest(u32 key) {
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, C<&IApplicationRootService::GetDisplayService>, "GetDisplayService"},
        FunctionInfo{1, nullptr, "GetDisplayServiceWithProxyNameExchange"}
    );
    return HandlerTableGenerateWithFind(key, functions);
}

IApplicationRootService::IApplicationRootService(Core::System& system_,
                                                 std::shared_ptr<Container> container)
    : ServiceFramework{system_, "vi:u"}, m_container{std::move(container)} {
}

IApplicationRootService::~IApplicationRootService() = default;

Result IApplicationRootService::GetDisplayService(
    Out<SharedPointer<IApplicationDisplayService>> out_application_display_service, Policy policy) {
    LOG_DEBUG(Service_VI, "called");
    R_RETURN(GetApplicationDisplayService(out_application_display_service, system, m_container,
                                          Permission::User, policy));
}

} // namespace Service::VI
