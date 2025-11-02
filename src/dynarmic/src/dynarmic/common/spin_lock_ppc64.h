// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <powah_emit.hpp>

namespace Dynarmic {

void EmitSpinLockLock(powah::Context& code, powah::GPR const ptr, powah::GPR const tmp);
void EmitSpinLockUnlock(powah::Context& code, powah::GPR const ptr, powah::GPR const tmp);

}  // namespace Dynarmic
