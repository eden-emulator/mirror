// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Kernel {
class KProcess;
}

namespace Service::VI {

class Container;

class IManagerDisplayService final : public ServiceFramework<IManagerDisplayService> {
public:
    explicit IManagerDisplayService(Core::System& system_, std::shared_ptr<Container> container);
    ~IManagerDisplayService() override;

    Result CreateSharedLayerSession(Kernel::KProcess* owner_process, u64* out_buffer_id,
                                    u64* out_layer_handle, u64 display_id, bool enable_blending);
    void DestroySharedLayerSession(Kernel::KProcess* owner_process);

    Result SetLayerBlending(bool enabled, u64 layer_id);
    Result SetLayerZIndex(s32 z_index, u64 layer_id);

public:
    Result CreateManagedLayer(Out<u64> out_layer_id, u32 flags, u64 display_id,
                              AppletResourceUserId aruid);
    Result DestroyManagedLayer(u64 layer_id);
    Result AddToLayerStack(u32 stack_id, u64 layer_id);
    Result SetLayerVisibility(bool visible, u64 layer_id);

private:
    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
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
    const std::shared_ptr<Container> m_container;
};

} // namespace Service::VI
