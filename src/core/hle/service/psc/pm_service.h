// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::PSC {

class IPmModule;

class IPmService final : public ServiceFramework<IPmService> {
public:
    explicit IPmService(Core::System& system_);
    ~IPmService() override;

private:
    Result GetPmModule(Out<SharedPointer<IPmModule>> out_module);

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, D<&IPmService::GetPmModule>, "GetPmModule"}
    );
};

} // namespace Service::PSC
