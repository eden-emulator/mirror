// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <span>
#include <string_view>

#include "common/common_types.h"

namespace Core {
class System;
}

namespace Kernel {
class KProcess;
}

namespace Core::HLE::GuestHooks {

// Diagnostic only. EDEN_GUEST_HOOKS is a comma separated list of "<module>+<offset>" entries
// (module rtld, main, subsdk0..9 or sdk; offset hexadecimal with 0x or decimal). Each entry is the
// address of either
//   - a `blr xN` instruction: the call is intercepted, the register-resolved target is logged and
//     the call continues there, or
//   - a function entry: the first instruction is relocated and execution continues after it.
// The arguments x0-x3 are logged on entry and x0 on return. Without the variable this does nothing.
// text_size is the size of the module's text segment; the hooks' stubs live in the zero padding at
// the end of the text segment's last page.
void InstallModule(Kernel::KProcess& process, std::string_view name, u64 base, u64 size,
                   u64 text_size);

// Returns true if imm is one of ours.
bool HandleSvc(Core::System& system, u32 imm, std::span<u64, 8> args);

} // namespace Core::HLE::GuestHooks
