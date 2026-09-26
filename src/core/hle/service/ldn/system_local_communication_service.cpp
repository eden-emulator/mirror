// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ldn/system_local_communication_service.h"

namespace Service::LDN {

ServiceFrameworkBase::FunctionInfoBase const* ISystemLocalCommunicationService::FindRequest(u32 key) {
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, nullptr, "GetState"},
        FunctionInfo{1, nullptr, "GetNetworkInfo"},
        FunctionInfo{2, nullptr, "GetIpv4Address"},
        FunctionInfo{3, nullptr, "GetDisconnectReason"},
        FunctionInfo{4, nullptr, "GetSecurityParameter"},
        FunctionInfo{5, nullptr, "GetNetworkConfig"},
        FunctionInfo{100, nullptr, "AttachStateChangeEvent"},
        FunctionInfo{101, nullptr, "GetNetworkInfoLatestUpdate"},
        FunctionInfo{102, nullptr, "Scan"},
        FunctionInfo{103, nullptr, "ScanPrivate"},
        FunctionInfo{104, nullptr, "SetWirelessControllerRestriction"},
        FunctionInfo{200, nullptr, "OpenAccessPoint"},
        FunctionInfo{201, nullptr, "CloseAccessPoint"},
        FunctionInfo{202, nullptr, "CreateNetwork"},
        FunctionInfo{203, nullptr, "CreateNetworkPrivate"},
        FunctionInfo{204, nullptr, "DestroyNetwork"},
        FunctionInfo{205, nullptr, "Reject"},
        FunctionInfo{206, nullptr, "SetAdvertiseData"},
        FunctionInfo{207, nullptr, "SetStationAcceptPolicy"},
        FunctionInfo{208, nullptr, "AddAcceptFilterEntry"},
        FunctionInfo{209, nullptr, "ClearAcceptFilter"},
        FunctionInfo{300, nullptr, "OpenStation"},
        FunctionInfo{301, nullptr, "CloseStation"},
        FunctionInfo{302, nullptr, "Connect"},
        FunctionInfo{303, nullptr, "ConnectPrivate"},
        FunctionInfo{304, nullptr, "Disconnect"},
        FunctionInfo{400, nullptr, "InitializeSystem"},
        FunctionInfo{401, nullptr, "FinalizeSystem"},
        FunctionInfo{402, nullptr, "SetOperationMode"},
        FunctionInfo{403, C<&ISystemLocalCommunicationService::InitializeSystem2>, "InitializeSystem2"}
    );
    return HandlerTableGenerateWithFind(key, functions);
}

ISystemLocalCommunicationService::ISystemLocalCommunicationService(Core::System& system_)
    : ServiceFramework{system_, "ISystemLocalCommunicationService"} {
}

ISystemLocalCommunicationService::~ISystemLocalCommunicationService() = default;

Result ISystemLocalCommunicationService::InitializeSystem2() {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    R_SUCCEED();
}

} // namespace Service::LDN
