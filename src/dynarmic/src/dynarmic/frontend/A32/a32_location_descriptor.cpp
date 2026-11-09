// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include "dynarmic/frontend/A32/a32_location_descriptor.h"

#include <format>

namespace Dynarmic::A32 {

std::string ToString(const LocationDescriptor& descriptor) {
    return std::format("{{{:08x},{},{},{:08x}{}}}",
                       descriptor.PC(),
                       descriptor.TFlag() ? "T" : "!T",
                       descriptor.EFlag() ? "E" : "!E",
                       descriptor.FPSCR().Value(),
                       descriptor.SingleStepping() ? ",step" : "");
}

}  // namespace Dynarmic::A32
