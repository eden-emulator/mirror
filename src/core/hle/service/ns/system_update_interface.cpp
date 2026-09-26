// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ns/system_update_control.h"
#include "core/hle/service/ns/system_update_interface.h"

namespace Service::NS {

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
