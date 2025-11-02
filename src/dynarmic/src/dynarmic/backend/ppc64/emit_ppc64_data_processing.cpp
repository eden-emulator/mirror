// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <powah_emit.hpp>
#include <fmt/ostream.h>

#include "dynarmic/common/assert.h"
#include "dynarmic/backend/ppc64/a32_core.h"
#include "dynarmic/backend/ppc64/abi.h"
#include "dynarmic/backend/ppc64/emit_context.h"
#include "dynarmic/backend/ppc64/emit_ppc64.h"
#include "dynarmic/backend/ppc64/reg_alloc.h"
#include "dynarmic/ir/basic_block.h"
#include "dynarmic/ir/microinstruction.h"
#include "dynarmic/ir/opcodes.h"

namespace Dynarmic::Backend::PPC64 {

template<>
void EmitIR<IR::Opcode::Pack2x32To1x64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::Pack2x64To1x128>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::LeastSignificantWord>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    UNREACHABLE();
}

/*
uint64_t f(uint64_t a) { return (uint16_t)a; }
*/
template<>
void EmitIR<IR::Opcode::LeastSignificantHalf>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const source = ctx.reg_alloc.UseGpr(args[0]);
    code.RLWINM(result, source, 0, 0xffff);
    ctx.reg_alloc.DefineValue(inst, result);
}

/*
uint64_t f(uint64_t a) { return (uint8_t)a; }
*/
template<>
void EmitIR<IR::Opcode::LeastSignificantByte>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const source = ctx.reg_alloc.UseGpr(args[0]);
    code.RLWINM(result, source, 0, 0xff);
    ctx.reg_alloc.DefineValue(inst, result);
}

/*
uint64_t f(uint64_t a) { return (uint32_t)(a >> 32); }
*/
template<>
void EmitIR<IR::Opcode::MostSignificantWord>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const source = ctx.reg_alloc.UseGpr(args[0]);
    code.SRDI(result, source, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

/*
uint64_t f(uint64_t a) { return ((uint32_t)a) >> 31; }
*/
template<>
void EmitIR<IR::Opcode::MostSignificantBit>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const source = ctx.reg_alloc.UseGpr(args[0]);
    code.RLWINM(result, source, 1, 31, 31);
    ctx.reg_alloc.DefineValue(inst, result);
}

/*
uint64_t f(uint64_t a) { return a == 0; }
*/
template<>
void EmitIR<IR::Opcode::IsZero32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const source = ctx.reg_alloc.UseGpr(args[0]);
    code.CNTLZD(result, source);
    code.SRDI(result, result, 6);
    ctx.reg_alloc.DefineValue(inst, result);
}

/*
uint64_t f(uint64_t a) { return (uint32_t)a == 0; }
*/
template<>
void EmitIR<IR::Opcode::IsZero64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const source = ctx.reg_alloc.UseGpr(args[0]);
    code.CNTLZW(result, source);
    code.SRWI(result, result, 5);
    ctx.reg_alloc.DefineValue(inst, result);
}

/*
uint64_t f(uint64_t a) { return (a & 1) != 0; }
*/
template<>
void EmitIR<IR::Opcode::TestBit>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const source = ctx.reg_alloc.UseGpr(args[0]);
    if (args[1].IsImmediate()) {
        auto const shift = args[1].GetImmediateU8();
        if (shift > 0) {
            code.RLDICL(result, source, (64 - shift - 1) & 0x3f, 63);
        }
    } else {
        UNREACHABLE();
    }
    ctx.reg_alloc.DefineValue(inst, result);
}

