// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/os/event.h"
#include "core/hle/service/service.h"

namespace Service::AM {

class ICradleFirmwareUpdater;

class IGlobalStateController final : public ServiceFramework<IGlobalStateController> {
public:
    explicit IGlobalStateController(Core::System& system_);
    ~IGlobalStateController() override;

private:
    Result StartShutdownSequence();
    Result StartRebootSequence();
    Result LoadAndApplyIdlePolicySettings();
    Result ShouldSleepOnBoot(Out<bool> out_should_sleep_on_boot);
    Result GetHdcpAuthenticationFailedEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result OpenCradleFirmwareUpdater(Out<SharedPointer<ICradleFirmwareUpdater>> out_cradle_firmware_updater);

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, nullptr, "RequestToEnterSleep"},
        FunctionInfo{1, nullptr, "EnterSleep"},
        FunctionInfo{2, nullptr, "StartSleepSequence"},
        FunctionInfo{3, D<&IGlobalStateController::StartShutdownSequence>, "StartShutdownSequence"},
        FunctionInfo{4, D<&IGlobalStateController::StartRebootSequence>, "StartRebootSequence"},
        FunctionInfo{9, nullptr, "IsAutoPowerDownRequested"},
        FunctionInfo{10, D<&IGlobalStateController::LoadAndApplyIdlePolicySettings>, "LoadAndApplyIdlePolicySettings"},
        FunctionInfo{11, nullptr, "NotifyCecSettingsChanged"},
        FunctionInfo{12, nullptr, "SetDefaultHomeButtonLongPressTime"},
        FunctionInfo{13, nullptr, "UpdateDefaultDisplayResolution"},
        FunctionInfo{14, D<&IGlobalStateController::ShouldSleepOnBoot>, "ShouldSleepOnBoot"},
        FunctionInfo{15, D<&IGlobalStateController::GetHdcpAuthenticationFailedEvent>, "GetHdcpAuthenticationFailedEvent"},
        FunctionInfo{30, D<&IGlobalStateController::OpenCradleFirmwareUpdater>, "OpenCradleFirmwareUpdater"}
    );
    KernelHelpers::ServiceContext m_context;
    Event m_hdcp_authentication_failed_event;
};

} // namespace Service::AM
