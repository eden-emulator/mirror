// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ns/system_update_control.h"
#include "core/hle/service/ns/system_update_interface.h"

namespace Service::NS {

ServiceFrameworkBase::FunctionInfoBase const* ISystemUpdateInterface::FindRequest(u32 key) {
    static const auto functions = CreateStaticMap(
        FunctionInfo{0, D<&ISystemUpdateInterface::GetBackgroundNetworkUpdateState>, "GetBackgroundNetworkUpdateState"},
        FunctionInfo{1, D<&ISystemUpdateInterface::OpenSystemUpdateControl>, "OpenSystemUpdateControl"},
        FunctionInfo{2, nullptr, "NotifyExFatDriverRequired"},
        FunctionInfo{3, nullptr, "ClearExFatDriverStatusForDebug"},
        FunctionInfo{4, nullptr, "RequestBackgroundNetworkUpdate"},
        FunctionInfo{5, nullptr, "NotifyBackgroundNetworkUpdate"},
        FunctionInfo{6, nullptr, "NotifyExFatDriverDownloadedForDebug"},
        FunctionInfo{9, D<&ISystemUpdateInterface::GetSystemUpdateNotificationEventForContentDelivery>, "GetSystemUpdateNotificationEventForContentDelivery"},
        FunctionInfo{10, nullptr, "NotifySystemUpdateForContentDelivery"},
        FunctionInfo{11, nullptr, "PrepareShutdown"},
        FunctionInfo{12, nullptr, "Unknown12"},
        FunctionInfo{13, nullptr, "Unknown13"},
        FunctionInfo{14, nullptr, "Unknown14"},
        FunctionInfo{15, nullptr, "Unknown15"},
        FunctionInfo{16, nullptr, "DestroySystemUpdateTask"},
        FunctionInfo{17, nullptr, "RequestSendSystemUpdate"},
        FunctionInfo{18, nullptr, "GetSendSystemUpdateProgress"}
    );
    return HandlerTableGenerateWithFind(key, functions);
}

ISystemUpdateInterface::ISystemUpdateInterface(Core::System& system_)
    : ServiceFramework{system_, "ns:su"}, service_context{system_, "ns:su"},
      update_notification_event{service_context} {
}

ISystemUpdateInterface::~ISystemUpdateInterface() = default;

Result ISystemUpdateInterface::GetBackgroundNetworkUpdateState(
    Out<BackgroundNetworkUpdateState> out_background_network_update_state) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    *out_background_network_update_state = BackgroundNetworkUpdateState::None;
    R_SUCCEED();
}

Result ISystemUpdateInterface::OpenSystemUpdateControl(
    Out<SharedPointer<ISystemUpdateControl>> out_system_update_control) {
    LOG_WARNING(Service_NS, "(STUBBED) called");
    *out_system_update_control = std::make_shared<ISystemUpdateControl>(system);
    R_SUCCEED();
}

Result ISystemUpdateInterface::GetSystemUpdateNotificationEventForContentDelivery(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Service_NS, "(STUBBED) called");
    *out_event = update_notification_event.GetHandle();
    R_SUCCEED();
}

} // namespace Service::NS
