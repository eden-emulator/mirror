// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/settings.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/vi/container.h"
#include "core/hle/service/vi/system_display_service.h"
#include "core/hle/service/vi/vi_types.h"

namespace Service::VI {

std::optional<ServiceFrameworkBase::FunctionInfoBase> ISystemDisplayService::FindRequest(u32 key) {
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
    return HandlerTableGenerateWithFind(key, functions);
}

ISystemDisplayService::ISystemDisplayService(Core::System& system_, std::shared_ptr<Container> container)
    : ServiceFramework{system_, "ISystemDisplayService"}, m_container{std::move(container)}
{}

ISystemDisplayService::~ISystemDisplayService() = default;

Result ISystemDisplayService::GetLayerZ(Out<u64> out_z_value, u64 layer_id) {
    LOG_DEBUG(Service_VI, "called. layer_id={}", layer_id);
    s32 z{};
    const auto res = m_container->GetLayerZIndex(layer_id, &z);
    R_TRY(res);
    *out_z_value = static_cast<u64>(z);
    R_SUCCEED();
}

Result ISystemDisplayService::SetLayerZ(u64 layer_id, u64 z_value) {
    LOG_DEBUG(Service_VI, "called. layer_id={}, z_value={}", layer_id, z_value);
    // Forward to container using internal API when available
    R_RETURN(m_container->SetLayerZIndex(layer_id, static_cast<s32>(z_value)));
}

// This function currently does nothing but return a success error code in
// the vi library itself, so do the same thing, but log out the passed in values.
Result ISystemDisplayService::SetLayerVisibility(bool visible, u64 layer_id) {
    LOG_DEBUG(Service_VI, "called, layer_id={}, visible={}", layer_id, visible);
    R_RETURN(m_container->SetLayerVisibility(layer_id, visible));
}

Result ISystemDisplayService::ListDisplayModes(
    Out<u64> out_count, u64 display_id,
    OutArray<DisplayMode, BufferAttr_HipcMapAlias> out_display_modes) {
    LOG_WARNING(Service_VI, "(STUBBED) called, display_id={}", display_id);

    if (!out_display_modes.empty()) {
        out_display_modes[0] = {
            .width = 1920,
            .height = 1080,
            .refresh_rate = 60.f,
            .unknown = {},
        };
        *out_count = 1;
    } else {
        *out_count = 0;
    }

    R_SUCCEED();
}

Result ISystemDisplayService::GetDisplayMode(Out<DisplayMode> out_display_mode, u64 display_id) {
    LOG_WARNING(Service_VI, "(STUBBED) called, display_id={}", display_id);

    if (Settings::IsDockedMode()) {
        out_display_mode->width = static_cast<u32>(DisplayResolution::DockedWidth);
        out_display_mode->height = static_cast<u32>(DisplayResolution::DockedHeight);
    } else {
        out_display_mode->width = static_cast<u32>(DisplayResolution::UndockedWidth);
        out_display_mode->height = static_cast<u32>(DisplayResolution::UndockedHeight);
    }

    out_display_mode->refresh_rate = 60.f; // This wouldn't seem to be correct for 30 fps games.
    out_display_mode->unknown = 0;

    R_SUCCEED();
}

Result ISystemDisplayService::GetSharedBufferMemoryHandleId(
    Out<s32> out_nvmap_handle, Out<u64> out_size,
    OutLargeData<SharedMemoryPoolLayout, BufferAttr_HipcMapAlias> out_pool_layout, u64 buffer_id,
    ClientAppletResourceUserId aruid) {
    LOG_INFO(Service_VI, "called. buffer_id={}, aruid={:#x}", buffer_id, aruid.pid);

    R_RETURN(m_container->GetSharedBufferManager()->GetSharedBufferMemoryHandleId(
        out_size, out_nvmap_handle, out_pool_layout, buffer_id, aruid.pid));
}

Result ISystemDisplayService::OpenSharedLayer(u64 layer_id) {
    LOG_INFO(Service_VI, "(STUBBED) called. layer_id={}", layer_id);
    R_SUCCEED();
}

Result ISystemDisplayService::ConnectSharedLayer(u64 layer_id) {
    LOG_INFO(Service_VI, "(STUBBED) called. layer_id={}", layer_id);
    R_SUCCEED();
}

Result ISystemDisplayService::AcquireSharedFrameBuffer(Out<android::Fence> out_fence,
                                                       Out<std::array<s32, 4>> out_slots,
                                                       Out<s64> out_target_slot, u64 layer_id) {
    LOG_DEBUG(Service_VI, "called");
    R_RETURN(m_container->GetSharedBufferManager()->AcquireSharedFrameBuffer(
        out_fence, *out_slots, out_target_slot, layer_id));
}

Result ISystemDisplayService::PresentSharedFrameBuffer(android::Fence fence,
                                                       Common::Rectangle<s32> crop_region,
                                                       u32 window_transform, s32 swap_interval,
                                                       u64 layer_id, s64 surface_id) {
    LOG_DEBUG(Service_VI, "called");
    R_RETURN(m_container->GetSharedBufferManager()->PresentSharedFrameBuffer(
        fence, crop_region, window_transform, swap_interval, layer_id, surface_id));
}

Result ISystemDisplayService::GetSharedFrameBufferAcquirableEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event, u64 layer_id) {
    LOG_DEBUG(Service_VI, "called");
    R_RETURN(m_container->GetSharedBufferManager()->GetSharedFrameBufferAcquirableEvent(out_event,
                                                                                        layer_id));
}

Result ISystemDisplayService::CancelSharedFrameBuffer(u64 layer_id, s64 slot) {
    LOG_DEBUG(Service_VI, "called");
    R_RETURN(m_container->GetSharedBufferManager()->CancelSharedFrameBuffer(layer_id, slot));
}

} // namespace Service::VI
