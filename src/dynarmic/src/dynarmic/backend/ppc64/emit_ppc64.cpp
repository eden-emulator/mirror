// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dynarmic/backend/ppc64/emit_ppc64.h"

#include <bit>

#include <powah_emit.hpp>
#include <fmt/ostream.h>
#include <mcl/bit/bit_field.hpp>

#include "abi.h"
#include "dynarmic/backend/ppc64/a32_core.h"
#include "dynarmic/backend/ppc64/a64_core.h"
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
void EmitIR<IR::Opcode::Identity>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const source = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    code.MR(result, source);
    ctx.reg_alloc.DefineValue(inst, result);
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
void EmitIR<IR::Opcode::GetNZCVFromOp>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    if (ctx.reg_alloc.IsValueLive(inst)) {
        ASSERT(false && "unimp value live");
        return;
    }

    // All logical operations whom set (RC) are going to compute as the following:
    // 1. Rt <- logical_op (Ra, Rb)
    // 2. Compare as Signed(Rt) against 0
    // Basically, it's equivalent to say:
    //
    // add r0, r1, r2   -> add. r0, r1, r2
    // cmpli r0, 0
    //
    // CR0:
    // 0 - 0x08 - N/LT, result is negative
    // 1 - 0x04 - P/GT, result is positive
    // 2 - 0x02 - Z/EQ, result is zero
    // 3 - 0x01 - S/SO, summary overflow (carry?)
    // XER:
    // 32 - SO
    // 33 - Overflow
    // 34 - Carry
    // NZCV -> (CR0.0, CR0.2, XER.34, XER.33)
    if (false) {
        //code.MFSPR(powah::GPR{1}, tmp4, powah::R0); // From XER
        /*uint64_t nzcv(uint64_t cr0, uint64_t xer) {
            return ((xer >> 33) & 1)
                | (((xer >> 34) & 1) << 1)
                | (((cr0 >> (32 + 2)) & 1) << 2)
                | (((cr0 >> (32 + 0)) & 1) << 3);
        }*/
        // auto const tmp9 = ctx.reg_alloc.ScratchGpr();
        // auto const tmp10 = ctx.reg_alloc.ScratchGpr();
        // code.SRDI(tmp9, tmp3, 34);
        // code.RLDICL(tmp10, tmp4, 31, 63);
        // code.SRDI(tmp3, tmp3, 32);
        // code.RLDIC(tmp9, tmp9, 2, 61);
        // code.OR(tmp9, tmp9, tmp10);
        // code.RLDIC(tmp3, tmp3, 3, 60);
        // code.SRDI(tmp4, tmp4, 34);
        // code.OR(tmp3, tmp9, tmp3);
        // code.RLDIC(tmp4, tmp4, 1, 62);
        // code.OR(tmp3, tmp3, tmp4);
    } else {
        // MFCR Fills RT 32:63, RT 0:31 left blank
        auto const tmp3 = ctx.reg_alloc.ScratchGpr();
        code.MR(tmp3, PPC64::RNZCV);
        ctx.reg_alloc.DefineValue(inst, tmp3);
    }
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

namespace {
void EmitTerminal(powah::Context&, EmitContext&, IR::Term::Interpret, IR::LocationDescriptor, bool) {
    ASSERT(false && "unimp");
}

void EmitTerminal(powah::Context& code, EmitContext& ctx, IR::Term::ReturnToDispatch, IR::LocationDescriptor, bool) {
    ASSERT(false && "unimp");
}

void EmitTerminal(powah::Context& code, EmitContext& ctx, IR::Term::LinkBlock terminal, IR::LocationDescriptor initial_location, bool) {
    auto const tmp = ctx.reg_alloc.ScratchGpr();
    if (ctx.emit_conf.a64_variant) {
        code.LI(tmp, terminal.next.Value());
        code.STD(tmp, PPC64::RJIT, offsetof(A64JitState, pc));
    } else {
        code.LI(tmp, terminal.next.Value());
        code.STW(tmp, PPC64::RJIT, offsetof(A32JitState, regs) + sizeof(u32) * 15);
    }
}

void EmitTerminal(powah::Context& code, EmitContext& ctx, IR::Term::LinkBlockFast terminal, IR::LocationDescriptor initial_location, bool) {
    ASSERT(false && "unimp");
}

void EmitTerminal(powah::Context& code, EmitContext& ctx, IR::Term::PopRSBHint, IR::LocationDescriptor, bool) {
    ASSERT(false && "unimp");
}

void EmitTerminal(powah::Context& code, EmitContext& ctx, IR::Term::FastDispatchHint, IR::LocationDescriptor, bool) {
    ASSERT(false && "unimp");
}

void EmitTerminal(powah::Context& code, EmitContext& ctx, IR::Term::If terminal, IR::LocationDescriptor initial_location, bool is_single_step) {
    ASSERT(false && "unimp");
}

void EmitTerminal(powah::Context& code, EmitContext& ctx, IR::Term::CheckBit terminal, IR::LocationDescriptor initial_location, bool is_single_step) {
    ASSERT(false && "unimp");
}

void EmitTerminal(powah::Context& code, EmitContext& ctx, IR::Term::CheckHalt terminal, IR::LocationDescriptor initial_location, bool is_single_step) {
    ASSERT(false && "unimp");
}

void EmitTerminal(powah::Context& code, EmitContext& ctx, IR::Term::Terminal terminal, IR::LocationDescriptor initial_location, bool is_single_step) {
    boost::apply_visitor([&](const auto& t) {
        EmitTerminal(code, ctx, t, initial_location, is_single_step);
    }, terminal);
}
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

    // auto const cycles_to_add = block.CycleCount();
    EmitTerminal(code, ctx, ctx.block.GetTerminal(), ctx.block.Location(), false);

    for (size_t i = 0; i < gpr_order.size(); ++i)
        code.LD(powah::GPR{gpr_order[i]}, powah::R1, -(i * 8));
    code.BLR();
    code.ApplyRelocs();

    /*
    llvm-objcopy -I binary -O elf64-powerpc --rename-section=.data=.text,code test.bin test.elf && llvm-objdump -d test.elf
    */
    static FILE* fp = fopen("test.bin", "wb");
    fwrite(code.base, code.offset - start_offset, sizeof(uint32_t), fp);
    fflush(fp);

    ebi.size = code.offset - start_offset;
    return ebi;
}

}  // namespace Dynarmic::Backend::RV64
