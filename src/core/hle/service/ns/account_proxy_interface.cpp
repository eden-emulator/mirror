// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/ns/account_proxy_interface.h"

namespace Service::NS {

IAccountProxyInterface::IAccountProxyInterface(Core::System& system_)
    : ServiceFramework{system_, "IAccountProxyInterface"} {
}

IAccountProxyInterface::~IAccountProxyInterface() = default;

} // namespace Service::NS
