// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <powah_emit.hpp>
#include <fmt/ostream.h>

#include "abi.h"
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

// uint64_t pack2x1(uint32_t lo, uint32_t hi) { return (uint64_t)lo | ((uint64_t)hi << 32); }
template<>
void EmitIR<IR::Opcode::Pack2x32To1x64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const lo = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    auto const hi = ctx.reg_alloc.UseGpr(inst->GetArg(1));
    auto const result = ctx.reg_alloc.ScratchGpr();
    code.SLDI(result, hi, 32);
    code.OR(result, result, lo);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::Pack2x64To1x128>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

// uint64_t lsw(uint64_t a) { return (uint32_t)a; }
template<>
void EmitIR<IR::Opcode::LeastSignificantWord>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const source = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    code.RLDICL(result, source, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

// uint64_t f(uint64_t a) { return (uint16_t)a; }
template<>
void EmitIR<IR::Opcode::LeastSignificantHalf>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const source = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    code.RLWINM(result, source, 0, 0xffff);
    ctx.reg_alloc.DefineValue(inst, result);
}

// uint64_t f(uint64_t a) { return (uint8_t)a; }
template<>
void EmitIR<IR::Opcode::LeastSignificantByte>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const source = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    code.RLWINM(result, source, 0, 0xff);
    ctx.reg_alloc.DefineValue(inst, result);
}

// uint64_t msw(uint64_t a) { return a >> 32; }
template<>
void EmitIR<IR::Opcode::MostSignificantWord>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const source = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    code.SRDI(result, source, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

/*
uint64_t f(uint64_t a) { return ((uint32_t)a) >> 31; }
*/
template<>
void EmitIR<IR::Opcode::MostSignificantBit>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const source = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    code.RLWINM(result, source, 1, 31, 31);
    ctx.reg_alloc.DefineValue(inst, result);
}

/*
uint64_t f(uint64_t a) { return (uint32_t)a == 0; }
*/
template<>
void EmitIR<IR::Opcode::IsZero32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const source = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    code.CNTLZW(result, source);
    code.SRWI(result, result, 5);
    ctx.reg_alloc.DefineValue(inst, result);
}

/*
uint64_t f(uint64_t a) { return a == 0; }
*/
template<>
void EmitIR<IR::Opcode::IsZero64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const source = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    code.CNTLZD(result, source);
    code.SRDI(result, result, 6);
    ctx.reg_alloc.DefineValue(inst, result);
}

/*
uint64_t f(uint64_t a) { return (a & 1) != 0; }
*/
template<>
void EmitIR<IR::Opcode::TestBit>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const source = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    if (inst->GetArg(1).IsImmediate()) {
        auto const shift = inst->GetArg(1).GetImmediateAsU64();
        if (shift > 0) {
            code.RLDICL(result, source, (64 - shift - 1) & 0x3f, 63);
        }
    } else {
        ASSERT(false && "unimp");
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
    auto const nzcv = ctx.reg_alloc.ScratchGpr();
    auto const then_ = ctx.reg_alloc.UseGpr(inst->GetArg(1));
    auto const else_ = ctx.reg_alloc.UseGpr(inst->GetArg(2));
    switch (inst->GetArg(0).GetCond()) {
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
        auto const tmp = ctx.reg_alloc.ScratchGpr();
        code.LWZ(nzcv, PPC64::RJIT, offsetof(A32JitState, cpsr_nzcv));
        code.SRWI(tmp, nzcv, 3);
        code.XOR(nzcv, tmp, nzcv);
        code.ANDI_(nzcv, nzcv, 1);
        code.ISELGT(nzcv, else_, then_);
    } break;
    case IR::Cond::LT: { // N != V
        auto const tmp = ctx.reg_alloc.ScratchGpr();
        code.LWZ(nzcv, PPC64::RJIT, offsetof(A32JitState, cpsr_nzcv));
        code.SRWI(tmp, nzcv, 3);
        code.XOR(nzcv, tmp, nzcv);
        code.ANDI_(nzcv, nzcv, 1);
        code.ISELGT(nzcv, then_, else_);
    } break;
    case IR::Cond::GT: { // Z == 0 && N == V
        auto const tmp = ctx.reg_alloc.ScratchGpr();
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
        auto const tmp = ctx.reg_alloc.ScratchGpr();
        auto const tmp2 = ctx.reg_alloc.ScratchGpr();
        powah::Label const l_ne = code.DefineLabel();
        code.MR(tmp2, then_);
        code.LD(nzcv, PPC64::RJIT, offsetof(A32JitState, cpsr_nzcv));
        code.ANDI_(tmp, nzcv, 4);
        code.BNE(powah::CR0, l_ne);
        code.SRWI(tmp, nzcv, 3);
        code.XOR(nzcv, tmp, nzcv);
        code.ANDI_(nzcv, nzcv, 1);
        code.ISELGT(tmp2, then_, else_);
        code.LABEL(l_ne);
        code.MR(nzcv, tmp2);
    } break;
    default:
        ASSERT(false && "unimp");
    }
    return nzcv;
}

