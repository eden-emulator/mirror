// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/core.h"
#include "core/hle/kernel/k_client_session.h"
#include "core/hle/kernel/k_scoped_resource_reservation.h"
#include "core/hle/kernel/k_session.h"
#include "core/hle/kernel/svc_results.h"
#include "core/hle/service/am/service/graphics_session.h"
#include "core/hle/service/server_manager.h"

namespace Service::AM {
Result CreateGraphicsSession(Core::System& system, HLERequestContext& ctx,
                             std::shared_ptr<SessionRequestHandler> handler,
                             Kernel::KClientSession** out_client) {
    auto& kernel = system.Kernel();
    Kernel::KScopedResourceReservation reservation(
        kernel, Kernel::GetCurrentProcessPointer(kernel), Kernel::LimitableResource::SessionCountMax);
    R_UNLESS(reservation.Succeeded(), Kernel::ResultLimitReached);
    auto* session = Kernel::KSession::Create(kernel);
    R_UNLESS(session != nullptr, Kernel::ResultOutOfResource);
    session->Initialize(kernel, nullptr, 0);
    reservation.Commit();
    Kernel::KSession::Register(kernel, session);
    auto& server = ctx.GetManager()->GetServerManager();
    auto manager = std::make_shared<SessionRequestManager>(kernel, server);
    manager->SetSessionHandler(std::move(handler));
    R_TRY(server.RegisterSession(&session->GetServerSession(), std::move(manager)));
    *out_client = &session->GetClientSession();
    R_SUCCEED();
}

} // namespace Service::AM