/*
struct jit {
    uint64_t t;
    uint64_t a;
};
uint64_t f(jit *p, uint64_t a, uint64_t b) {
    bool n = (p->a & 0b1000) != 0;
    bool z = (p->a & 0b0100) != 0;
    bool c = (p->a & 0b0010) != 0;
    bool v = (p->a & 0b0001) != 0;
    return (p->a & 0b0001) == 0 ? a : b;
}
*/
static powah::GPR EmitConditionalSelectX(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const nzcv = ctx.reg_alloc.ScratchGpr();
    powah::GPR const then_ = ctx.reg_alloc.UseScratchGpr(args[1]);
    powah::GPR const else_ = ctx.reg_alloc.UseScratchGpr(args[2]);
    switch (args[0].GetImmediateCond()) {
    case IR::Cond::EQ: // Z == 1
        code.LD(nzcv, PPC64::RJIT, offsetof(A32JitState, cpsr_nzcv));
        code.ANDI_(nzcv, nzcv, 4);
        code.ISELGT(nzcv, then_, else_);
        break;
    case IR::Cond::NE: // Z == 0
        code.LD(nzcv, PPC64::RJIT, offsetof(A32JitState, cpsr_nzcv));
        code.ANDI_(nzcv, nzcv, 4);
        code.ISELGT(nzcv, then_, else_);
        break;
    case IR::Cond::CS: // C == 1
        code.LD(nzcv, PPC64::RJIT, offsetof(A32JitState, cpsr_nzcv));
        code.ANDI_(nzcv, nzcv, 2);
        code.ISELGT(nzcv, then_, else_);
        break;
    case IR::Cond::CC: // C == 0
        code.LD(nzcv, PPC64::RJIT, offsetof(A32JitState, cpsr_nzcv));
        code.ANDI_(nzcv, nzcv, 2);
        code.ISELGT(nzcv, then_, else_);
        break;
    case IR::Cond::MI: // N == 1
        code.LD(nzcv, PPC64::RJIT, offsetof(A32JitState, cpsr_nzcv));
        code.ANDI_(nzcv, nzcv, 8);
        code.ISELGT(nzcv, then_, else_);
        break;
    case IR::Cond::PL: // N == 0
        code.LD(nzcv, PPC64::RJIT, offsetof(A32JitState, cpsr_nzcv));
        code.ANDI_(nzcv, nzcv, 8);
        code.ISELGT(nzcv, then_, else_);
        break;
    case IR::Cond::VS: // V == 1
        code.LD(nzcv, PPC64::RJIT, offsetof(A32JitState, cpsr_nzcv));
        code.ANDI_(nzcv, nzcv, 1);
        code.ISELGT(nzcv, then_, else_);
        break;
    case IR::Cond::VC: // V == 0
        code.LD(nzcv, PPC64::RJIT, offsetof(A32JitState, cpsr_nzcv));
        code.ANDI_(nzcv, nzcv, 1);
        code.ISELGT(nzcv, else_, then_);
        break;
    case IR::Cond::HI: // Z == 0 && C == 1
        code.LD(nzcv, PPC64::RJIT, offsetof(A32JitState, cpsr_nzcv));
        code.RLWINM(nzcv, nzcv, 0, 29, 30);
        code.CMPLDI(nzcv, 2);
        code.ISELEQ(nzcv, then_, else_);
        break;
    case IR::Cond::LS: // Z == 1 || C == 0
        code.LD(nzcv, PPC64::RJIT, offsetof(A32JitState, cpsr_nzcv));
        code.RLWINM(nzcv, nzcv, 0, 29, 30);
        code.CMPLDI(nzcv, 2);
        code.ISELEQ(nzcv, else_, then_);
        break;
    case IR::Cond::GE: { // N == V
        powah::GPR const tmp = ctx.reg_alloc.ScratchGpr();
        code.LWZ(nzcv, PPC64::RJIT, offsetof(A32JitState, cpsr_nzcv));
        code.SRWI(tmp, nzcv, 3);
        code.XOR(nzcv, tmp, nzcv);
        code.ANDI_(nzcv, nzcv, 1);
        code.ISELGT(nzcv, else_, then_);
    } break;
    case IR::Cond::LT: { // N != V
        powah::GPR const tmp = ctx.reg_alloc.ScratchGpr();
        code.LWZ(nzcv, PPC64::RJIT, offsetof(A32JitState, cpsr_nzcv));
        code.SRWI(tmp, nzcv, 3);
        code.XOR(nzcv, tmp, nzcv);
        code.ANDI_(nzcv, nzcv, 1);
        code.ISELGT(nzcv, then_, else_);
    } break;
    case IR::Cond::GT: { // Z == 0 && N == V
        powah::GPR const tmp = ctx.reg_alloc.ScratchGpr();
        powah::Label const l_ne = code.DefineLabel();
        powah::Label const l_cc = code.DefineLabel();
        powah::Label const l_fi = code.DefineLabel();
        code.LD(nzcv, PPC64::RJIT, offsetof(A32JitState, cpsr_nzcv));
        code.ANDI_(tmp, nzcv, 4);
        code.BNE(powah::CR0, l_ne);
        code.SRWI(tmp, nzcv, 3);
        code.XOR(nzcv, tmp, nzcv);
        code.ANDI_(nzcv, nzcv, 1);
        code.CMPLDI(nzcv, 0);
        code.BGT(powah::CR0, l_fi);
        code.LABEL(l_ne);
        code.MR(then_, else_);
        code.LABEL(l_cc);
        code.MR(nzcv, then_);
        code.LABEL(l_fi);
    } break;
    case IR::Cond::LE: { // Z == 1 || N != V
        powah::GPR const tmp = ctx.reg_alloc.ScratchGpr();
        powah::Label const l_ne = code.DefineLabel();
        code.LD(nzcv, PPC64::RJIT, offsetof(A32JitState, cpsr_nzcv));
        code.ANDI_(tmp, nzcv, 4);
        code.BNE(powah::CR0, l_ne);
        code.SRWI(tmp, nzcv, 3);
        code.XOR(nzcv, tmp, nzcv);
        code.ANDI_(nzcv, nzcv, 1);
        code.ISELGT(then_, then_, else_);
        code.LABEL(l_ne);
        code.MR(nzcv, then_);
    } break;
    default:
        UNREACHABLE();
    }
    return nzcv;
}

