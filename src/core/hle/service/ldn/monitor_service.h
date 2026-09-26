// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/ldn/ldn_types.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::LDN {

class IMonitorService final : public ServiceFramework<IMonitorService> {
public:
    explicit IMonitorService(Core::System& system_);
    ~IMonitorService() override;

private:
    Result GetStateForMonitor(Out<State> out_state);
    Result InitializeMonitor();
    Result FinalizeMonitor();

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, C<&IMonitorService::GetStateForMonitor>, "GetStateForMonitor"},
        FunctionInfo{1, nullptr, "GetNetworkInfoForMonitor"},
        FunctionInfo{2, nullptr, "GetIpv4AddressForMonitor"},
        FunctionInfo{3, nullptr, "GetDisconnectReasonForMonitor"},
        FunctionInfo{4, nullptr, "GetSecurityParameterForMonitor"},
        FunctionInfo{5, nullptr, "GetNetworkConfigForMonitor"},
        FunctionInfo{100, C<&IMonitorService::InitializeMonitor>, "InitializeMonitor"},
        FunctionInfo{101, C<&IMonitorService::FinalizeMonitor>, "FinalizeMonitor"}
    );
    State state{State::None};
};

} // namespace Service::LDN
