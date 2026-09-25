// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/hle/service/ldn/sf_service.h"

namespace Service::LDN {

ISfService::ISfService(Core::System& system_) : ServiceFramework{system_, "ISfService"} {
    // clang-format off
    static const FunctionInfo functions[] = {
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
            FunctionInfo{1560, nullptr, "ClearAcceptableGroupId"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

ISfService::~ISfService() = default;

} // namespace Service::LDN
