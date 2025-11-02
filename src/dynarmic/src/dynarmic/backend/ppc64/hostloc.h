// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "dynarmic/common/common_types.h"

namespace Dynarmic::Backend::PPC64 {

enum class HostLoc : uint8_t {
    R0, R1, R2, R3, R4, R5, R6, R7, R8, R9,
    R10, R11, R12, R13, R14, R15, R16, R17, R18, R19,
    R20, R21, R22, R23, R24, R25, R26, R27, R28, R29,
    R30, R31,
    FR0, FR1, FR2, FR3, FR4, FR5, FR6, FR7, FR8, FR9,
    FR10, FR11, FR12, FR13, FR14, FR15, FR16, FR17, FR18, FR19,
    FR20, FR21, FR22, FR23, FR24, FR25, FR26, FR27, FR28, FR29,
    FR30, FR31,
    VR0, VR1, VR2, VR3, VR4, VR5, VR6, VR7, VR8, VR9,
    VR10, VR11, VR12, VR13, VR14, VR15, VR16, VR17, VR18, VR19,
    VR20, VR21, VR22, VR23, VR24, VR25, VR26, VR27, VR28, VR29,
    VR30, VR31,
    FirstSpill,
};

}  // namespace Dynarmic::Backend::PPC64
