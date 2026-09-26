// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/math_util.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/nvnflinger/ui/fence.h"
#include "core/hle/service/service.h"
#include "core/hle/service/vi/shared_buffer_manager.h"

namespace Service::VI {
struct DisplayMode;

class Container;

class ISystemDisplayService final : public ServiceFramework<ISystemDisplayService> {
public:
    explicit ISystemDisplayService(Core::System& system_, std::shared_ptr<Container> container);
    ~ISystemDisplayService() override;

private:
    Result GetLayerZ(Out<u64> out_z_value, u64 layer_id);
    Result SetLayerZ(u64 layer_id, u64 z_value);
    Result SetLayerVisibility(bool visible, u64 layer_id);
    Result ListDisplayModes(Out<u64> out_count, u64 display_id,
                            OutArray<DisplayMode, BufferAttr_HipcMapAlias> out_display_modes);
    Result GetDisplayMode(Out<DisplayMode> out_display_mode, u64 display_id);

    Result GetSharedBufferMemoryHandleId(
        Out<s32> out_nvmap_handle, Out<u64> out_size,
        OutLargeData<SharedMemoryPoolLayout, BufferAttr_HipcMapAlias> out_pool_layout,
        u64 buffer_id, ClientAppletResourceUserId aruid);
    Result OpenSharedLayer(u64 layer_id);
    Result ConnectSharedLayer(u64 layer_id);
    Result GetSharedFrameBufferAcquirableEvent(OutCopyHandle<Kernel::KReadableEvent> out_event,
                                               u64 layer_id);
    Result AcquireSharedFrameBuffer(Out<android::Fence> out_fence,
                                    Out<std::array<s32, 4>> out_slots, Out<s64> out_target_slot,
                                    u64 layer_id);
    Result PresentSharedFrameBuffer(android::Fence fence, Common::Rectangle<s32> crop_region,
                                    u32 window_transform, s32 swap_interval, u64 layer_id,
                                    s64 surface_id);
    Result CancelSharedFrameBuffer(u64 layer_id, s64 slot);

private:
    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{1200, nullptr, "GetZOrderCountMin"},
        FunctionInfo{1202, nullptr, "GetZOrderCountMax"},
        FunctionInfo{1203, nullptr, "GetDisplayLogicalResolution"},
        FunctionInfo{1204, nullptr, "SetDisplayMagnification"},
        FunctionInfo{2201, nullptr, "SetLayerPosition"},
        FunctionInfo{2203, nullptr, "SetLayerSize"},
        FunctionInfo{2204, C<&ISystemDisplayService::GetLayerZ>, "GetLayerZ"},
        FunctionInfo{2205, C<&ISystemDisplayService::SetLayerZ>, "SetLayerZ"},
        FunctionInfo{2207, C<&ISystemDisplayService::SetLayerVisibility>, "SetLayerVisibility"},
        FunctionInfo{2209, nullptr, "SetLayerAlpha"},
        FunctionInfo{2210, nullptr, "SetLayerPositionAndSize"},
        FunctionInfo{2312, nullptr, "CreateStrayLayer"},
        FunctionInfo{2400, nullptr, "OpenIndirectLayer"},
        FunctionInfo{2401, nullptr, "CloseIndirectLayer"},
        FunctionInfo{2402, nullptr, "FlipIndirectLayer"},
        FunctionInfo{3000, C<&ISystemDisplayService::ListDisplayModes>, "ListDisplayModes"},
        FunctionInfo{3001, nullptr, "ListDisplayRgbRanges"},
        FunctionInfo{3002, nullptr, "ListDisplayContentTypes"},
        FunctionInfo{3200, C<&ISystemDisplayService::GetDisplayMode>, "GetDisplayMode"},
        FunctionInfo{3201, nullptr, "SetDisplayMode"},
        FunctionInfo{3202, nullptr, "GetDisplayUnderscan"},
        FunctionInfo{3203, nullptr, "SetDisplayUnderscan"},
        FunctionInfo{3204, nullptr, "GetDisplayContentType"},
        FunctionInfo{3205, nullptr, "SetDisplayContentType"},
        FunctionInfo{3206, nullptr, "GetDisplayRgbRange"},
        FunctionInfo{3207, nullptr, "SetDisplayRgbRange"},
        FunctionInfo{3208, nullptr, "GetDisplayCmuMode"},
        FunctionInfo{3209, nullptr, "SetDisplayCmuMode"},
        FunctionInfo{3210, nullptr, "GetDisplayContrastRatio"},
        FunctionInfo{3211, nullptr, "SetDisplayContrastRatio"},
        FunctionInfo{3214, nullptr, "GetDisplayGamma"},
        FunctionInfo{3215, nullptr, "SetDisplayGamma"},
        FunctionInfo{3216, nullptr, "GetDisplayCmuLuma"},
        FunctionInfo{3217, nullptr, "SetDisplayCmuLuma"},
        FunctionInfo{3218, nullptr, "SetDisplayCrcMode"},
        FunctionInfo{6013, nullptr, "GetLayerPresentationSubmissionTimestamps"},
        FunctionInfo{8225, C<&ISystemDisplayService::GetSharedBufferMemoryHandleId>, "GetSharedBufferMemoryHandleId"},
        FunctionInfo{8250, C<&ISystemDisplayService::OpenSharedLayer>, "OpenSharedLayer"},
        FunctionInfo{8251, nullptr, "CloseSharedLayer"},
        FunctionInfo{8252, C<&ISystemDisplayService::ConnectSharedLayer>, "ConnectSharedLayer"},
        FunctionInfo{8253, nullptr, "DisconnectSharedLayer"},
        FunctionInfo{8254, C<&ISystemDisplayService::AcquireSharedFrameBuffer>, "AcquireSharedFrameBuffer"},
        FunctionInfo{8255, C<&ISystemDisplayService::PresentSharedFrameBuffer>, "PresentSharedFrameBuffer"},
        FunctionInfo{8256, C<&ISystemDisplayService::GetSharedFrameBufferAcquirableEvent>, "GetSharedFrameBufferAcquirableEvent"},
        FunctionInfo{8257, nullptr, "FillSharedFrameBufferColor"},
        FunctionInfo{8258, C<&ISystemDisplayService::CancelSharedFrameBuffer>, "CancelSharedFrameBuffer"},
        FunctionInfo{9000, nullptr, "GetDp2hdmiController"}
    );
    const std::shared_ptr<Container> m_container;
};

} // namespace Service::VI