template<>
void EmitIR<IR::Opcode::ConditionalSelect32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    powah::GPR const result = EmitConditionalSelectX(code, ctx, inst);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::ConditionalSelect64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    powah::GPR const result = EmitConditionalSelectX(code, ctx, inst);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::ConditionalSelectNZCV>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    powah::GPR const result = EmitConditionalSelectX(code, ctx, inst);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::LogicalShiftLeft32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const source = ctx.reg_alloc.UseGpr(args[0]);
    powah::GPR const shift = ctx.reg_alloc.UseGpr(args[1]);
    code.SLW(result, source, shift);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::LogicalShiftLeft64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const source = ctx.reg_alloc.UseGpr(args[0]);
    powah::GPR const shift = ctx.reg_alloc.UseGpr(args[1]);
    code.SLD(result, source, shift);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::LogicalShiftRight32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const source = ctx.reg_alloc.UseGpr(args[0]);
    powah::GPR const shift = ctx.reg_alloc.UseGpr(args[1]);
    code.SRW(result, source, shift);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

/*
uint64_t f(uint64_t a, uint64_t s) { return a >> s; }
*/
template<>
void EmitIR<IR::Opcode::LogicalShiftRight64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const source = ctx.reg_alloc.UseGpr(args[0]);
    powah::GPR const shift = ctx.reg_alloc.UseGpr(args[1]);
    code.SRD(result, source, shift);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::ArithmeticShiftRight32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const source = ctx.reg_alloc.UseGpr(args[0]);
    powah::GPR const shift = ctx.reg_alloc.UseGpr(args[1]);
    code.SRAW(result, source, shift);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::ArithmeticShiftRight64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const source = ctx.reg_alloc.UseGpr(args[0]);
    powah::GPR const shift = ctx.reg_alloc.UseGpr(args[1]);
    code.SRAD(result, source, shift);
    ctx.reg_alloc.DefineValue(inst, result);
}

