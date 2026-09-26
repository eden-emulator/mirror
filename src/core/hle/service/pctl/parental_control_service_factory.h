// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/pctl/pctl_types.h"
#include "core/hle/service/service.h"

namespace Service::PCTL {

class IParentalControlService;

class IParentalControlServiceFactory : public ServiceFramework<IParentalControlServiceFactory> {
public:
    explicit IParentalControlServiceFactory(Core::System& system_, const char* name_,
                                            Capability capability_);
    ~IParentalControlServiceFactory() override;

    Result CreateService(Out<SharedPointer<IParentalControlService>> out_service,
                         ClientProcessId process_id);
    Result CreateServiceWithoutInitialize(Out<SharedPointer<IParentalControlService>> out_service,
                                          ClientProcessId process_id);

private:
    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        {0, D<&IParentalControlServiceFactory::CreateService>, "CreateService"},
        {1, D<&IParentalControlServiceFactory::CreateServiceWithoutInitialize>, "CreateServiceWithoutInitialize"}
    );
    Capability capability{};
};

void LoopProcess(Core::System& system);

} // namespace Service::PCTL
