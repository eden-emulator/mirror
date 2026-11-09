// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include "dynarmic/ir/location_descriptor.h"

#include <format>

namespace Dynarmic::IR {

std::string ToString(const LocationDescriptor& descriptor) {
    return std::format("{{{:016x}}}", descriptor.Value());
}

}  // namespace Dynarmic::IR
