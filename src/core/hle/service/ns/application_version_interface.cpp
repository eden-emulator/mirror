// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/ns/application_version_interface.h"

namespace Service::NS {

IApplicationVersionInterface::IApplicationVersionInterface(Core::System& system_)
    : ServiceFramework{system_, "IApplicationVersionInterface"} {
}

IApplicationVersionInterface::~IApplicationVersionInterface() = default;

} // namespace Service::NS
