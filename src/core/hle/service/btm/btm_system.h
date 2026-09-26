// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::BTM {
class IBtmSystemCore;

class IBtmSystem final : public ServiceFramework<IBtmSystem> {
public:
    explicit IBtmSystem(Core::System& system_);
    ~IBtmSystem() override;

private:
    Result GetCore(OutInterface<IBtmSystemCore> out_interface);

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, C<&IBtmSystem::GetCore>, "GetCore"}
    );
};

} // namespace Service::BTM
