// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Service::AM {
struct Applet;

class IOverlayFunctions final : public ServiceFramework<IOverlayFunctions> {
public:
    explicit IOverlayFunctions(Core::System &system_, std::shared_ptr<Applet> applet);
    ~IOverlayFunctions() override;

private:
    Result BeginToWatchShortHomeButtonMessage();
    Result EndToWatchShortHomeButtonMessage();
    Result GetApplicationIdForLogo(Out<u64> out_application_id);
    Result SetAutoSleepTimeAndDimmingTimeEnabled(bool enabled);
    Result IsHealthWarningRequired(Out<bool> is_required);
    Result SetHandlingHomeButtonShortPressedEnabled(bool enabled);
    Result SetHandlingTouchScreenInputEnabled(bool enabled);
    Result Unknown70();

private:
    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, D<&IOverlayFunctions::BeginToWatchShortHomeButtonMessage>, "BeginToWatchShortHomeButtonMessage"},
        FunctionInfo{1, D<&IOverlayFunctions::EndToWatchShortHomeButtonMessage>, "EndToWatchShortHomeButtonMessage"},
        FunctionInfo{2, D<&IOverlayFunctions::GetApplicationIdForLogo>, "GetApplicationIdForLogo"},
        FunctionInfo{3, nullptr, "SetGpuTimeSliceBoost"},
        FunctionInfo{4, D<&IOverlayFunctions::SetAutoSleepTimeAndDimmingTimeEnabled>, "SetAutoSleepTimeAndDimmingTimeEnabled"},
        FunctionInfo{5, nullptr, "TerminateApplicationAndSetReason"},
        FunctionInfo{6, nullptr, "SetScreenShotPermissionGlobally"},
        FunctionInfo{10, nullptr, "StartShutdownSequenceForOverlay"},
        FunctionInfo{11, nullptr, "StartRebootSequenceForOverlay"},
        FunctionInfo{20, D<&IOverlayFunctions::SetHandlingHomeButtonShortPressedEnabled>, "SetHandlingHomeButtonShortPressedEnabled"},
        FunctionInfo{21, D<&IOverlayFunctions::SetHandlingTouchScreenInputEnabled>, "SetHandlingTouchScreenInputEnabled"},
        FunctionInfo{30, nullptr, "SetHealthWarningShowingState"},
        FunctionInfo{31, D<&IOverlayFunctions::IsHealthWarningRequired>, "IsHealthWarningRequired"},
        FunctionInfo{40, nullptr, "GetApplicationNintendoLogo"},
        FunctionInfo{41, nullptr, "GetApplicationStartupMovie"},
        FunctionInfo{50, nullptr, "SetGpuTimeSliceBoostForApplication"},
        FunctionInfo{60, nullptr, "Unknown60"},
        FunctionInfo{70, D<&IOverlayFunctions::Unknown70>, "Unknown70"},
        FunctionInfo{90, nullptr, "SetRequiresGpuResourceUse"},
        FunctionInfo{101, nullptr, "BeginToObserveHidInputForDevelop"}
    );
    const std::shared_ptr<Applet> m_applet;
};
} // namespace Service::AM