template<>
void EmitIR<IR::Opcode::ConditionalSelect32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = EmitConditionalSelectX(code, ctx, inst);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::ConditionalSelect64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = EmitConditionalSelectX(code, ctx, inst);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::ConditionalSelectNZCV>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = EmitConditionalSelectX(code, ctx, inst);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::LogicalShiftLeft32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const source = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    auto const shift = ctx.reg_alloc.UseGpr(inst->GetArg(1));
    code.SLW(result, source, shift);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::LogicalShiftLeft64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const source = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    auto const shift = ctx.reg_alloc.UseGpr(inst->GetArg(1));
    code.SLD(result, source, shift);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::LogicalShiftRight32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const source = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    auto const shift = ctx.reg_alloc.UseGpr(inst->GetArg(1));
    code.SRW(result, source, shift);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

/*
uint64_t f(uint64_t a, uint64_t s) { return a >> s; }
*/
template<>
void EmitIR<IR::Opcode::LogicalShiftRight64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const source = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    auto const shift = ctx.reg_alloc.UseGpr(inst->GetArg(1));
    code.SRD(result, source, shift);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::ArithmeticShiftRight32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const source = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    auto const shift = ctx.reg_alloc.UseGpr(inst->GetArg(1));
    code.SRAW(result, source, shift);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::ArithmeticShiftRight64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const source = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    auto const shift = ctx.reg_alloc.UseGpr(inst->GetArg(1));
    code.SRAD(result, source, shift);
    ctx.reg_alloc.DefineValue(inst, result);
}

// __builtin_rotateright32
template<>
void EmitIR<IR::Opcode::RotateRight32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const src_a = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    auto const src_b = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    code.NEG(result, src_a, powah::R0);
    code.ROTLW(result, result, src_b);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::RotateRight64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const src_a = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    auto const src_b = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    code.NEG(result, src_a, powah::R0);
    code.ROTLD(result, result, src_b);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::RotateRightExtended>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const src_a = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    auto const src_b = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    code.NEG(result, src_a, powah::R0);
    code.ROTLD(result, result, src_b);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::LogicalShiftLeftMasked32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const source = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    code.SLW(result, source, source);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::LogicalShiftLeftMasked64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const source = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    code.SLD(result, source, source);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::LogicalShiftRightMasked32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const source = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    code.SRW(result, source, source);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::LogicalShiftRightMasked64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const source = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    code.SRD(result, source, source);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::ArithmeticShiftRightMasked32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const source = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    code.SRAW(result, source, source);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::ArithmeticShiftRightMasked64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const source = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    code.SRAD(result, source, source);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::RotateRightMasked32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const src_a = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    auto const src_b = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    code.NEG(result, src_a, powah::R0);
    code.ROTLD(result, result, src_b);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::RotateRightMasked64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const src_a = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    auto const src_b = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    code.NEG(result, src_a, powah::R0);
    code.ROTLD(result, result, src_b);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::Add32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const src_a = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    auto const src_b = ctx.reg_alloc.UseGpr(inst->GetArg(1));
    code.ADDC_(result, src_a, src_b);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