// __builtin_rotateright32
template<>
void EmitIR<IR::Opcode::RotateRight32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const src_a = ctx.reg_alloc.UseGpr(args[0]);
    powah::GPR const src_b = ctx.reg_alloc.UseGpr(args[0]);
    code.NEG(result, src_a, powah::R0);
    code.ROTLW(result, result, src_b);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::RotateRight64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const src_a = ctx.reg_alloc.UseGpr(args[0]);
    powah::GPR const src_b = ctx.reg_alloc.UseGpr(args[0]);
    code.NEG(result, src_a, powah::R0);
    code.ROTLD(result, result, src_b);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::RotateRightExtended>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const src_a = ctx.reg_alloc.UseGpr(args[0]);
    powah::GPR const src_b = ctx.reg_alloc.UseGpr(args[0]);
    code.NEG(result, src_a, powah::R0);
    code.ROTLD(result, result, src_b);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::LogicalShiftLeftMasked32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const source = ctx.reg_alloc.UseGpr(args[0]);
    code.SLW(result, source, source);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::LogicalShiftLeftMasked64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const source = ctx.reg_alloc.UseGpr(args[0]);
    code.SLD(result, source, source);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::LogicalShiftRightMasked32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const source = ctx.reg_alloc.UseGpr(args[0]);
    code.SRW(result, source, source);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::LogicalShiftRightMasked64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const source = ctx.reg_alloc.UseGpr(args[0]);
    code.SRD(result, source, source);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::ArithmeticShiftRightMasked32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const source = ctx.reg_alloc.UseGpr(args[0]);
    code.SRAW(result, source, source);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::ArithmeticShiftRightMasked64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const source = ctx.reg_alloc.UseGpr(args[0]);
    code.SRAL(result, source, source);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::RotateRightMasked32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const src_a = ctx.reg_alloc.UseGpr(args[0]);
    powah::GPR const src_b = ctx.reg_alloc.UseGpr(args[0]);
    code.NEG(result, src_a, powah::R0);
    code.ROTLD(result, result, src_b);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::RotateRightMasked64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const src_a = ctx.reg_alloc.UseGpr(args[0]);
    powah::GPR const src_b = ctx.reg_alloc.UseGpr(args[0]);
    code.NEG(result, src_a, powah::R0);
    code.ROTLD(result, result, src_b);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::Add32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const src_a = ctx.reg_alloc.UseGpr(args[0]);
    powah::GPR const src_b = ctx.reg_alloc.UseGpr(args[1]);
    code.ADD(result, src_a, src_b);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::Add64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const src_a = ctx.reg_alloc.UseGpr(args[0]);
    powah::GPR const src_b = ctx.reg_alloc.UseGpr(args[1]);
    code.ADD(result, src_a, src_b);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::Sub32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const src_a = ctx.reg_alloc.UseGpr(args[0]);
    powah::GPR const src_b = ctx.reg_alloc.UseGpr(args[1]);
    code.SUBF(result, src_b, src_a);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::Sub64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const src_a = ctx.reg_alloc.UseGpr(args[0]);
    powah::GPR const src_b = ctx.reg_alloc.UseGpr(args[1]);
    code.SUBF(result, src_b, src_a);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::Mul32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const src_a = ctx.reg_alloc.UseGpr(args[0]);
    powah::GPR const src_b = ctx.reg_alloc.UseGpr(args[1]);
    code.MULLW(result, src_a, src_b);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::Mul64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const src_a = ctx.reg_alloc.UseGpr(args[0]);
    powah::GPR const src_b = ctx.reg_alloc.UseGpr(args[1]);
    code.MULLD(result, src_a, src_b);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::SignedMultiplyHigh64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const src_a = ctx.reg_alloc.UseGpr(args[0]);
    powah::GPR const src_b = ctx.reg_alloc.UseGpr(args[1]);
    code.MULLD(result, src_a, src_b);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::UnsignedMultiplyHigh64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const src_a = ctx.reg_alloc.UseGpr(args[0]);
    powah::GPR const src_b = ctx.reg_alloc.UseGpr(args[1]);
    code.MULLD(result, src_a, src_b);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::UnsignedDiv32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const src_a = ctx.reg_alloc.UseGpr(args[0]);
    powah::GPR const src_b = ctx.reg_alloc.UseGpr(args[1]);
    code.DIVDU(result, src_a, src_b);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::UnsignedDiv64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const src_a = ctx.reg_alloc.UseGpr(args[0]);
    powah::GPR const src_b = ctx.reg_alloc.UseGpr(args[1]);
    code.DIVDU(result, src_a, src_b);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::SignedDiv32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const src_a = ctx.reg_alloc.UseGpr(args[0]);
    powah::GPR const src_b = ctx.reg_alloc.UseGpr(args[1]);
    code.DIVW(result, src_a, src_b);
    code.EXTSW(result, result);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::SignedDiv64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const src_a = ctx.reg_alloc.UseGpr(args[0]);
    powah::GPR const src_b = ctx.reg_alloc.UseGpr(args[1]);
    code.DIVD(result, src_a, src_b);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::And32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const src_a = ctx.reg_alloc.UseGpr(args[0]);
    powah::GPR const src_b = ctx.reg_alloc.UseGpr(args[1]);
    code.AND(result, src_a, src_b);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::And64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const src_a = ctx.reg_alloc.UseGpr(args[0]);
    powah::GPR const src_b = ctx.reg_alloc.UseGpr(args[1]);
    code.AND(result, src_a, src_b);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::AndNot32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const src_a = ctx.reg_alloc.UseGpr(args[0]);
    powah::GPR const src_b = ctx.reg_alloc.UseGpr(args[1]);
    code.NAND(result, src_a, src_b);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::AndNot64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const src_a = ctx.reg_alloc.UseGpr(args[0]);
    powah::GPR const src_b = ctx.reg_alloc.UseGpr(args[1]);
    code.NAND(result, src_a, src_b);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::Eor32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const src_a = ctx.reg_alloc.UseGpr(args[0]);
    powah::GPR const src_b = ctx.reg_alloc.UseGpr(args[1]);
    code.XOR(result, src_a, src_b);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::Eor64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const src_a = ctx.reg_alloc.UseGpr(args[0]);
    powah::GPR const src_b = ctx.reg_alloc.UseGpr(args[1]);
    code.XOR(result, src_a, src_b);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::Or32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const src_a = ctx.reg_alloc.UseGpr(args[0]);
    powah::GPR const src_b = ctx.reg_alloc.UseGpr(args[1]);
    code.OR(result, src_a, src_b);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::Or64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const src_a = ctx.reg_alloc.UseGpr(args[0]);
    powah::GPR const src_b = ctx.reg_alloc.UseGpr(args[1]);
    code.OR(result, src_a, src_b);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::Not32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const source = ctx.reg_alloc.UseGpr(args[0]);
    code.NOT(result, source);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::Not64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const source = ctx.reg_alloc.UseGpr(args[0]);
    code.NOT(result, source);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::SignExtendByteToWord>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const source = ctx.reg_alloc.UseGpr(args[0]);
    code.EXTSB(result, source);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::SignExtendHalfToWord>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const source = ctx.reg_alloc.UseGpr(args[0]);
    code.EXTSH(result, source);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::SignExtendByteToLong>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const source = ctx.reg_alloc.UseGpr(args[0]);
    code.EXTSH(result, source);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::SignExtendHalfToLong>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const source = ctx.reg_alloc.UseGpr(args[0]);
    code.EXTSB(result, source);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::SignExtendWordToLong>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const source = ctx.reg_alloc.UseGpr(args[0]);
    code.EXTSW(result, source);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::ZeroExtendByteToWord>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const source = ctx.reg_alloc.UseGpr(args[0]);
    code.RLWINM(result, source, 0, 0xff);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::ZeroExtendHalfToWord>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const source = ctx.reg_alloc.UseGpr(args[0]);
    code.RLWINM(result, source, 0, 0xffff);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::ZeroExtendByteToLong>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const source = ctx.reg_alloc.UseGpr(args[0]);
    code.RLWINM(result, source, 0, 0xff);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::ZeroExtendHalfToLong>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const source = ctx.reg_alloc.UseGpr(args[0]);
    code.RLWINM(result, source, 0, 0xffff);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::ZeroExtendWordToLong>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const source = ctx.reg_alloc.UseGpr(args[0]);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::ZeroExtendLongToQuad>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    UNREACHABLE();
}

