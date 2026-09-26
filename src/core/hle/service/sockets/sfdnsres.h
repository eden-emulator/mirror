// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::Sockets {

class SFDNSRES final : public ServiceFramework<SFDNSRES> {
public:
    explicit SFDNSRES(Core::System& system_);
    ~SFDNSRES() override;

private:
    void GetHostByNameRequest(HLERequestContext& ctx);
    void GetGaiStringErrorRequest(HLERequestContext& ctx);
    void GetHostByNameRequestWithOptions(HLERequestContext& ctx);
    void GetAddrInfoRequest(HLERequestContext& ctx);
    void GetAddrInfoRequestWithOptions(HLERequestContext& ctx);
    void ResolverSetOptionRequest(HLERequestContext& ctx);

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, nullptr, "SetDnsAddressesPrivateRequest"},
        FunctionInfo{1, nullptr, "GetDnsAddressPrivateRequest"},
        FunctionInfo{2, &SFDNSRES::GetHostByNameRequest, "GetHostByNameRequest"},
        FunctionInfo{3, nullptr, "GetHostByAddrRequest"},
        FunctionInfo{4, nullptr, "GetHostStringErrorRequest"},
        FunctionInfo{5, &SFDNSRES::GetGaiStringErrorRequest, "GetGaiStringErrorRequest"},
        FunctionInfo{6, &SFDNSRES::GetAddrInfoRequest, "GetAddrInfoRequest"},
        FunctionInfo{7, nullptr, "GetNameInfoRequest"},
        FunctionInfo{8, nullptr, "RequestCancelHandleRequest"},
        FunctionInfo{9, nullptr, "CancelRequest"},
        FunctionInfo{10, &SFDNSRES::GetHostByNameRequestWithOptions, "GetHostByNameRequestWithOptions"},
        FunctionInfo{11, nullptr, "GetHostByAddrRequestWithOptions"},
        FunctionInfo{12, &SFDNSRES::GetAddrInfoRequestWithOptions, "GetAddrInfoRequestWithOptions"},
        FunctionInfo{13, nullptr, "GetNameInfoRequestWithOptions"},
        FunctionInfo{14, &SFDNSRES::ResolverSetOptionRequest, "ResolverSetOptionRequest"},
        FunctionInfo{15, nullptr, "ResolverGetOptionRequest"}
    );
};

class DNS_PRIV final : public ServiceFramework<DNS_PRIV> {
public:
    explicit DNS_PRIV(Core::System& system_);
    ~DNS_PRIV() override;

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, nullptr, "Cmd0"},
        FunctionInfo{1, nullptr, "Cmd1"},
        FunctionInfo{2, nullptr, "Cmd2"}
    );
};

} // namespace Service::Sockets
