// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/nvnflinger/hos_binder_driver.h"
#include "core/hle/service/vi/application_display_service.h"
#include "core/hle/service/vi/container.h"
#include "core/hle/service/vi/manager_root_service.h"
#include "core/hle/service/vi/service_creator.h"
#include "core/hle/service/vi/vi.h"
#include "core/hle/service/vi/vi_types.h"

namespace Service::VI {

ServiceFrameworkBase::FunctionInfoBase const* IManagerRootService::FindRequest(u32 key) {
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{2, C<&IManagerRootService::GetDisplayService>, "GetDisplayService"},
        FunctionInfo{3, nullptr, "GetDisplayServiceWithProxyNameExchange"},
        FunctionInfo{100, nullptr, "PrepareFatal"},
        FunctionInfo{101, nullptr, "ShowFatal"},
        FunctionInfo{102, nullptr, "DrawFatalRectangle"},
        FunctionInfo{103, nullptr, "DrawFatalText32"}
    );
    return HandlerTableGenerateWithFind(key, functions);
}

IManagerRootService::IManagerRootService(Core::System& system_,
                                         std::shared_ptr<Container> container)
    : ServiceFramework{system_, "vi:m"}, m_container{std::move(container)} {
}

IManagerRootService::~IManagerRootService() = default;

Result IManagerRootService::GetDisplayService(
    Out<SharedPointer<IApplicationDisplayService>> out_application_display_service, Policy policy) {
    LOG_DEBUG(Service_VI, "called");
    R_RETURN(GetApplicationDisplayService(out_application_display_service, system, m_container,
                                          Permission::Manager, policy));
}

} // namespace Service::VI
