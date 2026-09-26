// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::OMM {

class IOperationModeManager final : public ServiceFramework<IOperationModeManager> {
public:
    explicit IOperationModeManager(Core::System& system_);
    ~IOperationModeManager() override;

    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, nullptr, "GetOperationMode"},
        FunctionInfo{1, nullptr, "GetOperationModeChangeEvent"},
        FunctionInfo{2, nullptr, "EnableAudioVisual"},
        FunctionInfo{3, nullptr, "DisableAudioVisual"},
        FunctionInfo{4, nullptr, "EnterSleepAndWait"},
        FunctionInfo{5, nullptr, "GetCradleStatus"},
        FunctionInfo{6, nullptr, "FadeInDisplay"},
        FunctionInfo{7, nullptr, "FadeOutDisplay"},
        FunctionInfo{8, nullptr, "GetCradleFwVersion"},
        FunctionInfo{9, nullptr, "NotifyCecSettingsChanged"},
        FunctionInfo{10, nullptr, "SetOperationModePolicy"},
        FunctionInfo{11, nullptr, "GetDefaultDisplayResolution"},
        FunctionInfo{12, nullptr, "GetDefaultDisplayResolutionChangeEvent"},
        FunctionInfo{13, nullptr, "UpdateDefaultDisplayResolution"},
        FunctionInfo{14, nullptr, "ShouldSleepOnBoot"},
        FunctionInfo{15, nullptr, "NotifyHdcpApplicationExecutionStarted"},
        FunctionInfo{16, nullptr, "NotifyHdcpApplicationExecutionFinished"},
        FunctionInfo{17, nullptr, "NotifyHdcpApplicationDrawingStarted"},
        FunctionInfo{18, nullptr, "NotifyHdcpApplicationDrawingFinished"},
        FunctionInfo{19, nullptr, "GetHdcpAuthenticationFailedEvent"},
        FunctionInfo{20, nullptr, "GetHdcpAuthenticationFailedEmulationEnabled"},
        FunctionInfo{21, nullptr, "SetHdcpAuthenticationFailedEmulation"},
        FunctionInfo{22, nullptr, "GetHdcpStateChangeEvent"},
        FunctionInfo{23, nullptr, "GetHdcpState"},
        FunctionInfo{24, nullptr, "ShowCardUpdateProcessing"},
        FunctionInfo{25, nullptr, "SetApplicationCecSettingsAndNotifyChanged"},
        FunctionInfo{26, nullptr, "GetOperationModeSystemInfo"},
        FunctionInfo{27, nullptr, "GetAppletFullAwakingSystemEvent"},
        FunctionInfo{28, nullptr, "CreateCradleFirmwareUpdater"}
    );
};

} // namespace Service::OMM
