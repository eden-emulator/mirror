// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "shader_recompiler/backend/glasm/emit_glasm_instructions.h"
#include "shader_recompiler/backend/glasm/glasm_emit_context.h"
#include "shader_recompiler/frontend/ir/value.h"

namespace Shader::Backend::GLASM {

void EmitGetRegister(EmitContext& ctx) {
    throw NotImplementedException("GLASM instruction {}", __LINE__);
}

void EmitSetRegister(EmitContext& ctx) {
    throw NotImplementedException("GLASM instruction {}", __LINE__);
}

void EmitGetPred(EmitContext& ctx) {
    throw NotImplementedException("GLASM instruction {}", __LINE__);
}

void EmitSetPred(EmitContext& ctx) {
    throw NotImplementedException("GLASM instruction {}", __LINE__);
}

void EmitSetGotoVariable(EmitContext& ctx) {
    throw NotImplementedException("GLASM instruction {}", __LINE__);
}

void EmitGetGotoVariable(EmitContext& ctx) {
    throw NotImplementedException("GLASM instruction {}", __LINE__);
}

void EmitSetIndirectBranchVariable(EmitContext& ctx) {
    throw NotImplementedException("GLASM instruction {}", __LINE__);
}

void EmitGetIndirectBranchVariable(EmitContext& ctx) {
    throw NotImplementedException("GLASM instruction {}", __LINE__);
}

void EmitGetZFlag(EmitContext& ctx) {
    throw NotImplementedException("GLASM instruction {}", __LINE__);
}

void EmitGetSFlag(EmitContext& ctx) {
    throw NotImplementedException("GLASM instruction {}", __LINE__);
}

void EmitGetCFlag(EmitContext& ctx) {
    throw NotImplementedException("GLASM instruction {}", __LINE__);
}

void EmitGetOFlag(EmitContext& ctx) {
    throw NotImplementedException("GLASM instruction {}", __LINE__);
}

void EmitSetZFlag(EmitContext& ctx) {
    throw NotImplementedException("GLASM instruction {}", __LINE__);
}

void EmitSetSFlag(EmitContext& ctx) {
    throw NotImplementedException("GLASM instruction {}", __LINE__);
}

void EmitSetCFlag(EmitContext& ctx) {
    throw NotImplementedException("GLASM instruction {}", __LINE__);
}

void EmitSetOFlag(EmitContext& ctx) {
    throw NotImplementedException("GLASM instruction {}", __LINE__);
}

void EmitGetZeroFromOp(EmitContext& ctx) {
    throw NotImplementedException("GLASM instruction {}", __LINE__);
}

void EmitGetSignFromOp(EmitContext& ctx) {
    throw NotImplementedException("GLASM instruction {}", __LINE__);
}

void EmitGetCarryFromOp(EmitContext& ctx) {
    throw NotImplementedException("GLASM instruction {}", __LINE__);
}

void EmitGetOverflowFromOp(EmitContext& ctx) {
    throw NotImplementedException("GLASM instruction {}", __LINE__);
}

void EmitGetSparseFromOp(EmitContext& ctx) {
    throw NotImplementedException("GLASM instruction {}", __LINE__);
}

void EmitGetInBoundsFromOp(EmitContext& ctx) {
    throw NotImplementedException("GLASM instruction {}", __LINE__);
}

} // namespace Shader::Backend::GLASM
