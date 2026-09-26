// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::Audio {

class IFinalOutputRecorderManagerForApplet final
    : public ServiceFramework<IFinalOutputRecorderManagerForApplet> {
public:
    explicit IFinalOutputRecorderManagerForApplet(Core::System& system_);
    ~IFinalOutputRecorderManagerForApplet() override;

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, nullptr, "RequestSuspend"},
        FunctionInfo{1, nullptr, "RequestResume"}
    );
};

} // namespace Service::Audio
