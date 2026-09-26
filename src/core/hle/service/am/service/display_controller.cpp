// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/result.h"
#include "core/hle/service/am/applet.h"
#include "core/hle/service/am/service/display_controller.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/vi/shared_buffer_manager.h"

namespace Service::AM {

IDisplayController::IDisplayController(Core::System& system_, std::shared_ptr<Applet> applet_)
    : ServiceFramework{system_, "IDisplayController"}, applet(std::move(applet_)) {
}

IDisplayController::~IDisplayController() = default;

Result IDisplayController::GetLastForegroundCaptureImageEx(
    Out<bool> out_was_written, OutBuffer<BufferAttr_HipcMapAlias> out_image_data) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    *out_was_written = true;
    R_SUCCEED();
}

Result IDisplayController::GetCallerAppletCaptureImageEx(
    Out<bool> out_was_written, OutBuffer<BufferAttr_HipcMapAlias> out_image_data) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    *out_was_written = true;
    R_SUCCEED();
}

Result IDisplayController::TakeScreenShotOfOwnLayer(bool unknown0, s32 fbshare_layer_index) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    R_SUCCEED();
}

Result IDisplayController::ClearCaptureBuffer(bool unknown0, s32 fbshare_layer_index, u32 color) {
    LOG_DEBUG(Service_AM, "called, unknown0={} fbshare_layer_index={} color={:#x}", unknown0,
              fbshare_layer_index, color);
    R_RETURN(applet->display_layer_manager.ClearAppletCaptureBuffer(fbshare_layer_index, color));
}

Result IDisplayController::AcquireLastForegroundCaptureSharedBuffer(
    Out<bool> out_was_written, Out<s32> out_fbshare_layer_index) {
    LOG_DEBUG(Service_AM, "called");
    R_RETURN(applet->display_layer_manager.WriteAppletCaptureBuffer(
        out_was_written, out_fbshare_layer_index, VI::CaptureKind::LastForeground));
}

Result IDisplayController::ReleaseLastForegroundCaptureSharedBuffer() {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    R_SUCCEED();
}

Result IDisplayController::AcquireCallerAppletCaptureSharedBuffer(
    Out<bool> out_was_written, Out<s32> out_fbshare_layer_index) {
    LOG_DEBUG(Service_AM, "called");
    R_RETURN(applet->display_layer_manager.WriteAppletCaptureBuffer(
        out_was_written, out_fbshare_layer_index, VI::CaptureKind::CallerApplet));
}

Result IDisplayController::ReleaseCallerAppletCaptureSharedBuffer() {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    R_SUCCEED();
}

Result IDisplayController::AcquireLastApplicationCaptureSharedBuffer(
    Out<bool> out_was_written, Out<s32> out_fbshare_layer_index) {
    LOG_DEBUG(Service_AM, "called");
    R_RETURN(applet->display_layer_manager.WriteAppletCaptureBuffer(
        out_was_written, out_fbshare_layer_index, VI::CaptureKind::LastApplication));
}

Result IDisplayController::ReleaseLastApplicationCaptureSharedBuffer() {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    R_SUCCEED();
}

} // namespace Service::AM
