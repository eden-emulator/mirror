// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::Capture {

class IScreenShotControlService final : public ServiceFramework<IScreenShotControlService> {
public:
    explicit IScreenShotControlService(Core::System& system_);
    ~IScreenShotControlService() override;

    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{1, nullptr, "CaptureRawImage"},
        FunctionInfo{2, nullptr, "CaptureRawImageWithTimeout"},
        FunctionInfo{3, nullptr, "AttachSharedBuffer"},
        FunctionInfo{5, nullptr, "CaptureRawImageToAttachedSharedBuffer"},
        FunctionInfo{210, nullptr, "Unknown210"},
        FunctionInfo{1001, nullptr, "RequestTakingScreenShot"},
        FunctionInfo{1002, nullptr, "RequestTakingScreenShotWithTimeout"},
        FunctionInfo{1003, nullptr, "RequestTakingScreenShotEx"},
        FunctionInfo{1004, nullptr, "RequestTakingScreenShotEx1"},
        FunctionInfo{1009, nullptr, "CancelTakingScreenShot"},
        FunctionInfo{1010, nullptr, "SetTakingScreenShotCancelState"},
        FunctionInfo{1011, nullptr, "NotifyTakingScreenShotRefused"},
        FunctionInfo{1012, nullptr, "NotifyTakingScreenShotFailed"},
        FunctionInfo{1101, nullptr, "SetupOverlayMovieThumbnail"},
        FunctionInfo{1106, nullptr, "Unknown1106"},
        FunctionInfo{1107, nullptr, "Unknown1107"},
        FunctionInfo{1201, nullptr, "OpenRawScreenShotReadStream"},
        FunctionInfo{1202, nullptr, "CloseRawScreenShotReadStream"},
        FunctionInfo{1203, nullptr, "ReadRawScreenShotReadStream"},
        FunctionInfo{1204, nullptr, "Unknown1204"}
    );
};

} // namespace Service::Capture
