// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dynarmic/backend/ppc64/emit_ppc64.h"

#include <bit>

#include <powah_emit.hpp>
#include <fmt/ostream.h>
#include <mcl/bit/bit_field.hpp>

#include "dynarmic/backend/ppc64/a32_core.h"
#include "dynarmic/backend/ppc64/abi.h"
#include "dynarmic/backend/ppc64/emit_context.h"
#include "dynarmic/backend/ppc64/reg_alloc.h"
#include "dynarmic/ir/basic_block.h"
#include "dynarmic/ir/microinstruction.h"
#include "dynarmic/ir/opcodes.h"

namespace Dynarmic::Backend::PPC64 {

template<>
void EmitIR<IR::Opcode::Void>(powah::Context&, EmitContext&, IR::Inst*) {}

template<>
void EmitIR<IR::Opcode::Identity>(powah::Context&, EmitContext& ctx, IR::Inst* inst) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::Breakpoint>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::CallHostFunction>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::PushRSB>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::GetCarryFromOp>(powah::Context&, EmitContext& ctx, IR::Inst* inst) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::GetOverflowFromOp>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::GetGEFromOp>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::GetNZCVFromOp>(powah::Context&, EmitContext& ctx, IR::Inst* inst) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::GetNZFromOp>(powah::Context& as, EmitContext& ctx, IR::Inst* inst) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::GetUpperFromOp>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::GetLowerFromOp>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::GetCFlagFromNZCV>(powah::Context& as, EmitContext& ctx, IR::Inst* inst) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::NZCVFromPackedFlags>(powah::Context&, EmitContext&, IR::Inst*) {
    UNREACHABLE();
}

EmittedBlockInfo EmitPPC64(powah::Context& as, IR::Block block, const EmitConfig& emit_conf) {
    EmittedBlockInfo ebi;
    RegAlloc reg_alloc{as};
    EmitContext ctx{block, reg_alloc, emit_conf, ebi};
    //ebi.entry_point = reinterpret_cast<CodePtr>(as.GetCursorPointer());
    for (auto iter = block.begin(); iter != block.end(); ++iter) {
        IR::Inst* inst = &*iter;

        switch (inst->GetOpcode()) {
#define OPCODE(name, type, ...)                  \
    case IR::Opcode::name:                       \
        EmitIR<IR::Opcode::name>(as, ctx, inst); \
        break;
#define A32OPC(name, type, ...)                       \
    case IR::Opcode::A32##name:                       \
        EmitIR<IR::Opcode::A32##name>(as, ctx, inst); \
        break;
#define A64OPC(name, type, ...)                       \
    case IR::Opcode::A64##name:                       \
        EmitIR<IR::Opcode::A64##name>(as, ctx, inst); \
        break;
#include "dynarmic/ir/opcodes.inc"
#undef OPCODE
#undef A32OPC
#undef A64OPC
        default:
            UNREACHABLE();
        }
    }
    //UNREACHABLE();
    //ebi.size = reinterpret_cast<CodePtr>(as.GetCursorPointer()) - ebi.entry_point;
    return ebi;
}

void EmitRelocation(powah::Context& as, EmitContext& ctx, LinkTarget link_target) {
    UNREACHABLE();
}

}  // namespace Dynarmic::Backend::RV64
