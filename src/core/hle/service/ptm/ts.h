// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/common_types.h"
#include "core/hle/service/service.h"

namespace Service::PTM {

class TS final : public ServiceFramework<TS> {
public:
    explicit TS(Core::System& system_);
    ~TS() override;

private:
    void GetTemperature(HLERequestContext& ctx);
    void GetTemperatureMilliC(HLERequestContext& ctx);
    void OpenSession(HLERequestContext& ctx);

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static const auto functions = CreateStaticMap(
        FunctionInfo{0, nullptr, "GetTemperatureRange"},
        FunctionInfo{1, &TS::GetTemperature, "GetTemperature"},
        FunctionInfo{2, nullptr, "SetMeasurementMode"},
        FunctionInfo{3, &TS::GetTemperatureMilliC, "GetTemperatureMilliC"},
        FunctionInfo{4, &TS::OpenSession, "OpenSession"}
    );
};

} // namespace Service::PTM
