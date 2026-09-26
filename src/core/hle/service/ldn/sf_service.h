// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::LDN {

class ISfService final : public ServiceFramework<ISfService> {
public:
    explicit ISfService(Core::System& system_);
    ~ISfService() override;

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static const auto functions = CreateStaticMap(
        FunctionInfo{0, nullptr, "Initialize"},
        FunctionInfo{256, nullptr, "AttachNetworkInterfaceStateChangeEvent"},
        FunctionInfo{264, nullptr, "GetNetworkInterfaceLastError"},
        FunctionInfo{272, nullptr, "GetRole"},
        FunctionInfo{280, nullptr, "GetAdvertiseData"},
        FunctionInfo{288, nullptr, "GetGroupInfo"},
        FunctionInfo{296, nullptr, "GetGroupInfo2"},
        FunctionInfo{304, nullptr, "GetGroupOwner"},
        FunctionInfo{312, nullptr, "GetIpConfig"},
        FunctionInfo{320, nullptr, "GetLinkLevel"},
        FunctionInfo{512, nullptr, "Scan"},
        FunctionInfo{768, nullptr, "CreateGroup"},
        FunctionInfo{776, nullptr, "DestroyGroup"},
        FunctionInfo{784, nullptr, "SetAdvertiseData"},
        FunctionInfo{1536, nullptr, "SendToOtherGroup"},
        FunctionInfo{1544, nullptr, "RecvFromOtherGroup"},
        FunctionInfo{1552, nullptr, "AddAcceptableGroupId"},
        FunctionInfo{1560, nullptr, "ClearAcceptableGroupId"}
    );
};

} // namespace Service::LDN
