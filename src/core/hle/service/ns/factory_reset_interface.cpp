// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/ns/factory_reset_interface.h"

namespace Service::NS {

std::optional<ServiceFrameworkBase::FunctionInfoBase> IFactoryResetInterface::FindRequest(u32 key) {
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{100, nullptr, "ResetToFactorySettings"},
        FunctionInfo{101, nullptr, "ResetToFactorySettingsWithoutUserSaveData"},
        FunctionInfo{102, nullptr, "ResetToFactorySettingsForRefurbishment"},
        FunctionInfo{103, nullptr, "ResetToFactorySettingsWithPlatformRegion"},
        FunctionInfo{104, nullptr, "ResetToFactorySettingsWithPlatformRegionAuthentication"},
        FunctionInfo{105, nullptr, "RequestResetToFactorySettingsSecurely"},
        FunctionInfo{106, nullptr, "RequestResetToFactorySettingsWithPlatformRegionAuthenticationSecurely"}
    );
    return HandlerTableGenerateWithFind(key, functions);
}

IFactoryResetInterface::IFactoryResetInterface(Core::System& system_)
    : ServiceFramework{system_, "IFactoryResetInterface"} {
}

IFactoryResetInterface::~IFactoryResetInterface() = default;

} // namespace Service::NS
