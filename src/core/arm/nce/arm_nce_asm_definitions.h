// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project */
/* SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#define __ASSEMBLY__

// Newer Linux UAPI headers check __ASSEMBLER__ instead of __ASSEMBLY__ to hide C declarations.
// Without it, asm-generic/signal.h redeclares sigset_t and clashes with glibc's. Only define it
// around these includes, since glibc headers also look at __ASSEMBLER__.
#ifndef __ASSEMBLER__
#define __ASSEMBLER__
#define EDEN_NCE_UNDEF_ASSEMBLER
#endif

#include <asm-generic/signal.h>
#include <asm-generic/unistd.h>

#ifdef EDEN_NCE_UNDEF_ASSEMBLER
#undef __ASSEMBLER__
#undef EDEN_NCE_UNDEF_ASSEMBLER
#endif

#define ReturnToRunCodeByExceptionLevelChangeSignal SIGUSR2
#define BreakFromRunCodeSignal SIGURG
#define GuestAccessFaultSignal SIGSEGV
#define GuestAlignmentFaultSignal SIGBUS

#define GuestContextSp 0xF8
#define GuestContextHostContext 0x320

#define HostContextSpTpidrEl0 0xE0
#define HostContextTpidrEl0 0xE8
#define HostContextRegs 0x0
#define HostContextVregs 0x60

#define TpidrEl0NativeContext 0x10
#define TpidrEl0Lock 0x18
#define TpidrEl0TlsMagic 0x20
#define TlsMagic 0x555a5559

#define SpinLockLocked 0
#define SpinLockUnlocked 1
