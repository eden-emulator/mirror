// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Service::Glue {

class ARPManager;
class IRegistrar;

class ARP_R final : public ServiceFramework<ARP_R> {
public:
    explicit ARP_R(Core::System& system_, const ARPManager& manager_);
    ~ARP_R() override;

private:
    void GetApplicationLaunchProperty(HLERequestContext& ctx);
    void GetApplicationLaunchPropertyWithApplicationId(HLERequestContext& ctx);
    void GetApplicationControlProperty(HLERequestContext& ctx);
    void GetApplicationControlPropertyWithApplicationId(HLERequestContext& ctx);

    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, &ARP_R::GetApplicationLaunchProperty, "GetApplicationLaunchProperty"},
        FunctionInfo{1, &ARP_R::GetApplicationLaunchPropertyWithApplicationId, "GetApplicationLaunchPropertyWithApplicationId"},
        FunctionInfo{2, &ARP_R::GetApplicationControlProperty, "GetApplicationControlProperty"},
        FunctionInfo{3, &ARP_R::GetApplicationControlPropertyWithApplicationId, "GetApplicationControlPropertyWithApplicationId"},
        FunctionInfo{4, nullptr, "GetApplicationInstanceUnregistrationNotifier"},
        FunctionInfo{5, nullptr, "ListApplicationInstanceId"},
        FunctionInfo{6, nullptr, "GetMicroApplicationInstanceId"},
        FunctionInfo{7, nullptr, "GetApplicationCertificate"},
        FunctionInfo{9998, nullptr, "GetPreomiaApplicationLaunchProperty"},
        FunctionInfo{9999, nullptr, "GetPreomiaApplicationControlProperty"}
    );
    const ARPManager& manager;
};

class ARP_W final : public ServiceFramework<ARP_W> {
public:
    explicit ARP_W(Core::System& system_, ARPManager& manager_);
    ~ARP_W() override;

private:
    void AcquireRegistrar(HLERequestContext& ctx);
    void UnregisterApplicationInstance(HLERequestContext& ctx);

    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, &ARP_W::AcquireRegistrar, "AcquireRegistrar"},
        FunctionInfo{1, &ARP_W::UnregisterApplicationInstance , "UnregisterApplicationInstance "},
        FunctionInfo{2, nullptr, "AcquireUpdater"}
    );
    ARPManager& manager;
    std::shared_ptr<IRegistrar> registrar;
};

} // namespace Service::Glue
