// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Service::AM {

class IDebugFunctions final : public ServiceFramework<IDebugFunctions> {
public:
    explicit IDebugFunctions(Core::System& system_);
    ~IDebugFunctions() override;

    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, nullptr, "NotifyMessageToHomeMenuForDebug"},
        FunctionInfo{1, nullptr, "OpenMainApplication"},
        FunctionInfo{10, nullptr, "PerformSystemButtonPressing"},
        FunctionInfo{20, nullptr, "InvalidateTransitionLayer"},
        FunctionInfo{30, nullptr, "RequestLaunchApplicationWithUserAndArgumentForDebug"},
        FunctionInfo{31, nullptr, "RequestLaunchApplicationByApplicationLaunchInfoForDebug"},
        FunctionInfo{40, nullptr, "GetAppletResourceUsageInfo"},
        FunctionInfo{50, nullptr, "AddSystemProgramIdAndAppletIdForDebug"},
        FunctionInfo{51, nullptr, "AddOperationConfirmedLibraryAppletIdForDebug"},
        FunctionInfo{100, nullptr, "SetCpuBoostModeForApplet"},
        FunctionInfo{101, nullptr, "CancelCpuBoostModeForApplet"},
        FunctionInfo{110, nullptr, "PushToAppletBoundChannelForDebug"},
        FunctionInfo{111, nullptr, "TryPopFromAppletBoundChannelForDebug"},
        FunctionInfo{120, nullptr, "AlarmSettingNotificationEnableAppEventReserve"},
        FunctionInfo{121, nullptr, "AlarmSettingNotificationDisableAppEventReserve"},
        FunctionInfo{122, nullptr, "AlarmSettingNotificationPushAppEventNotify"},
        FunctionInfo{130, nullptr, "FriendInvitationSetApplicationParameter"},
        FunctionInfo{131, nullptr, "FriendInvitationClearApplicationParameter"},
        FunctionInfo{132, nullptr, "FriendInvitationPushApplicationParameter"},
        FunctionInfo{140, nullptr, "RestrictPowerOperationForSecureLaunchModeForDebug"},
        FunctionInfo{200, nullptr, "CreateFloatingLibraryAppletAccepterForDebug"},
        FunctionInfo{300, nullptr, "TerminateAllRunningApplicationsForDebug"},
        FunctionInfo{900, nullptr, "GetGrcProcessLaunchedSystemEvent"}
    );
};

} // namespace Service::AM
