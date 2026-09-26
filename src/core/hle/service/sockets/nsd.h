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

class NSD final : public ServiceFramework<NSD> {
public:
    explicit NSD(Core::System& system_, const char* name);
    ~NSD() override;

private:
    void SetChangeEnvironmentIdentifierDisabled(HLERequestContext& ctx);
    void Resolve(HLERequestContext& ctx);
    void ResolveEx(HLERequestContext& ctx);
    void GetEnvironmentIdentifier(HLERequestContext& ctx);
    void GetApplicationServerEnvironmentType(HLERequestContext& ctx);

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{5, nullptr, "GetSettingUrl"},
        FunctionInfo{10, nullptr, "GetSettingName"},
        FunctionInfo{11, &NSD::GetEnvironmentIdentifier, "GetEnvironmentIdentifier"},
        FunctionInfo{12, nullptr, "GetDeviceId"},
        FunctionInfo{13, nullptr, "DeleteSettings"},
        FunctionInfo{14, nullptr, "ImportSettings"},
        FunctionInfo{15, &NSD::SetChangeEnvironmentIdentifierDisabled, "SetChangeEnvironmentIdentifierDisabled"},
        FunctionInfo{20, &NSD::Resolve, "Resolve"},
        FunctionInfo{21, &NSD::ResolveEx, "ResolveEx"},
        FunctionInfo{30, nullptr, "GetNasServiceSetting"},
        FunctionInfo{31, nullptr, "GetNasServiceSettingEx"},
        FunctionInfo{40, nullptr, "GetNasRequestFqdn"},
        FunctionInfo{41, nullptr, "GetNasRequestFqdnEx"},
        FunctionInfo{42, nullptr, "GetNasApiFqdn"},
        FunctionInfo{43, nullptr, "GetNasApiFqdnEx"},
        FunctionInfo{50, nullptr, "GetCurrentSetting"},
        FunctionInfo{51, nullptr, "WriteTestParameter"},
        FunctionInfo{52, nullptr, "ReadTestParameter"},
        FunctionInfo{60, nullptr, "ReadSaveDataFromFsForTest"},
        FunctionInfo{61, nullptr, "WriteSaveDataToFsForTest"},
        FunctionInfo{62, nullptr, "DeleteSaveDataOfFsForTest"},
        FunctionInfo{63, nullptr, "IsChangeEnvironmentIdentifierDisabled"},
        FunctionInfo{64, nullptr, "SetWithoutDomainExchangeFqdns"},
        FunctionInfo{100, &NSD::GetApplicationServerEnvironmentType, "GetApplicationServerEnvironmentType"},
        FunctionInfo{101, nullptr, "SetApplicationServerEnvironmentType"},
        FunctionInfo{102, nullptr, "DeleteApplicationServerEnvironmentType"}
    );
};

} // namespace Service::Sockets