/*
struct jit {
    uint32_t nzcv;
};
uint64_t addc(jit *p, uint64_t a, uint64_t b) {
    long long unsigned int e;
    uint64_t r = __builtin_addcll(a, b, p->nzcv & 0b0010, &e);
    p->nzcv = (p->nzcv & 0b1101) | e;
    return r;
}
*/
template<>
void EmitIR<IR::Opcode::Add64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const src_a = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    auto const src_b = ctx.reg_alloc.UseGpr(inst->GetArg(1));
    code.ADDC_(result, src_a, src_b);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::Sub32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const src_a = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    auto const src_b = ctx.reg_alloc.UseGpr(inst->GetArg(1));
    code.SUBFC_(result, src_b, src_a);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::Sub64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const src_a = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    auto const src_b = ctx.reg_alloc.UseGpr(inst->GetArg(1));
    code.SUBFC_(result, src_b, src_a);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::Mul32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const src_a = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    auto const src_b = ctx.reg_alloc.UseGpr(inst->GetArg(1));
    code.MULLWO_(result, src_a, src_b);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::Mul64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const src_a = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    auto const src_b = ctx.reg_alloc.UseGpr(inst->GetArg(1));
    code.MULLDO_(result, src_a, src_b);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::SignedMultiplyHigh64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const src_a = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    auto const src_b = ctx.reg_alloc.UseGpr(inst->GetArg(1));
    code.MULLDO_(result, src_a, src_b);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::UnsignedMultiplyHigh64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const src_a = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    auto const src_b = ctx.reg_alloc.UseGpr(inst->GetArg(1));
    code.MULLDO_(result, src_a, src_b);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::UnsignedDiv32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const src_a = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    auto const src_b = ctx.reg_alloc.UseGpr(inst->GetArg(1));
    code.DIVWU_(result, src_a, src_b);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::UnsignedDiv64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const src_a = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    auto const src_b = ctx.reg_alloc.UseGpr(inst->GetArg(1));
    code.DIVDU_(result, src_a, src_b);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::SignedDiv32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const src_a = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    auto const src_b = ctx.reg_alloc.UseGpr(inst->GetArg(1));
    code.DIVW_(result, src_a, src_b);
    code.EXTSW(result, result);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::SignedDiv64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const src_a = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    auto const src_b = ctx.reg_alloc.UseGpr(inst->GetArg(1));
    code.DIVD_(result, src_a, src_b);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::And32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const src_a = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    auto const src_b = ctx.reg_alloc.UseGpr(inst->GetArg(1));
    auto const tmp3 = ctx.reg_alloc.ScratchGpr();
    auto const tmp8 = ctx.reg_alloc.ScratchGpr();
    auto const tmp9 = PPC64::RNZCV;
    code.RLDICL(tmp3, src_a, 0, 32); // Truncate
    code.AND(tmp3, tmp3, src_b);
    code.CNTLZD(tmp9, tmp3);
    code.RLWINM(tmp8, tmp3, 0, 0, 0);
    code.SRDI(tmp9, tmp9, 6);
    code.SLDI(tmp9, tmp9, 30);
    code.OR(tmp9, tmp9, tmp8);
    ctx.reg_alloc.DefineValue(inst, tmp3);
}

template<>
void EmitIR<IR::Opcode::And64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const src_a = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    auto const src_b = ctx.reg_alloc.UseGpr(inst->GetArg(1));
    auto const tmp3 = ctx.reg_alloc.ScratchGpr();
    auto const tmp10 = ctx.reg_alloc.ScratchGpr();
    auto const tmp9 = PPC64::RNZCV;
    code.AND(tmp3, src_a, src_b);
    code.CNTLZD(tmp10, tmp3);
    code.SRADI(tmp9, tmp3, 32);
    code.SRDI(tmp10, tmp10, 6);
    code.RLWINM(tmp9, tmp9, 0, 0, 0);
    code.SLDI(tmp10, tmp10, 30);
    code.OR(tmp9, tmp9, tmp10);
    ctx.reg_alloc.DefineValue(inst, tmp3);
}

template<>
void EmitIR<IR::Opcode::AndNot32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const src_a = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    auto const src_b = ctx.reg_alloc.UseGpr(inst->GetArg(1));
    code.NAND_(result, src_a, src_b);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::AndNot64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const src_a = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    auto const src_b = ctx.reg_alloc.UseGpr(inst->GetArg(1));
    code.NAND_(result, src_a, src_b);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::Eor32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const src_a = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    auto const src_b = ctx.reg_alloc.UseGpr(inst->GetArg(1));
    code.XOR_(result, src_a, src_b);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::Eor64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const src_a = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    auto const src_b = ctx.reg_alloc.UseGpr(inst->GetArg(1));
    code.XOR_(result, src_a, src_b);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::Or32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const src_a = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    auto const src_b = ctx.reg_alloc.UseGpr(inst->GetArg(1));
    code.OR_(result, src_a, src_b);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::Or64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const src_a = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    auto const src_b = ctx.reg_alloc.UseGpr(inst->GetArg(1));
    code.OR_(result, src_a, src_b);
    ctx.reg_alloc.DefineValue(inst, result);
}

