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
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::Breakpoint>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::CallHostFunction>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::PushRSB>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::GetCarryFromOp>(powah::Context&, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::GetOverflowFromOp>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::GetGEFromOp>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::GetNZCVFromOp>(powah::Context&, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::GetNZFromOp>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::GetUpperFromOp>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::GetLowerFromOp>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::GetCFlagFromNZCV>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::NZCVFromPackedFlags>(powah::Context&, EmitContext&, IR::Inst*) {
    ASSERT(false && "unimp");
}

EmittedBlockInfo EmitPPC64(powah::Context& code, IR::Block block, const EmitConfig& emit_conf) {
    EmittedBlockInfo ebi;
    RegAlloc reg_alloc{code};
    EmitContext ctx{block, reg_alloc, emit_conf, ebi};

    auto const start_offset = code.offset;
    ebi.entry_point = CodePtr(code.base + start_offset);

    // Non-volatile saves
    std::vector<u32> gpr_order{GPR_ORDER};
    for (size_t i = 0; i < gpr_order.size(); ++i)
        code.STD(powah::GPR{gpr_order[i]}, powah::R1, -(i * 8));

    for (auto iter = block.begin(); iter != block.end(); ++iter) {
        IR::Inst* inst = &*iter;
        switch (inst->GetOpcode()) {
#define OPCODE(name, type, ...)                  \
    case IR::Opcode::name:                       \
        EmitIR<IR::Opcode::name>(code, ctx, inst); \
        break;
#define A32OPC(name, type, ...)                       \
    case IR::Opcode::A32##name:                       \
        EmitIR<IR::Opcode::A32##name>(code, ctx, inst); \
        break;
#define A64OPC(name, type, ...)                       \
    case IR::Opcode::A64##name:                       \
        EmitIR<IR::Opcode::A64##name>(code, ctx, inst); \
        break;
#include "dynarmic/ir/opcodes.inc"
#undef OPCODE
#undef A32OPC
#undef A64OPC
        default:
            UNREACHABLE();
        }
    }

    for (size_t i = 0; i < gpr_order.size(); ++i)
        code.LD(powah::GPR{gpr_order[i]}, powah::R1, -(i * 8));
    code.BLR();

    /*
    llvm-objcopy -I binary -O elf64-powerpc --rename-section=.data=.text,code test.bin test.elf
    llvm-objdump -d test.elf
    */
    static FILE* fp = fopen("test.bin", "ab");
    fwrite(code.base, code.offset - start_offset, sizeof(uint32_t), fp);

    ebi.size = code.offset - start_offset;
    return ebi;
}

void EmitRelocation(powah::Context& code, EmitContext& ctx, LinkTarget link_target) {
    ASSERT(false && "unimp");
}

}  // namespace Dynarmic::Backend::RV64
