// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/core.h"
#include "core/file_sys/common_funcs.h"
#include "core/hle/service/am/applet.h"
#include "core/hle/service/am/service/applet_common_functions.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::AM {

IAppletCommonFunctions::IAppletCommonFunctions(Core::System& system_,
                                               std::shared_ptr<Applet> applet_)
    : ServiceFramework{system_, "IAppletCommonFunctions"}, applet{std::move(applet_)} {
}

IAppletCommonFunctions::~IAppletCommonFunctions() = default;

Result IAppletCommonFunctions::SetHomeButtonDoubleClickEnabled(
    bool home_button_double_click_enabled) {
    LOG_WARNING(Service_AM, "(STUBBED) called, home_button_double_click_enabled={}",
                home_button_double_click_enabled);
    R_SUCCEED();
}

Result IAppletCommonFunctions::GetHomeButtonDoubleClickEnabled(
    Out<bool> out_home_button_double_click_enabled) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    *out_home_button_double_click_enabled = false;
    R_SUCCEED();
}

Result IAppletCommonFunctions::SetDisplayMagnification(f32 x, f32 y, f32 width, f32 height) {
    LOG_DEBUG(Service_AM, "(STUBBED) called, x={}, y={}, width={}, height={}", x, y, width,
                height);
    std::scoped_lock lk{applet->lock};
    applet->display_magnification = Common::Rectangle<f32>{x, y, x + width, y + height};
    R_SUCCEED();
}

Result IAppletCommonFunctions::SetCpuBoostRequestPriority(s32 priority) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    std::scoped_lock lk{applet->lock};
    applet->cpu_boost_request_priority = priority;
    R_SUCCEED();
}

Result IAppletCommonFunctions::GetCurrentApplicationId(Out<u64> out_application_id) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    *out_application_id = FileSys::GetBaseTitleID(system.GetApplicationProcessProgramID());
    R_SUCCEED();
}

Result IAppletCommonFunctions::SetGpuTimeSliceBoost(s64 time_span) {
    LOG_WARNING(Service_AM, "(STUBBED) called, time_span={}", time_span);
    R_SUCCEED();
}

Result IAppletCommonFunctions::Unknown350(Out<u16> out_unknown) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    *out_unknown = 0;
    R_SUCCEED();
}

} // namespace Service::AM
