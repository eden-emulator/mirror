// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ns/dynamic_rights_interface.h"

namespace Service::NS {

ServiceFrameworkBase::FunctionInfoBase const* IDynamicRightsInterface::FindRequest(u32 key) {
    static const auto functions = CreateStaticMap(
        FunctionInfo{0, nullptr, "RequestApplicationRightsOnServer"},
        FunctionInfo{1, nullptr, "RequestAssignRights"},
        FunctionInfo{4, nullptr, "DeprecatedRequestAssignRightsToResume"},
        FunctionInfo{5, D<&IDynamicRightsInterface::VerifyActivatedRightsOwners>, "VerifyActivatedRightsOwners"},
        FunctionInfo{6, nullptr, "DeprecatedGetApplicationRightsStatus"},
        FunctionInfo{7, nullptr, "RequestPrefetchForDynamicRights"},
        FunctionInfo{8, nullptr, "GetDynamicRightsState"},
        FunctionInfo{9, nullptr, "RequestApplicationRightsOnServerToResume"},
        FunctionInfo{10, nullptr, "RequestAssignRightsToResume"},
        FunctionInfo{11, nullptr, "GetActivatedRightsUsers"},
        FunctionInfo{12, nullptr, "GetApplicationRightsStatus"},
        FunctionInfo{13, D<&IDynamicRightsInterface::GetRunningApplicationStatus>, "GetRunningApplicationStatus"},
        FunctionInfo{14, nullptr, "SelectApplicationLicense"},
        FunctionInfo{15, nullptr, "RequestContentsAuthorizationToken"},
        FunctionInfo{16, nullptr, "QualifyUser"},
        FunctionInfo{17, nullptr, "QualifyUserWithProcessId"},
        FunctionInfo{18, D<&IDynamicRightsInterface::NotifyApplicationRightsCheckStart>, "NotifyApplicationRightsCheckStart"},
        FunctionInfo{19, nullptr, "UpdateUserList"},
        FunctionInfo{20, nullptr, "IsRightsLostUser"},
        FunctionInfo{21, nullptr, "SetRequiredAddOnContentsOnContentsAvailabilityTransition"},
        FunctionInfo{22, nullptr, "GetLimitedApplicationLicense"},
        FunctionInfo{23, nullptr, "GetLimitedApplicationLicenseUpgradableEvent"},
        FunctionInfo{24, nullptr, "NotifyLimitedApplicationLicenseUpgradableEventForDebug"},
        FunctionInfo{25, nullptr, "RequestProceedDynamicRightsState"},
        FunctionInfo{26, D<&IDynamicRightsInterface::HasAccountRestrictedRightsInRunningApplications>, "HasAccountRestrictedRightsInRunningApplications"},
        FunctionInfo{27, nullptr, "Unknown27"}, //20.0.0+
        FunctionInfo{28, nullptr, "Unknown28"}, //20.0.0+
        FunctionInfo{29, nullptr, "Unknown29"}, //21.0.0+
        FunctionInfo{30, nullptr, "Unknown30"} //21.0.0+
    );
    return HandlerTableGenerateWithFind(key, functions);
}

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
