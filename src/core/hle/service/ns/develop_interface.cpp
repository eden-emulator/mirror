// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/ns/develop_interface.h"

namespace Service::NS {

IDevelopInterface::IDevelopInterface(Core::System& system_) : ServiceFramework{system_, "ns:dev"} {
}

IDevelopInterface::~IDevelopInterface() = default;

} // namespace Service::NS