// __builtin_bswap32
template<>
void EmitIR<IR::Opcode::ByteReverseWord>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const source = ctx.reg_alloc.UseGpr(args[0]);
    if (false) {
        //code.BRW(result, source);
        code.RLDICL(result, result, 0, 32);
    } else {
        code.ROTLWI(result, source, 8);
        code.RLWIMI(result, source, 24, 16, 23);
        code.RLWIMI(result, source, 24, 0, 7);
    }
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::ByteReverseHalf>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const source = ctx.reg_alloc.UseGpr(args[0]);
    if (false) {
        //code.BRH(result, source);
        code.RLWINM(result, source, 0, 0xff);
    } else {
        code.ROTLWI(result, source, 24);
        code.RLWIMI(result, source, 8, 0, 23);
        code.CLRLDI(result, result, 48);
    }
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::ByteReverseDual>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const source = ctx.reg_alloc.UseGpr(args[0]);
    if (false) {
        //code.BRD(result, source);
    } else {
        powah::GPR const tmp = ctx.reg_alloc.ScratchGpr();
        code.ROTLDI(tmp, source, 16);
        code.ROTLDI(result, source, 8);
        code.RLDIMI(result, tmp, 8, 48);
        code.ROTLDI(tmp, source, 24);
        code.RLDIMI(result, tmp, 16, 40);
        code.ROTLDI(tmp, source, 32);
        code.RLDIMI(result, tmp, 24, 32);
        code.ROTLDI(tmp, source, 48);
        code.RLDIMI(result, tmp, 40, 16);
        code.ROTLDI(tmp, source, 56);
        code.RLDIMI(result, tmp, 48, 8);
        code.RLDIMI(result, source, 56, 0);
    }
    ctx.reg_alloc.DefineValue(inst, result);
}

