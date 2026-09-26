// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <memory>

#include "common/logging.h"
#include "core/core.h"
#include "core/hle/kernel/k_event.h"
#include "core/hle/service/btm/btm_user_core.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::BTM {

IBtmUserCore::IBtmUserCore(Core::System& system_)
    : ServiceFramework{system_, "IBtmUserCore"}, service_context{system_, "IBtmUserCore"} {
    scan_event = service_context.CreateEvent("IBtmUserCore:ScanEvent");
    connection_event = service_context.CreateEvent("IBtmUserCore:ConnectionEvent");
    service_discovery_event = service_context.CreateEvent("IBtmUserCore:DiscoveryEvent");
    config_event = service_context.CreateEvent("IBtmUserCore:ConfigEvent");
}

IBtmUserCore::~IBtmUserCore() {
    service_context.CloseEvent(scan_event);
    service_context.CloseEvent(connection_event);
    service_context.CloseEvent(service_discovery_event);
    service_context.CloseEvent(config_event);
}

Result IBtmUserCore::AcquireBleScanEvent(Out<bool> out_is_valid,
                                         OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Service_BTM, "(STUBBED) called");

    *out_is_valid = true;
    *out_event = &scan_event->GetReadableEvent();
    R_SUCCEED();
}

Result IBtmUserCore::AcquireBleConnectionEvent(Out<bool> out_is_valid,
                                               OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Service_BTM, "(STUBBED) called");

    *out_is_valid = true;
    *out_event = &connection_event->GetReadableEvent();
    R_SUCCEED();
}

Result IBtmUserCore::AcquireBleServiceDiscoveryEvent(
    Out<bool> out_is_valid, OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Service_BTM, "(STUBBED) called");

    *out_is_valid = true;
    *out_event = &service_discovery_event->GetReadableEvent();
    R_SUCCEED();
}

Result IBtmUserCore::AcquireBleMtuConfigEvent(Out<bool> out_is_valid,
                                              OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Service_BTM, "(STUBBED) called");

    *out_is_valid = true;
    *out_event = &config_event->GetReadableEvent();
    R_SUCCEED();
}

} // namespace Service::BTM