// TODO(lizzie): NZVC support for NOT
template<>
void EmitIR<IR::Opcode::Not32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const source = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    code.NOT(result, source);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::Not64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const source = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    code.NOT(result, source);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::SignExtendByteToWord>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const source = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    code.EXTSB(result, source);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::SignExtendHalfToWord>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const source = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    code.EXTSH(result, source);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::SignExtendByteToLong>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const source = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    code.EXTSH(result, source);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::SignExtendHalfToLong>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const source = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    code.EXTSB(result, source);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::SignExtendWordToLong>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const source = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    code.EXTSW(result, source);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::ZeroExtendByteToWord>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const source = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    code.RLWINM(result, source, 0, 0xff);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::ZeroExtendHalfToWord>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const source = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    code.RLWINM(result, source, 0, 0xffff);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::ZeroExtendByteToLong>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const source = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    code.RLWINM(result, source, 0, 0xff);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::ZeroExtendHalfToLong>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const source = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    code.RLWINM(result, source, 0, 0xffff);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::ZeroExtendWordToLong>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const source = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    code.RLDICL(result, source, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::ZeroExtendLongToQuad>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

// __builtin_bswap32
template<>
void EmitIR<IR::Opcode::ByteReverseWord>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const source = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    if (false) {
        //code.BRW(result, source);
        //code.RLDICL(result, result, 0, 32);
    } else {
        auto const result = ctx.reg_alloc.ScratchGpr();
        code.ROTLWI(result, source, 24);
        code.RLWIMI(result, source, 8, 8, 15);
        code.RLWIMI(result, source, 8, 24, 31);
        code.RLDICL(result, result, 0, 32);
        ctx.reg_alloc.DefineValue(inst, result);
    }
}

// __builtin_bswap64
template<>
void EmitIR<IR::Opcode::ByteReverseHalf>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const source = ctx.reg_alloc.UseGpr(inst->GetArg(0));
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
    auto const source = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    if (false) {
        //code.BRD(result, source);
    } else {
        auto const tmp10 = ctx.reg_alloc.ScratchGpr();
        auto const tmp9 = ctx.reg_alloc.ScratchGpr();
        auto const tmp3 = ctx.reg_alloc.ScratchGpr();
        code.MR(tmp3, source);
        code.ROTLWI(tmp10, tmp3, 24);
        code.SRDI(tmp9, tmp3, 32);
        code.RLWIMI(tmp10, tmp3, 8, 8, 15);
        code.RLWIMI(tmp10, tmp3, 8, 24, 31);
        code.ROTLWI(tmp3, tmp9, 24);
        code.RLWIMI(tmp3, tmp9, 8, 8, 15);
        code.RLWIMI(tmp3, tmp9, 8, 24, 31);
        code.RLDIMI(tmp3, tmp10, 32, 0);
        ctx.reg_alloc.DefineValue(inst, tmp3);
    }
}

// __builtin_clz
template<>
void EmitIR<IR::Opcode::CountLeadingZeros32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const source = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    code.CNTLZW(result, source);
    ctx.reg_alloc.DefineValue(inst, result);
}

// __builtin_clz
template<>
void EmitIR<IR::Opcode::CountLeadingZeros64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const source = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    code.CNTLZD(result, source);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::ExtractRegister32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::ExtractRegister64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::ReplicateBit32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::ReplicateBit64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(false && "unimp");
}

template<>
void EmitIR<IR::Opcode::MaxSigned32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const src_a = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    auto const src_b = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    code.CMPD(powah::CR0, result, src_a);
    code.ISELGT(result, result, src_b);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::MaxSigned64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const src_a = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    auto const src_b = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    code.CMPD(powah::CR0, result, src_a);
    code.ISELGT(result, result, src_b);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::MaxUnsigned32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const src_a = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    auto const src_b = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    code.CMPLW(result, src_a);
    code.ISELGT(result, result, src_b);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::MaxUnsigned64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const src_a = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    auto const src_b = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    code.CMPLD(result, src_a);
    code.ISELGT(result, result, src_b);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::MinSigned32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const src_a = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    auto const src_b = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    code.CMPW(powah::CR0, result, src_a);
    code.ISELGT(result, result, src_b);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::MinSigned64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const src_a = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    auto const src_b = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    code.CMPD(powah::CR0, result, src_a);
    code.ISELGT(result, result, src_b);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::MinUnsigned32>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const src_a = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    auto const src_b = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    code.CMPLW(result, src_a);
    code.ISELGT(result, result, src_b);
    code.RLDICL(result, result, 0, 32);
    ctx.reg_alloc.DefineValue(inst, result);
}

template<>
void EmitIR<IR::Opcode::MinUnsigned64>(powah::Context& code, EmitContext& ctx, IR::Inst* inst) {
    auto const result = ctx.reg_alloc.ScratchGpr();
    auto const src_a = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    auto const src_b = ctx.reg_alloc.UseGpr(inst->GetArg(0));
    code.CMPLD(result, src_a);
    code.ISELGT(result, result, src_b);
    ctx.reg_alloc.DefineValue(inst, result);
}

}  // namespace Dynarmic::Backend::RV64
