// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>

#include <powah_emit.hpp>
#include "dynarmic/common/common_types.h"

namespace biscuit {
class Assembler;
}  // namespace biscuit

namespace Dynarmic::IR {
class Block;
class Inst;
enum class Cond;
enum class Opcode;
}  // namespace Dynarmic::IR

namespace Dynarmic::Backend::PPC64 {

using CodePtr = std::byte*;

enum class LinkTarget {
    ReturnFromRunCode,
};

struct Relocation {
    std::ptrdiff_t code_offset;
    LinkTarget target;
};

struct EmittedBlockInfo {
    CodePtr entry_point;
    size_t size;
    std::vector<Relocation> relocations;
};

struct EmitConfig {
    bool enable_cycle_counting;
    bool always_little_endian;
};

struct EmitContext;

EmittedBlockInfo EmitPPC64(powah::Context& code, IR::Block block, const EmitConfig& emit_conf);

template<IR::Opcode op>
void EmitIR(powah::Context& code, EmitContext& ctx, IR::Inst* inst);
void EmitRelocation(powah::Context& code, EmitContext& ctx, LinkTarget link_target);
void EmitA32Cond(powah::Context& code, EmitContext& ctx, IR::Cond cond, powah::Label* label);
void EmitA32Terminal(powah::Context& code, EmitContext& ctx);

}  // namespace Dynarmic::Backend::RV64
