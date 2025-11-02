// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "dynarmic/backend/ppc64/emit_ppc64.h"
#include "dynarmic/backend/ppc64/reg_alloc.h"

namespace Dynarmic::IR {
class Block;
}  // namespace Dynarmic::IR

namespace Dynarmic::Backend::PPC64 {

struct EmitConfig;

struct EmitContext {
    IR::Block& block;
    RegAlloc& reg_alloc;
    const EmitConfig& emit_conf;
    EmittedBlockInfo& ebi;
};

}  // namespace Dynarmic::Backend::RV64
