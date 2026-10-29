// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/common_types.h"

namespace Shader::Ampere {

enum class OpKind : u8 {
    Plain,  // straight-line instruction
    Bra,    // branch (0x947)
    Exit,   // thread exit (0x94d)
    Kill,   // discard (0x95b)
    Bssy,   // push convergence barrier (0x945)
    Bsync,  // wait on convergence barrier (0x941)
    Brx,    // computed branch through a table (0x949)
    Call,   // call (0x944)
    Ret,    // return through a register (0x950)
    Nop,    // no effect in the IR model: BREAK, WARPSYNC, DEPBAR, NOP, ...
};

struct OpInfo {
    OpKind kind{OpKind::Plain};
    // A pure instruction only writes registers/predicates and cannot fault or have side effects, so a
    // non-trivial guard predicate is modelled as a select on its results instead of splitting the block.
    bool pure{};
    // Uniform datapath instruction: its guard predicate is a uniform predicate (UP0..UP6, UPT).
    bool uniform{};
};

// ALU-style encodings carry a form in bits 9..11 (1 reg, 2 imm in src2, 3 cbuf in src2, 4 imm,
// 5 cbuf, 6 uniform reg, 7 uniform reg in src2) on top of a 9 bit base operation.
[[nodiscard]] constexpr u32 FormOf(u32 op12) {
    return (op12 >> 9) & 7;
}
[[nodiscard]] constexpr u32 BaseOf(u32 op12) {
    return op12 & 0x1ff;
}

[[nodiscard]] constexpr OpInfo Classify(u32 op12) {
    switch (op12) {
    case 0x947: return {OpKind::Bra, false};
    case 0x94d: return {OpKind::Exit, false};
    case 0x95b: return {OpKind::Kill, false};
    case 0x945: return {OpKind::Bssy, false};
    case 0x941: return {OpKind::Bsync, false};
    case 0x949: return {OpKind::Brx, false};
    case 0x958: return {OpKind::Brx, false};
    case 0x944: return {OpKind::Call, false};
    case 0x950: return {OpKind::Ret, false};
    case 0x942: // BREAK
    case 0x948: // WARPSYNC
    case 0x918: // NOP
    case 0x91a: // DEPBAR: IR data dependencies replace scoreboards
        return {OpKind::Nop, false};
    // Pure attribute reads
    case 0x326: // IPA
    case 0x321: // ALD
    case 0xb82: // LDC
    case 0x805: // CS2R
    case 0x301: // BREV
    case 0x822: // FSWZADD
    case 0x3c2: // R2UR: reads the guard as data, so it is not split into a block
    case 0x919: // S2R
    case 0x803: // P2R
    case 0x804: // R2P
    case 0x81c: // PLOP3
        return {OpKind::Plain, true};
    // Uniform datapath: exact opcodes
    case 0x882: // UMOV imm
    case 0xc82: // UMOV UR
    case 0xab9: // ULDC
    case 0xabb: // ULDC.64
    case 0x9c3: // S2UR
    case 0x89c: // UPLOP3
    case 0x886: // VOTEU
        return {OpKind::Plain, true, true};
    default:
        break;
    }
    const u32 form = FormOf(op12);
    if (form == 0) return {};
    // Uniform ALU: UIADD3, ULEA, ULOP3, USHF, UIMAD, UISETP, USEL (register or immediate forms)
    if (form == 1 || form == 4) {
        switch (BaseOf(op12)) {
        case 0x090:
        case 0x091:
        case 0x092:
        case 0x099:
        case 0x0a4:
        case 0x0a5:
        case 0x0be:
        case 0x09a:
        case 0x0bd:
        case 0x0bf:
        case 0x08c:
        case 0x087:
            return {OpKind::Plain, true, true};
        default:
            break;
        }
    }
    switch (BaseOf(op12)) {
    case 0x020: // FMUL
    case 0x108: // MUFU
    case 0x002: // MOV
    case 0x021: // FADD
    case 0x023: // FFMA
    case 0x009: // FMNMX
    case 0x007: // SEL
    case 0x008: // FSEL
    case 0x00b: // FSETP
    case 0x00a: // FSET
    case 0x040: // HMNMX2
    case 0x00c: // ISETP
    case 0x010: // IADD3
    case 0x012: // LOP3
    case 0x019: // SHF
    case 0x011: // LEA
    case 0x017: // IMNMX
    case 0x016: // PRMT
    case 0x030: // HADD2
    case 0x031: // HFMA2
    case 0x032: // HMUL2
    case 0x024: // IMAD
    case 0x027: // IMAD.HI
    case 0x025: // IMAD.WIDE
    case 0x013: // IABS
    case 0x100: // FLO
    case 0x109: // POPC
    case 0x01a: // SGXT
    case 0x034: // HSETP2
    case 0x03e: // F2FP
    case 0x105: // F2I
    case 0x106: // I2F
    case 0x045: // I2FP
    case 0x107: // FRND
    case 0x104: // F2F
        return {OpKind::Plain, true};
    default:
        return {};
    }
}

} // namespace Shader::Ampere
