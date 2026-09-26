// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/vi/container.h"
#include "core/hle/service/vi/manager_display_service.h"

namespace Service::VI {

IManagerDisplayService::IManagerDisplayService(Core::System& system_,
                                               std::shared_ptr<Container> container)
    : ServiceFramework{system_, "IManagerDisplayService"}, m_container{std::move(container)} {
}

IManagerDisplayService::~IManagerDisplayService() = default;

Result IManagerDisplayService::CreateSharedLayerSession(Kernel::KProcess* owner_process,
                                                        u64* out_buffer_id, u64* out_layer_handle,
                                                        u64 display_id, bool enable_blending) {
    R_RETURN(m_container->GetSharedBufferManager()->CreateSession(
        owner_process, out_buffer_id, out_layer_handle, display_id, enable_blending));
}

void IManagerDisplayService::DestroySharedLayerSession(Kernel::KProcess* owner_process) {
    m_container->GetSharedBufferManager()->DestroySession(owner_process);
}

Result IManagerDisplayService::SetLayerBlending(bool enabled, u64 layer_id) {
    R_RETURN(m_container->SetLayerBlending(layer_id, enabled));
}

Result IManagerDisplayService::SetLayerZIndex(s32 z_index, u64 layer_id) {
    R_RETURN(m_container->SetLayerZIndex(layer_id, z_index));
}

Result IManagerDisplayService::CreateManagedLayer(Out<u64> out_layer_id, u32 flags, u64 display_id,
                                                  AppletResourceUserId aruid) {
    LOG_DEBUG(Service_VI, "called. flags={}, display={}, aruid={}", flags, display_id, aruid.pid);
    R_RETURN(m_container->CreateManagedLayer(out_layer_id, display_id, aruid.pid));
}

Result IManagerDisplayService::DestroyManagedLayer(u64 layer_id) {
    LOG_DEBUG(Service_VI, "called. layer_id={}", layer_id);
    R_RETURN(m_container->DestroyManagedLayer(layer_id));
}

Result IManagerDisplayService::AddToLayerStack(u32 stack_id, u64 layer_id) {
    LOG_WARNING(Service_VI, "(STUBBED) called. stack_id={}, layer_id={}", stack_id, layer_id);
    R_SUCCEED();
}

Result IManagerDisplayService::SetLayerVisibility(bool visible, u64 layer_id) {
    LOG_DEBUG(Service_VI, "called, layer_id={}, visible={}", layer_id, visible);
    R_RETURN(m_container->SetLayerVisibility(layer_id, visible));
}

} // namespace Service::VI
