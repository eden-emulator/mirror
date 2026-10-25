// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// snx_recompiler.h includes cpu.h and arm.h before its own extern "C" block, so the whole
// header needs C linkage for the runtime library's functions to resolve.
extern "C" {
#include <scarletnx/snx_recompiler.h>
}
