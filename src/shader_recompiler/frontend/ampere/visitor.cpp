// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// Encoding reference: Mesa NAK sm70_encode.rs (MIT), Copyright 2022 Collabora, Ltd.

#include <bit>
#include <string>

#include "shader_recompiler/frontend/ampere/isa.h"
#include "shader_recompiler/frontend/ampere/visitor.h"

namespace Shader::Ampere {

Visitor::Visitor(Environment& env_, IR::Block& block, const Instruction& instruction, u32 index_,
                 ProgramInfo* program_info_, const UniformLayout& layout_, bool probe_)
    : env{env_}, ir{block}, insn{instruction}, index{index_}, program_info{program_info_},
      layout{layout_}, probe{probe_}, op{instruction.Bits(0, 12)} {
    // Opcode, guard predicate and the scheduling word (105..127) are never instruction semantics.
    consumed = {0x0000ffffU, 0, 0, 0xfffffe00U};
}

u32 Visitor::Take(u32 lo, u32 count) {
    for (u32 bit = lo; bit < lo + count; ++bit) {
        consumed[bit / 32] |= 1U << (bit % 32);
    }
    return insn.Bits(lo, count);
}

void Visitor::Ignore(u32 lo, u32 count) {
    for (u32 bit = lo; bit < lo + count; ++bit) {
        consumed[bit / 32] |= 1U << (bit % 32);
    }
}

void Visitor::Unsupported(std::string_view reason) const {
    throw UnsupportedError(index, reason);
}

IR::U32 Visitor::X(u32 reg) {
    if (reg >= 255) {
        return ir.Imm32(0U);
    }
    NoteGpr(reg);
    return ir.GetReg(static_cast<IR::Reg>(reg));
}

IR::F32 Visitor::F(u32 reg) {
    return ir.BitCast<IR::F32>(X(reg));
}

void Visitor::X(u32 reg, const IR::U32& value) {
    if (reg >= 255) {
        return; // RZ discards
    }
    NoteGpr(reg);
    const IR::Reg dest = static_cast<IR::Reg>(reg);
    if (guard_select) {
        ir.SetReg(dest, IR::U32{ir.Select(*guard_select, value, ir.GetReg(dest))});
    } else {
        ir.SetReg(dest, value);
    }
}

void Visitor::F(u32 reg, const IR::F32& value) {
    X(reg, ir.BitCast<IR::U32>(value));
}

IR::U1 Visitor::Pred(u32 pred_index, bool negated) {
    if (pred_index == 7) {
        return ir.Imm1(!negated);
    }
    return ir.GetPred(static_cast<IR::Pred>(pred_index), negated);
}

void Visitor::SetPred(u32 pred_index, const IR::U1& value) {
    if (pred_index == 7) {
        return; // PT discards
    }
    const IR::Pred dest = static_cast<IR::Pred>(pred_index);
    if (guard_select) {
        ir.SetPred(dest, IR::U1{ir.Select(*guard_select, value, ir.GetPred(dest))});
    } else {
        ir.SetPred(dest, value);
    }
}

void Visitor::NoteGpr(u32 reg) {
    if (probe) {
        if (program_info) {
            program_info->max_gpr = (std::max)(program_info->max_gpr, reg);
            program_info->used_gprs[reg] = true;
        }
        return;
    }
    Require(layout.scattered ? !layout.reserved[reg] : reg < layout.gpr_limit,
            "native program uses registers reserved for the uniform file");
}

IR::Reg Visitor::UniformSlot(u32 reg) {
    if (probe) {
        if (program_info) {
            program_info->uses_any_ur = true;
            program_info->max_ur = (std::max)(program_info->max_ur, reg);
        }
        return static_cast<IR::Reg>(UNIFORM_REGISTER_BASE + reg); // the probe result is discarded
    }
    return static_cast<IR::Reg>(layout.scattered ? layout.slots[reg] : layout.register_base + reg);
}

IR::Reg Visitor::PredicateSlot() {
    if (probe) {
        if (program_info) program_info->uses_up = true;
        return static_cast<IR::Reg>(UNIFORM_PREDICATE_SLOT);
    }
    return static_cast<IR::Reg>(layout.predicate_slot);
}

IR::U32 Visitor::UR(u32 reg) {
    if (reg >= 63) {
        return ir.Imm32(0U); // URZ
    }
    return ir.GetReg(UniformSlot(reg));
}

void Visitor::UR(u32 reg, const IR::U32& value) {
    if (reg >= 63) {
        return;
    }
    const IR::Reg dest = UniformSlot(reg);
    if (guard_select) {
        ir.SetReg(dest, IR::U32{ir.Select(*guard_select, value, ir.GetReg(dest))});
    } else {
        ir.SetReg(dest, value);
    }
}

IR::U1 Visitor::UPred(u32 pred_index, bool negated) {
    if (pred_index >= 7) {
        return ir.Imm1(!negated);
    }
    const IR::U32 bits{ir.GetReg(PredicateSlot())};
    const IR::U1 bit{ir.INotEqual(ir.BitFieldExtract(bits, ir.Imm32(pred_index), ir.Imm32(1U)), ir.Imm32(0U))};
    return negated ? IR::U1{ir.LogicalNot(bit)} : bit;
}

void Visitor::SetUPred(u32 pred_index, const IR::U1& value) {
    if (pred_index >= 7) {
        return; // UPT discards
    }
    const IR::Reg slot = PredicateSlot();
    const IR::U32 old{ir.GetReg(slot)};
    IR::U1 final_value{value};
    if (guard_select) {
        final_value = IR::U1{ir.Select(*guard_select, value, UPred(pred_index, false))};
    }
    const IR::U32 bit{ir.Select(final_value, ir.Imm32(1U), ir.Imm32(0U))};
    ir.SetReg(slot, ir.BitFieldInsert(old, bit, ir.Imm32(pred_index), ir.Imm32(1U)));
}

u32 Visitor::Dst() {
    return Take(16, uniform_op ? 6 : 8);
}

void Visitor::WriteDst(u32 reg, const IR::U32& value) {
    if (uniform_op) {
        UR(reg, value);
    } else {
        X(reg, value);
    }
}

IR::U1 Visitor::PredIn(u32 pred_index, bool negated) {
    return uniform_op ? UPred(pred_index, negated) : Pred(pred_index, negated);
}

void Visitor::PredOut(u32 pred_index, const IR::U1& value) {
    if (uniform_op) {
        SetUPred(pred_index, value);
    } else {
        SetPred(pred_index, value);
    }
}

IR::U32 Visitor::Cbuf(u32 cbuf_index, u32 byte_offset) {
    if (cbuf_index >= 32) { // banks 24..31 are the native root tables
        Unsupported("native constant buffer address");
    }
    const u32 skew = byte_offset % 4;
    if (skew == 0) {
        return ir.GetCbuf(ir.Imm32(cbuf_index), ir.Imm32(byte_offset));
    }
    // A byte address inside a word reads 32 bits starting there: the tail of the word it falls in
    // and the head of the next one (little endian).
    const IR::U32 low{ir.GetCbuf(ir.Imm32(cbuf_index), ir.Imm32(byte_offset - skew))};
    const IR::U32 high{ir.GetCbuf(ir.Imm32(cbuf_index), ir.Imm32(byte_offset - skew + 4))};
    return ir.BitwiseOr(ir.ShiftRightLogical(low, ir.Imm32(8 * skew)),
                        ir.ShiftLeftLogical(high, ir.Imm32(32 - 8 * skew)));
}

Operand Visitor::WideSlot(bool modifiers) {
    Operand operand;
    switch (Form()) {
    case 2:
    case 4:
        operand.kind = Operand::Kind::Imm;
        operand.imm = Take(32, 32);
        break;
    case 3:
    case 5:
        operand.kind = Operand::Kind::Cbuf;
        Require(Take(91, 1) == 0, "native bindless constant buffer");
        operand.cbuf_index = Take(54, 5);
        operand.cbuf_offset = Take(38, 16);
        if (modifiers) {
            operand.abs = Take(62, 1) != 0;
            operand.negate = Take(63, 1) != 0;
        }
        break;
    case 6:
    case 7:
        operand.kind = Operand::Kind::Ureg;
        Require(Take(91, 1) == 1, "native uniform source flag");
        operand.reg = Take(32, 6);
        if (modifiers) {
            operand.abs = Take(62, 1) != 0;
            operand.negate = Take(63, 1) != 0;
        }
        break;
    default:
        Unsupported("native source form");
    }
    return operand;
}

Operand Visitor::Source(u32 which, bool modifiers) {
    Operand operand;
    const u32 form = Form();
    Require(form >= 1 && form <= 7, "native ALU source form");
    const u32 reg_bits = uniform_op ? 6 : 8;
    if (uniform_op) {
        operand.kind = Operand::Kind::Ureg;
    }
    if (which == 0) {
        operand.reg = Take(24, reg_bits);
        if (modifiers) {
            operand.negate = Take(72, 1) != 0;
            operand.abs = Take(73, 1) != 0;
        }
        return operand;
    }
    // Sources 1 and 2 share two physical slots: the wide slot (32..63) and a register in 64..71.
    const bool wide = which == 1 ? (form == 4 || form == 5 || form == 6)
                                 : (form == 2 || form == 3 || form == 7);
    if (wide) {
        return WideSlot(modifiers);
    }
    if (which == 1 && form == 1) {
        operand.reg = Take(32, reg_bits);
        if (modifiers) {
            operand.abs = Take(62, 1) != 0;
            operand.negate = Take(63, 1) != 0;
        }
    } else {
        operand.reg = Take(64, reg_bits);
        if (modifiers) {
            operand.abs = Take(74, 1) != 0;
            operand.negate = Take(75, 1) != 0;
        }
    }
    return operand;
}

IR::U32 Visitor::Value(const Operand& operand) {
    switch (operand.kind) {
    case Operand::Kind::Reg:
        return X(operand.reg);
    case Operand::Kind::Ureg:
        return UR(operand.reg);
    case Operand::Kind::Imm:
        return ir.Imm32(operand.imm);
    case Operand::Kind::Cbuf:
        return Cbuf(operand.cbuf_index, operand.cbuf_offset);
    }
    Unsupported("native operand kind");
}

IR::F32 Visitor::FloatValue(const Operand& operand, bool apply_modifiers) {
    IR::F32 value{ir.BitCast<IR::F32>(Value(operand))};
    if (apply_modifiers && (operand.abs || operand.negate)) {
        value = ir.FPAbsNeg(value, operand.abs, operand.negate);
    }
    return value;
}

void Visitor::Run() {
    const Guard guard = GuardOf(insn);
    if (guard.Never()) {
        return;
    }
    const OpInfo info = Classify(op);
    uniform_op = info.uniform;
    if (info.pure && !guard.Always()) {
        guard_select = info.uniform ? UPred(guard.index, guard.negated)
                                    : Pred(guard.index, guard.negated);
    }
    // ALU-style encodings are dispatched on their 9 bit base operation.
    if (info.pure && Form() >= 1 && Form() <= 7 && op != 0x202 && op != 0x802 && op != 0xa02 &&
        op != 0xc02 && !(info.uniform && (op == 0x882 || op == 0xc82 || op == 0xab9 || op == 0xabb ||
                                          op == 0x9c3 || op == 0x89c || op == 0x886))) {
        if (info.uniform) {
            Require(Take(91, 1) == 1, "native uniform datapath flag");
        }
        bool handled = true;
        switch (BaseOf(op)) {
        case 0x090: IADD3(); break;
        case 0x091: LEA(); break;
        case 0x092: LOP3(); break;
        case 0x099: SHF(); break;
        case 0x0a4: IMAD(); break;
        case 0x08c: ISETP(); break;
        case 0x087: SEL(false); break;
        case 0x021: FADD(); break;
        case 0x023: FFMA(); break;
        case 0x009: FMNMX(); break;
        case 0x007: SEL(false); break;
        case 0x008: SEL(true); break;
        case 0x00b: FSETP(); break;
        case 0x00a: FSET(); break;
        case 0x040: HMNMX2(); break;
        case 0x013: IABS(); break;
        case 0x100: FLO(); break;
        case 0x0bd: FLO(); break;
        case 0x109: POPC(); break;
        case 0x0bf: POPC(); break;
        case 0x01a: SGXT(); break;
        case 0x09a: SGXT(); break;
        case 0x0be: BREV(); break;
        case 0x00c: ISETP(); break;
        case 0x010: IADD3(); break;
        case 0x012: LOP3(); break;
        case 0x019: SHF(); break;
        case 0x011: LEA(); break;
        case 0x017: IMNMX(); break;
        case 0x016: PRMT(); break;
        case 0x030: HADD2(); break;
        case 0x031: HFMA2(); break;
        case 0x032: HMUL2(); break;
        case 0x024: IMAD(); break;
        case 0x027: IMAD_HI(); break;
        case 0x025: IMAD_WIDE(); break;
        case 0x0a5: IMAD_WIDE(); break;
        case 0x034: HSETP2(); break;
        case 0x03e: F2FP(); break;
        case 0x105: F2I(); break;
        case 0x106: case 0x045: I2F(); break;
        case 0x107: FRND(); break;
        case 0x104: F2F(); break;
        default: handled = false; break;
        }
        if (handled) {
            CheckConsumed();
            return;
        }
    }
    switch (op) {
    case 0x202:
    case 0x802:
    case 0xa02:
    case 0xc02:
        MOV();
        break;
    case 0x882:
    case 0xc82:
        UMOV();
        break;
    case 0xab9:
    case 0xabb:
        ULDC();
        break;
    case 0x89c:
        PLOP3();
        break;
    case 0x220:
    case 0x820:
    case 0xa20:
    case 0xc20:
        FMUL();
        break;
    case 0x308:
    case 0xb08:
    case 0xd08:
        MUFU();
        break;
    case 0x326:
        IPA();
        break;
    case 0xb60:
    case 0xf60:
    case 0x361:
        TEX();
        break;
    case 0xb66:
    case 0xf66:
        TLD();
        break;
    case 0xb6c:
        TXD();
        break;
    case 0xb69:
        TMML();
        break;
    case 0x822:
        FSWZADD();
        break;
    case 0x301:
        BREV();
        break;
    case 0xfae:
        LDGSTS();
        break;
    case 0x9af:
        ir.WorkgroupMemoryBarrier();
        break;
    case 0x806:
        VOTE();
        break;
    case 0x886:
        VOTEU();
        break;
    case 0xb1d:
        BAR();
        break;
    case 0x9ab: // ERRBAR: kept as a device memory barrier, like the lowering
        ir.DeviceMemoryBarrier();
        break;
    case 0x992:
        MEMBAR();
        break;
    case 0xb6f:
        TXQ();
        break;
    case 0xf8c:
        ATOMS_POPC();
        break;
    case 0xf89:
    case 0x589:
        SHFL();
        break;
    case 0x983:
        LDL();
        break;
    case 0x387:
        STL();
        break;
    case 0x226:
        IDP();
        break;
    case 0x38c:
        ATOMS();
        break;
    case 0x3c4:
        REDUX();
        break;
    case 0x3a1:
        MATCH();
        break;
    case 0x923:
        ISBERD();
        break;
    case 0x927:
        ISBEWR();
        break;
    case 0x3a8:
    case 0x9a8:
    case 0x98e:
        ATOMG();
        break;
    case 0x83b:
        LDSM();
        break;
    case 0x23a:
        MOVM();
        break;
    case 0x23c:
        HMMA();
        break;
    case 0x984:
        LDS();
        break;
    case 0x388:
        STS();
        break;
    case 0x3c2:
        R2UR();
        break;
    case 0x381:
    case 0x981:
        LDG();
        break;
    case 0x386:
    case 0x986:
        STG();
        break;
    case 0xb9b:
        SUST();
        break;
    case 0x593:
        SUATOM();
        break;
    case 0xb97:
        SULD();
        break;
    case 0xb63:
    case 0xf63:
        TLD4();
        break;
    case 0x321:
        ALD();
        break;
    case 0x322:
        AST();
        break;
    case 0xb82:
        LDC();
        break;
    case 0x805:
        CS2R();
        break;
    case 0x919:
    case 0x9c3:
        S2R();
        break;
    case 0x94d:
        EXIT();
        break;
    case 0x803:
        P2R();
        break;
    case 0x804:
        R2P();
        break;
    case 0x81c: // PLOP3
        PLOP3();
        break;
    case 0x918: // NOP
        break;
    case 0x942: // BREAK Bx: only drops threads from the barrier; the following BRA leaves the loop
        Take(16, 4);
        Take(87, 4); // the predicate only chooses which threads leave; threads run independently here
        break;
    case 0x948: // WARPSYNC with the full-warp mask: lanes are executed independently
        Require(Take(32, 32) == 0xffffffffU, "native partial warp sync");
        Require(Take(87, 4) == 7, "native predicated warp sync");
        break;
    case 0x91a: // DEPBAR.LE SB5, 1: scoreboards are replaced by IR data dependencies
        Take(38, 6);
        Require(Take(44, 4) == 0xd, "native dependency barrier mode");
        break;
    case 0x95b: // KILL: the discard itself is a block end class
        Require(Take(87, 4) == 7, "native predicated KILL");
        break;
    case 0x944: // CALL: resolved by the control flow builder
        Take(34, 48);
        Require(Take(86, 1) == 1, "native call increment mode");
        Take(87, 4);
        break;
    case 0x950: // RET.REL: the return address register was validated by the control flow analysis
        Take(24, 8);
        Take(34, 48);
        Require(Take(86, 1) == 1, "native return decrement mode");
        Take(87, 4);
        break;
    case 0x958: // BRXU: resolved by the control flow builder
        Take(24, 6);
        Take(34, 48);
        Take(87, 4);
        Take(91, 1);
        break;
    case 0x949: // BRX: resolved by the control flow builder
        Take(24, 8);
        Take(34, 48);
        Take(87, 4);
        break;
    case 0x945: // BSSY / BSYNC are control flow only
    case 0x941:
        Take(16, 4);
        Take(34, 30);
        Take(87, 4);
        break;
    default:
        Unsupported(fmt::format("unsupported native opcode {:#x}", op));
    }
    CheckConsumed();
}

void Visitor::CheckConsumed() {
    for (u32 word = 0; word < 4; ++word) {
        const u32 stray = insn.words[word] & ~consumed[word];
        if (stray) {
            Unsupported(fmt::format("op {:#x}: bit {} not modelled", op, word * 32 + std::countr_zero(stray)));
        }
    }
}

} // namespace Shader::Ampere
