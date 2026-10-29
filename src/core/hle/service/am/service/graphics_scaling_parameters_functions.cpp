// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/hle/service/am/applet.h"
#include "core/hle/service/am/service/graphics_scaling_parameters_functions.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::AM {

IGraphicsScalingParametersFunctions::IGraphicsScalingParametersFunctions(
    Core::System& system, std::shared_ptr<Applet> applet)
    : ServiceFramework{system, "IGraphicsScalingParametersFunctions"}, m_applet{std::move(applet)} {
    static const FunctionInfo functions[] = {
        {10, D<&IGraphicsScalingParametersFunctions::GetGraphicsScalingParameters>,
         "GetGraphicsScalingParameters"},
        {1000, nullptr, "SetSupportingGraphicsScalingParametersForDebug"},
        {1010, nullptr, "SetGpuQuotaForDebug"},
        {1020, nullptr, "ClearGpuQuotaForDebug"},
        {1030, nullptr, "SetScreenRatioForDebug"},
        {1040, nullptr, "ClearScreenRatioForDebug"},
        {1050, D<&IGraphicsScalingParametersFunctions::SetGraphicsScaleChangedNotificationEnabled>,
         "SetGraphicsScaleChangedNotificationEnabled"},
    };
    RegisterHandlers(functions);
}

IGraphicsScalingParametersFunctions::~IGraphicsScalingParametersFunctions() = default;

Result IGraphicsScalingParametersFunctions::GetGraphicsScalingParameters(
    Out<GraphicsScalingParameters> out_parameters) {
    // Prototype policy: lowest SDK scale (10 maps to public scale 0), unit screen ratio.
    // The ratio field interpretation is inferred from the adjacent debug API.
    *out_parameters = {10, 1, 1};
    LOG_DEBUG(Service_AM, "called, prototype scale=10, screen ratio=1/1");
    R_SUCCEED();
}

Result IGraphicsScalingParametersFunctions::SetGraphicsScaleChangedNotificationEnabled(bool enabled) {
    LOG_DEBUG(Service_AM, "called, enabled={}", enabled);
    std::scoped_lock lk{m_applet->lock};
    m_applet->graphics_scale_changed_notification_enabled = enabled;
    R_SUCCEED();
}

} // namespace Service::AM
