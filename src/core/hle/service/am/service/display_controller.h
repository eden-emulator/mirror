// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::AM {

struct Applet;

class IDisplayController final : public ServiceFramework<IDisplayController> {
public:
    explicit IDisplayController(Core::System& system_, std::shared_ptr<Applet> applet_);
    ~IDisplayController() override;

private:
    Result GetLastForegroundCaptureImageEx(Out<bool> out_was_written,
                                         OutBuffer<BufferAttr_HipcMapAlias> out_image_data);
    Result GetCallerAppletCaptureImageEx(Out<bool> out_was_written,
                                         OutBuffer<BufferAttr_HipcMapAlias> out_image_data);
    Result TakeScreenShotOfOwnLayer(bool unknown0, s32 fbshare_layer_index);
    Result ClearCaptureBuffer(bool unknown0, s32 fbshare_layer_index, u32 color);
    Result AcquireLastForegroundCaptureSharedBuffer(Out<bool> out_was_written,
                                                    Out<s32> out_fbshare_layer_index);
    Result ReleaseLastForegroundCaptureSharedBuffer();
    Result AcquireCallerAppletCaptureSharedBuffer(Out<bool> out_was_written,
                                                  Out<s32> out_fbshare_layer_index);
    Result ReleaseCallerAppletCaptureSharedBuffer();
    Result AcquireLastApplicationCaptureSharedBuffer(Out<bool> out_was_written,
                                                     Out<s32> out_fbshare_layer_index);
    Result ReleaseLastApplicationCaptureSharedBuffer();

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, nullptr, "GetLastForegroundCaptureImage"},
        FunctionInfo{1, nullptr, "UpdateLastForegroundCaptureImage"},
        FunctionInfo{2, nullptr, "GetLastApplicationCaptureImage"},
        FunctionInfo{3, nullptr, "GetCallerAppletCaptureImage"},
        FunctionInfo{4, nullptr, "UpdateCallerAppletCaptureImage"},
        FunctionInfo{5, D<&IDisplayController::GetLastForegroundCaptureImageEx>, "GetLastForegroundCaptureImageEx"},
        FunctionInfo{6, nullptr, "GetLastApplicationCaptureImageEx"},
        FunctionInfo{7, D<&IDisplayController::GetCallerAppletCaptureImageEx>, "GetCallerAppletCaptureImageEx"},
        FunctionInfo{8, D<&IDisplayController::TakeScreenShotOfOwnLayer>, "TakeScreenShotOfOwnLayer"},
        FunctionInfo{9, nullptr, "CopyBetweenCaptureBuffers"},
        FunctionInfo{10, nullptr, "AcquireLastApplicationCaptureBuffer"},
        FunctionInfo{11, nullptr, "ReleaseLastApplicationCaptureBuffer"},
        FunctionInfo{12, nullptr, "AcquireLastForegroundCaptureBuffer"},
        FunctionInfo{13, nullptr, "ReleaseLastForegroundCaptureBuffer"},
        FunctionInfo{14, nullptr, "AcquireCallerAppletCaptureBuffer"},
        FunctionInfo{15, nullptr, "ReleaseCallerAppletCaptureBuffer"},
        FunctionInfo{16, nullptr, "AcquireLastApplicationCaptureBufferEx"},
        FunctionInfo{17, nullptr, "AcquireLastForegroundCaptureBufferEx"},
        FunctionInfo{18, nullptr, "AcquireCallerAppletCaptureBufferEx"},
        FunctionInfo{20, D<&IDisplayController::ClearCaptureBuffer>, "ClearCaptureBuffer"},
        FunctionInfo{21, nullptr, "ClearAppletTransitionBuffer"},
        FunctionInfo{22, D<&IDisplayController::AcquireLastApplicationCaptureSharedBuffer>, "AcquireLastApplicationCaptureSharedBuffer"},
        FunctionInfo{23, D<&IDisplayController::ReleaseLastApplicationCaptureSharedBuffer>, "ReleaseLastApplicationCaptureSharedBuffer"},
        FunctionInfo{24, D<&IDisplayController::AcquireLastForegroundCaptureSharedBuffer>, "AcquireLastForegroundCaptureSharedBuffer"},
        FunctionInfo{25, D<&IDisplayController::ReleaseLastForegroundCaptureSharedBuffer>, "ReleaseLastForegroundCaptureSharedBuffer"},
        FunctionInfo{26, D<&IDisplayController::AcquireCallerAppletCaptureSharedBuffer>, "AcquireCallerAppletCaptureSharedBuffer"},
        FunctionInfo{27, D<&IDisplayController::ReleaseCallerAppletCaptureSharedBuffer>, "ReleaseCallerAppletCaptureSharedBuffer"},
        FunctionInfo{28, nullptr, "TakeScreenShotOfOwnLayerEx"}
    );
    const std::shared_ptr<Applet> applet;
};

} // namespace Service::AM
