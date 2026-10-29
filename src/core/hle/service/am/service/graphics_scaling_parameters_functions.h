// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::AM {

struct Applet;

struct GraphicsScalingParameters {
    u32 graphics_scale;
    u32 screen_ratio_numerator;
    u32 screen_ratio_denominator;
};
static_assert(sizeof(GraphicsScalingParameters) == 12);

class IGraphicsScalingParametersFunctions final
    : public ServiceFramework<IGraphicsScalingParametersFunctions> {
public:
    explicit IGraphicsScalingParametersFunctions(Core::System& system,
                                                 std::shared_ptr<Applet> applet);
    ~IGraphicsScalingParametersFunctions() override;

private:
    Result GetGraphicsScalingParameters(Out<GraphicsScalingParameters> out_parameters);
    Result SetGraphicsScaleChangedNotificationEnabled(bool enabled);

    const std::shared_ptr<Applet> m_applet;
};

} // namespace Service::AM
