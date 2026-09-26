// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/vi/container.h"
#include "core/hle/service/vi/manager_display_service.h"

namespace Service::VI {

ServiceFrameworkBase::FunctionInfoBase const* IManagerDisplayService::FindRequest(u32 key) {
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{200, nullptr, "AllocateProcessHeapBlock"},
        FunctionInfo{201, nullptr, "FreeProcessHeapBlock"},
        FunctionInfo{1102, nullptr, "GetDisplayResolution"},
        FunctionInfo{2010, C<&IManagerDisplayService::CreateManagedLayer>, "CreateManagedLayer"},
        FunctionInfo{2011, C<&IManagerDisplayService::DestroyManagedLayer>, "DestroyManagedLayer"},
        FunctionInfo{2012, nullptr, "CreateStrayLayer"},
        FunctionInfo{2050, nullptr, "CreateIndirectLayer"},
        FunctionInfo{2051, nullptr, "DestroyIndirectLayer"},
        FunctionInfo{2052, nullptr, "CreateIndirectProducerEndPoint"},
        FunctionInfo{2053, nullptr, "DestroyIndirectProducerEndPoint"},
        FunctionInfo{2054, nullptr, "CreateIndirectConsumerEndPoint"},
        FunctionInfo{2055, nullptr, "DestroyIndirectConsumerEndPoint"},
        FunctionInfo{2060, nullptr, "CreateWatermarkCompositor"},
        FunctionInfo{2062, nullptr, "SetWatermarkText"},
        FunctionInfo{2063, nullptr, "SetWatermarkLayerStacks"},
        FunctionInfo{2300, nullptr, "AcquireLayerTexturePresentingEvent"},
        FunctionInfo{2301, nullptr, "ReleaseLayerTexturePresentingEvent"},
        FunctionInfo{2302, nullptr, "GetDisplayHotplugEvent"},
        FunctionInfo{2303, nullptr, "GetDisplayModeChangedEvent"},
        FunctionInfo{2402, nullptr, "GetDisplayHotplugState"},
        FunctionInfo{2501, nullptr, "GetCompositorErrorInfo"},
        FunctionInfo{2601, nullptr, "GetDisplayErrorEvent"},
        FunctionInfo{2701, nullptr, "GetDisplayFatalErrorEvent"},
        FunctionInfo{4201, nullptr, "SetDisplayAlpha"},
        FunctionInfo{4203, nullptr, "SetDisplayLayerStack"},
        FunctionInfo{4205, nullptr, "SetDisplayPowerState"},
        FunctionInfo{4206, nullptr, "SetDefaultDisplay"},
        FunctionInfo{4207, nullptr, "ResetDisplayPanel"},
        FunctionInfo{4208, nullptr, "SetDisplayFatalErrorEnabled"},
        FunctionInfo{4209, nullptr, "IsDisplayPanelOn"},
        FunctionInfo{4300, nullptr, "GetInternalPanelId"},
        FunctionInfo{6000, C<&IManagerDisplayService::AddToLayerStack>, "AddToLayerStack"},
        FunctionInfo{6001, nullptr, "RemoveFromLayerStack"},
        FunctionInfo{6002, C<&IManagerDisplayService::SetLayerVisibility>, "SetLayerVisibility"},
        FunctionInfo{6003, nullptr, "SetLayerConfig"},
        FunctionInfo{6004, nullptr, "AttachLayerPresentationTracer"},
        FunctionInfo{6005, nullptr, "DetachLayerPresentationTracer"},
        FunctionInfo{6006, nullptr, "StartLayerPresentationRecording"},
        FunctionInfo{6007, nullptr, "StopLayerPresentationRecording"},
        FunctionInfo{6008, nullptr, "StartLayerPresentationFenceWait"},
        FunctionInfo{6009, nullptr, "StopLayerPresentationFenceWait"},
        FunctionInfo{6010, nullptr, "GetLayerPresentationAllFencesExpiredEvent"},
        FunctionInfo{6011, nullptr, "EnableLayerAutoClearTransitionBuffer"},
        FunctionInfo{6012, nullptr, "DisableLayerAutoClearTransitionBuffer"},
        FunctionInfo{6013, nullptr, "SetLayerOpacity"},
        FunctionInfo{6014, nullptr, "AttachLayerWatermarkCompositor"},
        FunctionInfo{6015, nullptr, "DetachLayerWatermarkCompositor"},
        FunctionInfo{7000, nullptr, "SetContentVisibility"},
        FunctionInfo{8000, nullptr, "SetConductorLayer"},
        FunctionInfo{8001, nullptr, "SetTimestampTracking"},
        FunctionInfo{8100, nullptr, "SetIndirectProducerFlipOffset"},
        FunctionInfo{8200, nullptr, "CreateSharedBufferStaticStorage"},
        FunctionInfo{8201, nullptr, "CreateSharedBufferTransferMemory"},
        FunctionInfo{8202, nullptr, "DestroySharedBuffer"},
        FunctionInfo{8203, nullptr, "BindSharedLowLevelLayerToManagedLayer"},
        FunctionInfo{8204, nullptr, "BindSharedLowLevelLayerToIndirectLayer"},
        FunctionInfo{8207, nullptr, "UnbindSharedLowLevelLayer"},
        FunctionInfo{8208, nullptr, "ConnectSharedLowLevelLayerToSharedBuffer"},
        FunctionInfo{8209, nullptr, "DisconnectSharedLowLevelLayerFromSharedBuffer"},
        FunctionInfo{8210, nullptr, "CreateSharedLayer"},
        FunctionInfo{8211, nullptr, "DestroySharedLayer"},
        FunctionInfo{8216, nullptr, "AttachSharedLayerToLowLevelLayer"},
        FunctionInfo{8217, nullptr, "ForceDetachSharedLayerFromLowLevelLayer"},
        FunctionInfo{8218, nullptr, "StartDetachSharedLayerFromLowLevelLayer"},
        FunctionInfo{8219, nullptr, "FinishDetachSharedLayerFromLowLevelLayer"},
        FunctionInfo{8220, nullptr, "GetSharedLayerDetachReadyEvent"},
        FunctionInfo{8221, nullptr, "GetSharedLowLevelLayerSynchronizedEvent"},
        FunctionInfo{8222, nullptr, "CheckSharedLowLevelLayerSynchronized"},
        FunctionInfo{8223, nullptr, "RegisterSharedBufferImporterAruid"},
        FunctionInfo{8224, nullptr, "UnregisterSharedBufferImporterAruid"},
        FunctionInfo{8227, nullptr, "CreateSharedBufferProcessHeap"},
        FunctionInfo{8228, nullptr, "GetSharedLayerLayerStacks"},
        FunctionInfo{8229, nullptr, "SetSharedLayerLayerStacks"},
        FunctionInfo{8291, nullptr, "PresentDetachedSharedFrameBufferToLowLevelLayer"},
        FunctionInfo{8292, nullptr, "FillDetachedSharedFrameBufferColor"},
        FunctionInfo{8293, nullptr, "GetDetachedSharedFrameBufferImage"},
        FunctionInfo{8294, nullptr, "SetDetachedSharedFrameBufferImage"},
        FunctionInfo{8295, nullptr, "CopyDetachedSharedFrameBufferImage"},
        FunctionInfo{8296, nullptr, "SetDetachedSharedFrameBufferSubImage"},
        FunctionInfo{8297, nullptr, "GetSharedFrameBufferContentParameter"},
        FunctionInfo{8298, nullptr, "ExpandStartupLogoOnSharedFrameBuffer"}
    );
    return HandlerTableGenerateWithFind(key, functions);
}

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
