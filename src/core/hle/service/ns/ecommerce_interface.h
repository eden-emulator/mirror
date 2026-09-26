// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Service::NS {

class IECommerceInterface final : public ServiceFramework<IECommerceInterface> {
public:
    explicit IECommerceInterface(Core::System& system_);
    ~IECommerceInterface() override;

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, nullptr, "RequestLinkDevice"},
        FunctionInfo{1, nullptr, "RequestCleanupAllPreInstalledApplications"},
        FunctionInfo{2, nullptr, "RequestCleanupPreInstalledApplication"},
        FunctionInfo{3, nullptr, "RequestSyncRights"},
        FunctionInfo{4, nullptr, "RequestUnlinkDevice"},
        FunctionInfo{5, nullptr, "RequestRevokeAllELicense"},
        FunctionInfo{6, nullptr, "RequestSyncRightsBasedOnAssignedELicenses"}
    );
};

} // namespace Service::NS
