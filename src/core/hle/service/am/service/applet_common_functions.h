// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::AM {

struct Applet;

class IAppletCommonFunctions final : public ServiceFramework<IAppletCommonFunctions> {
public:
    explicit IAppletCommonFunctions(Core::System& system_, std::shared_ptr<Applet> applet_);
    ~IAppletCommonFunctions() override;

private:
    Result SetHomeButtonDoubleClickEnabled(bool home_button_double_click_enabled);
    Result GetHomeButtonDoubleClickEnabled(Out<bool> out_home_button_double_click_enabled);
    Result SetDisplayMagnification(f32 x, f32 y, f32 width, f32 height);
    Result SetCpuBoostRequestPriority(s32 priority);
    Result GetCurrentApplicationId(Out<u64> out_application_id);
    Result SetGpuTimeSliceBoost(s64 time_span);
    Result Unknown350(Out<u16> out_unknown);

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, nullptr, "SetTerminateResult"},
        FunctionInfo{10, nullptr, "ReadThemeStorage"},
        FunctionInfo{11, nullptr, "WriteThemeStorage"},
        FunctionInfo{20, nullptr, "PushToAppletBoundChannel"},
        FunctionInfo{21, nullptr, "TryPopFromAppletBoundChannel"},
        FunctionInfo{40, nullptr, "GetDisplayLogicalResolution"},
        FunctionInfo{42, D<&IAppletCommonFunctions::SetDisplayMagnification>, "SetDisplayMagnification"},
        FunctionInfo{50, D<&IAppletCommonFunctions::SetHomeButtonDoubleClickEnabled>, "SetHomeButtonDoubleClickEnabled"},
        FunctionInfo{51, D<&IAppletCommonFunctions::GetHomeButtonDoubleClickEnabled>, "GetHomeButtonDoubleClickEnabled"},
        FunctionInfo{52, nullptr, "IsHomeButtonShortPressedBlocked"},
        FunctionInfo{60, nullptr, "IsVrModeCurtainRequired"},
        FunctionInfo{61, nullptr, "IsSleepRequiredByHighTemperature"},
        FunctionInfo{62, nullptr, "IsSleepRequiredByLowBattery"},
        FunctionInfo{70, D<&IAppletCommonFunctions::SetCpuBoostRequestPriority>, "SetCpuBoostRequestPriority"},
        FunctionInfo{80, nullptr, "SetHandlingCaptureButtonShortPressedMessageEnabledForApplet"},
        FunctionInfo{81, nullptr, "SetHandlingCaptureButtonLongPressedMessageEnabledForApplet"},
        FunctionInfo{90, nullptr, "OpenNamedChannelAsParent"},
        FunctionInfo{91, nullptr, "OpenNamedChannelAsChild"},
        FunctionInfo{100, nullptr, "SetApplicationCoreUsageMode"},
        FunctionInfo{300, D<&IAppletCommonFunctions::GetCurrentApplicationId>, "GetCurrentApplicationId"},
        FunctionInfo{310, nullptr, "IsSystemAppletHomeMenu"}, //19.0.0+
        FunctionInfo{320, D<&IAppletCommonFunctions::SetGpuTimeSliceBoost>, "SetGpuTimeSliceBoost"}, //19.0.0+
        FunctionInfo{321, nullptr, "SetGpuTimeSliceBoostDueToApplication"}, //19.0.0+
        FunctionInfo{350, D<&IAppletCommonFunctions::Unknown350>, "Unknown350"} //20.0.0+
    );
    const std::shared_ptr<Applet> applet;
};

} // namespace Service::AM
