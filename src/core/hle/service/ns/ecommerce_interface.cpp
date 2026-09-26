// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/ns/ecommerce_interface.h"

namespace Service::NS {

IECommerceInterface::IECommerceInterface(Core::System& system_)
    : ServiceFramework{system_, "IECommerceInterface"} {
}

IECommerceInterface::~IECommerceInterface() = default;

} // namespace Service::NS
