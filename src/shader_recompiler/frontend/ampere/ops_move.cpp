// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// Encoding reference: Mesa NAK sm70_encode.rs (MIT), Copyright 2022 Collabora, Ltd.

#include "shader_recompiler/frontend/ampere/visitor.h"

namespace Shader::Ampere {

void Visitor::MOV() {
    const u32 dest = Take(16, 8);
    // MOV has a single source in the wide slot (register, immediate or constant buffer) and a
    // byte-lane mask; partial lane masks are not modelled.
    const Operand source = Form() == 1 ? [&] {
        Operand operand;
        operand.reg = Take(32, 8);
        return operand;
    }() : WideSlot(false);
    Require(Take(72, 4) == 0xf, "native MOV lane mask");
    X(dest, Value(source));
}

} // namespace Shader::Ampere
