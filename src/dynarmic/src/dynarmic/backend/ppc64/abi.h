// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <powah_emit.hpp>
#include "dynarmic/common/common_types.h"

namespace Dynarmic::Backend::PPC64 {

/*
https://refspecs.linuxfoundation.org/ELF/ppc64/PPC-elf64abi.html#REG

r0        Volatile register used in function prologs
r1        Stack frame pointer
r2        TOC pointer
r3        Volatile parameter and return value register
r4-r10    Volatile registers used for function parameters
r11       Volatile register used in calls by pointer and as an environment pointer for languages which require one
r12       Volatile register used for exception handling and glink code
r13       Reserved for use as system thread ID
r14-r31   Nonvolatile registers used for local variables

f0        Volatile scratch register
f1-f4     Volatile floating point parameter and return value registers
f5-f13    Volatile floating point parameter registers
f14-f31   Nonvolatile registers

LR        Link register (volatile)
CTR       Loop counter register (volatile)
XER       Fixed point exception register (volatile)
FPSCR     Floating point status and control register (volatile)

CR0-CR1   Volatile condition code register fields
CR2-CR4   Nonvolatile condition code register fields
CR5-CR7   Volatile condition code register fields

v0-v1     Volatile scratch registers
v2-v13    Volatile vector parameters registers
v14-v19   Volatile scratch registers
v20-v31   Non-volatile registers
vrsave    Non-volatile 32-bit register
*/

// Jit fn signature => (AXXAddressSpace& process, AXXJitState& thread_ctx, volatile u32* halt_reason)
constexpr powah::GPR RPROCESS = powah::R3;
constexpr powah::GPR RJIT = powah::R4;
constexpr powah::GPR RHALTREASON = powah::R5;
constexpr powah::GPR RTOCPTR = powah::R6;
// temporals
constexpr powah::GPR RNZCV = powah::R7;
constexpr powah::GPR RCHECKBIT = powah::R8;

constexpr powah::GPR ABI_PARAM1 = powah::R3;
constexpr powah::GPR ABI_PARAM2 = powah::R4;
constexpr powah::GPR ABI_PARAM3 = powah::R5;
constexpr powah::GPR ABI_PARAM4 = powah::R6;

// See https://refspecs.linuxfoundation.org/ELF/ppc64/PPC-elf64abi.html#REG
constexpr std::initializer_list<u32> GPR_ORDER{
    // r13 is thread-id
    14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31 //non-volatile
};

constexpr std::initializer_list<powah::GPR> ABI_CALLEER_SAVED{
    powah::R3, powah::R4, powah::R5, powah::R6,
    powah::R7, powah::R8, powah::R9, powah::R10,
    powah::R11, powah::R12
};
constexpr std::initializer_list<powah::GPR> ABI_CALLEE_SAVED{
    powah::R14, powah::R15, powah::R16, powah::R17,
    powah::R18, powah::R19, powah::R20, powah::R21,
    powah::R22, powah::R23, powah::R24, powah::R25,
    powah::R26, powah::R27, powah::R28, powah::R29,
    powah::R30, powah::R31
};

}  // namespace Dynarmic::Backend::RV64
