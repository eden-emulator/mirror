// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <powah_emit.hpp>
#include "dynarmic/common/common_types.h"

namespace Dynarmic::Backend::PPC64 {

constexpr powah::GPR RJIT = powah::R3; //yeah it's param, so what?
constexpr powah::GPR RNZCV = powah::R31;

constexpr powah::GPR ABI_PARAM1 = powah::R3;
constexpr powah::GPR ABI_PARAM2 = powah::R4;
constexpr powah::GPR ABI_PARAM3 = powah::R5;
constexpr powah::GPR ABI_PARAM4 = powah::R6;

// See https://refspecs.linuxfoundation.org/ELF/ppc64/PPC-elf64abi.html#REG
constexpr std::initializer_list<u32> GPR_ORDER{
    14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
};

constexpr std::initializer_list<powah::GPR> ABI_CALLEE_SAVED{
    powah::R14, powah::R15, powah::R16, powah::R17,
    powah::R18, powah::R19, powah::R20, powah::R21,
    powah::R22, powah::R23, powah::R24, powah::R25,
    powah::R26, powah::R27, powah::R28, powah::R29,
    powah::R30, powah::R31
};

}  // namespace Dynarmic::Backend::RV64
