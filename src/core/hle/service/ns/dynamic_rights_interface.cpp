// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ns/dynamic_rights_interface.h"

namespace Service::NS {

IDynamicRightsInterface::IDynamicRightsInterface(Core::System& system_)
    : ServiceFramework{system_, "DynamicRightsInterface"} {
}

IDynamicRightsInterface::~IDynamicRightsInterface() = default;

Result IDynamicRightsInterface::NotifyApplicationRightsCheckStart() {
    LOG_WARNING(Service_NS, "(STUBBED) called");
    R_SUCCEED();
}

Result IDynamicRightsInterface::GetRunningApplicationStatus(Out<u32> out_status,
                                                            u64 rights_handle) {
    LOG_WARNING(Service_NS, "(STUBBED) called, rights_handle={:#x}", rights_handle);
    *out_status = 0;
    R_SUCCEED();
}

Result IDynamicRightsInterface::VerifyActivatedRightsOwners(u64 rights_handle) {
    LOG_WARNING(Service_NS, "(STUBBED) called, rights_handle={:#x}", rights_handle);
    R_SUCCEED();
}

Result IDynamicRightsInterface::HasAccountRestrictedRightsInRunningApplications(
    Out<bool> out_is_restricted) {
    LOG_WARNING(Service_NS, "(STUBBED) called");
    *out_is_restricted = 0;
    R_SUCCEED();
}

} // namespace Service::NS