// __builtin_clz
template<>
void EmitIR<IR::Opcode::CountLeadingZeros32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const source = ctx.reg_alloc.UseGpr(args[0]);
    code.CNTLZW(result, source);
    ctx.reg_alloc.DefineValue(inst, result);
}

// __builtin_clz
template<>
void EmitIR<IR::Opcode::CountLeadingZeros64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const source = ctx.reg_alloc.UseGpr(args[0]);
    code.CNTLZD(result, source);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::ExtractRegister32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::ExtractRegister64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::ReplicateBit32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::ReplicateBit64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::MaxSigned32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const src_a = ctx.reg_alloc.UseGpr(args[0]);
    powah::GPR const src_b = ctx.reg_alloc.UseGpr(args[0]);
    code.CMPD(powah::CR0, result, src_a);
    code.ISELGT(result, result, src_b);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::MaxSigned64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const src_a = ctx.reg_alloc.UseGpr(args[0]);
    powah::GPR const src_b = ctx.reg_alloc.UseGpr(args[0]);
    code.CMPD(powah::CR0, result, src_a);
    code.ISELGT(result, result, src_b);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::MaxUnsigned32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const src_a = ctx.reg_alloc.UseGpr(args[0]);
    powah::GPR const src_b = ctx.reg_alloc.UseGpr(args[0]);
    code.CMPLW(result, src_a);
    code.ISELGT(result, result, src_b);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::MaxUnsigned64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const src_a = ctx.reg_alloc.UseGpr(args[0]);
    powah::GPR const src_b = ctx.reg_alloc.UseGpr(args[0]);
    code.CMPLD(result, src_a);
    code.ISELGT(result, result, src_b);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::MinSigned32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const src_a = ctx.reg_alloc.UseGpr(args[0]);
    powah::GPR const src_b = ctx.reg_alloc.UseGpr(args[0]);
    code.CMPW(powah::CR0, result, src_a);
    code.ISELGT(result, result, src_b);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::MinSigned64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const src_a = ctx.reg_alloc.UseGpr(args[0]);
    powah::GPR const src_b = ctx.reg_alloc.UseGpr(args[0]);
    code.CMPD(powah::CR0, result, src_a);
    code.ISELGT(result, result, src_b);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::MinUnsigned32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const src_a = ctx.reg_alloc.UseGpr(args[0]);
    powah::GPR const src_b = ctx.reg_alloc.UseGpr(args[0]);
    code.CMPLW(result, src_a);
    code.ISELGT(result, result, src_b);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::MinUnsigned64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    powah::GPR const result = ctx.reg_alloc.ScratchGpr();
    powah::GPR const src_a = ctx.reg_alloc.UseGpr(args[0]);
    powah::GPR const src_b = ctx.reg_alloc.UseGpr(args[0]);
    code.CMPLD(result, src_a);
    code.ISELGT(result, result, src_b);
    ctx.reg_alloc.DefineValue(inst, result);
}

}  // namespace Dynarmic::Backend::RV64
