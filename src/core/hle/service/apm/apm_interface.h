// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Service::APM {

class Controller;
class Module;

class APM final : public ServiceFramework<APM> {
public:
    explicit APM(Core::System& system_, std::shared_ptr<Module> apm_, Controller& controller_,
                 const char* name);
    ~APM() override;

private:
    void OpenSession(HLERequestContext& ctx);
    void GetPerformanceMode(HLERequestContext& ctx);
    void IsCpuOverclockEnabled(HLERequestContext& ctx);

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, &APM::OpenSession, "OpenSession"},
        FunctionInfo{1, &APM::GetPerformanceMode, "GetPerformanceMode"},
        FunctionInfo{6, &APM::IsCpuOverclockEnabled, "IsCpuOverclockEnabled"}
    );
    std::shared_ptr<Module> apm;
    Controller& controller;
};

class APM_Sys final : public ServiceFramework<APM_Sys> {
public:
    explicit APM_Sys(Core::System& system_, Controller& controller);
    ~APM_Sys() override;

    void SetCpuBoostMode(HLERequestContext& ctx);

private:
    void GetPerformanceEvent(HLERequestContext& ctx);
    void GetCurrentPerformanceConfiguration(HLERequestContext& ctx);

    Controller& controller;
};

} // namespace Service::APM
