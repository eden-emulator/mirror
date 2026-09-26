// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Service::NS {

class ISystemUpdateControl final : public ServiceFramework<ISystemUpdateControl> {
public:
    explicit ISystemUpdateControl(Core::System& system_);
    ~ISystemUpdateControl() override;

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, nullptr, "HasDownloaded"},
        FunctionInfo{1, nullptr, "RequestCheckLatestUpdate"},
        FunctionInfo{2, nullptr, "RequestDownloadLatestUpdate"},
        FunctionInfo{3, nullptr, "GetDownloadProgress"},
        FunctionInfo{4, nullptr, "ApplyDownloadedUpdate"},
        FunctionInfo{5, nullptr, "RequestPrepareCardUpdate"},
        FunctionInfo{6, nullptr, "GetPrepareCardUpdateProgress"},
        FunctionInfo{7, nullptr, "HasPreparedCardUpdate"},
        FunctionInfo{8, nullptr, "ApplyCardUpdate"},
        FunctionInfo{9, nullptr, "GetDownloadedEulaDataSize"},
        FunctionInfo{10, nullptr, "GetDownloadedEulaData"},
        FunctionInfo{11, nullptr, "SetupCardUpdate"},
        FunctionInfo{12, nullptr, "GetPreparedCardUpdateEulaDataSize"},
        FunctionInfo{13, nullptr, "GetPreparedCardUpdateEulaData"},
        FunctionInfo{14, nullptr, "SetupCardUpdateViaSystemUpdater"},
        FunctionInfo{15, nullptr, "HasReceived"},
        FunctionInfo{16, nullptr, "RequestReceiveSystemUpdate"},
        FunctionInfo{17, nullptr, "GetReceiveProgress"},
        FunctionInfo{18, nullptr, "ApplyReceivedUpdate"},
        FunctionInfo{19, nullptr, "GetReceivedEulaDataSize"},
        FunctionInfo{20, nullptr, "GetReceivedEulaData"},
        FunctionInfo{21, nullptr, "SetupToReceiveSystemUpdate"},
        FunctionInfo{22, nullptr, "RequestCheckLatestUpdateIncludesRebootlessUpdate"}
    );
};

} // namespace Service::NS
