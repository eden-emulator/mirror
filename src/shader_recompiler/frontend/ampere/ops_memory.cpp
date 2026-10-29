// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// Encoding reference: Mesa NAK sm70_encode.rs (MIT), Copyright 2022 Collabora, Ltd.

#include "shader_recompiler/frontend/ampere/visitor.h"

namespace Shader::Ampere {
namespace {
// Access types of LDG/STG (NAK MemType).
enum class MemType : u32 { U8, S8, U16, S16, B32, B64, B128 };
} // Anonymous namespace

// 64-bit global address: optional register (pair), optional uniform register (pair) and a signed
// 24-bit byte offset. Pairs are added as two 32-bit words with an explicit carry so that the address
// stays a Pack(Composite(lo, hi)) the storage buffer pass can follow back to its constant buffer.
IR::U64 Visitor::GlobalAddress(u32 reg, bool reg_pair, bool use_uniform, u32 uniform, bool uniform_pair,
                               u32 offset) {
    if (use_uniform) {
        Require(uniform < 63 && (!uniform_pair || (uniform % 2 == 0 && uniform + 1 < 63)),
                "native global uniform address");
    }
    const bool have_reg = reg != 255;
    if (have_reg) {
        Require(reg < 255 && (!reg_pair || (reg % 2 == 0 && reg + 1 < 255)),
                "native global register address");
    }
    const auto uniform_word = [&](u32 k) -> IR::U32 {
        return k == 1 && !uniform_pair ? ir.Imm32(0U) : UR(uniform + k);
    };
    const auto reg_word = [&](u32 k) -> IR::U32 {
        return k == 1 && !reg_pair ? ir.Imm32(0U) : X(reg + k);
    };
    IR::U32 low;
    IR::U32 high;
    if (use_uniform && have_reg) {
        const IR::U32 a_low{uniform_word(0)};
        const IR::U32 a_high{uniform_word(1)};
        low = ir.IAdd(a_low, reg_word(0));
        const IR::U1 carry{ir.ILessThan(low, a_low, false)};
        high = ir.IAdd(ir.IAdd(a_high, reg_word(1)),
                       IR::U32{ir.Select(carry, ir.Imm32(1U), ir.Imm32(0U))});
    } else if (use_uniform) {
        low = uniform_word(0);
        high = uniform_word(1);
    } else if (have_reg) {
        low = reg_word(0);
        high = reg_word(1);
    } else {
        low = ir.Imm32(0U);
        high = ir.Imm32(0U);
    }
    IR::U64 address{ir.PackUint2x32(ir.CompositeConstruct(low, high))};
    const s32 signed_offset = static_cast<s32>(offset << 8) >> 8;
    if (signed_offset != 0) {
        address = ir.IAdd(address, ir.Imm64(static_cast<s64>(signed_offset)));
    }
    return address;
}

// LDG: global load. 0x381 takes a register address, 0x981 adds an optional uniform register address.
void Visitor::LDG() {
    const bool uniform_form = op == 0x981;
    const u32 dest = Take(16, 8);
    const u32 reg = Take(24, 8);
    bool use_uniform = false;
    u32 uniform = 0;
    bool uniform_pair = false;
    bool reg_pair;
    u32 load_pred_field = 0;
    bool load_pred_negate = false;
    if (uniform_form) {
        uniform = Take(32, 6);
        uniform_pair = Take(72, 1) != 0;
        reg_pair = Take(90, 1) != 0;
        use_uniform = Take(91, 1) != 0;
        // The load returns zero when its predicate (encoded reversed in 64..66, negate in 67) is false.
        load_pred_field = Take(64, 3);
        load_pred_negate = Take(67, 1) != 0;
        Require(Take(68, 4) == 0, "native global load predicate");
    } else {
        reg_pair = Take(72, 1) != 0;
        Require(Take(91, 1) == 0, "native global load flags");
    }
    const u32 offset = Take(40, 24);
    const u32 type = Take(73, 3);
    Ignore(76, 4); // memory ordering
    Require(Take(81, 3) == 7, "native global load residency");
    Ignore(84, 3); // cache eviction priority
    Require(type <= 6, "native global load type");
    const IR::U64 address{GlobalAddress(reg, reg_pair, use_uniform, uniform, uniform_pair, offset)};
    switch (static_cast<MemType>(type)) {
    case MemType::U8:
        X(dest, ir.LoadGlobalU8(address));
        break;
    case MemType::S8:
        X(dest, ir.LoadGlobalS8(address));
        break;
    case MemType::U16:
        X(dest, ir.LoadGlobalU16(address));
        break;
    case MemType::S16:
        X(dest, ir.LoadGlobalS16(address));
        break;
    case MemType::B32:
        X(dest, ir.LoadGlobal32(address));
        break;
    case MemType::B64: {
        Require(dest == 255 || (dest % 2 == 0 && dest + 1 < 255), "native global load destination");
        const IR::Value value{ir.LoadGlobal64(address)};
        X(dest, IR::U32{ir.CompositeExtract(value, 0)});
        if (dest != 255) {
            X(dest + 1, IR::U32{ir.CompositeExtract(value, 1)});
        }
        break;
    }
    case MemType::B128: {
        Require(dest == 255 || (dest % 4 == 0 && dest + 3 < 255), "native global load destination");
        const IR::Value value{ir.LoadGlobal128(address)};
        for (u32 k = 0; dest != 255 && k < 4; ++k) {
            X(dest + k, IR::U32{ir.CompositeExtract(value, k)});
        }
        break;
    }
    }
    if (dest != 255 && (load_pred_field != 0 || load_pred_negate)) {
        const IR::U1 enabled{Pred(7 - load_pred_field, load_pred_negate)};
        const u32 count = type == static_cast<u32>(MemType::B128) ? 4 : type == static_cast<u32>(MemType::B64) ? 2 : 1;
        for (u32 k = 0; k < count; ++k) {
            X(dest + k, IR::U32{ir.Select(enabled, X(dest + k), ir.Imm32(0U))});
        }
    }
}

// STG: global store, same addressing as LDG.
void Visitor::STG() {
    const bool uniform_form = op == 0x986;
    const u32 reg = Take(24, 8);
    const u32 data = Take(32, 8);
    const u32 offset = Take(40, 24);
    bool use_uniform = false;
    u32 uniform = 0;
    bool uniform_pair = false;
    bool reg_pair;
    if (uniform_form) {
        uniform = Take(64, 6);
        uniform_pair = Take(72, 1) != 0;
        reg_pair = Take(90, 1) != 0;
        use_uniform = Take(91, 1) != 0;
    } else {
        reg_pair = Take(72, 1) != 0;
        Require(Take(91, 1) == 0, "native global store flags");
    }
    const u32 type = Take(73, 3);
    Ignore(76, 4); // memory ordering
    Ignore(84, 3); // cache eviction priority
    Require(type <= 6, "native global store type");
    const IR::U64 address{GlobalAddress(reg, reg_pair, use_uniform, uniform, uniform_pair, offset)};
    const auto word = [&](u32 k) -> IR::U32 { return data == 255 ? X(255) : X(data + k); };
    switch (static_cast<MemType>(type)) {
    case MemType::U8:
        ir.WriteGlobalU8(address, X(data));
        break;
    case MemType::S8:
        ir.WriteGlobalS8(address, X(data));
        break;
    case MemType::U16:
        ir.WriteGlobalU16(address, X(data));
        break;
    case MemType::S16:
        ir.WriteGlobalS16(address, X(data));
        break;
    case MemType::B32:
        ir.WriteGlobal32(address, X(data));
        break;
    case MemType::B64:
        Require(data == 255 || (data % 2 == 0 && data + 1 < 255), "native global store data");
        ir.WriteGlobal64(address, ir.CompositeConstruct(word(0), word(1)));
        break;
    case MemType::B128:
        Require(data == 255 || (data % 4 == 0 && data + 3 < 255), "native global store data");
        ir.WriteGlobal128(address, ir.CompositeConstruct(word(0), word(1), word(2), word(3)));
        break;
    }
}

// R2UR: broadcast a register from the first active lane whose guard holds into a uniform register.
// The uniform register is written unconditionally, like the Maxwell sequence of the lowering.
void Visitor::R2UR() {
    const u32 destination = Take(16, 6);
    Ignore(22, 2);
    const u32 source = Take(24, 8);
    Require(Take(81, 3) == 7, "native register to uniform predicate output");
    Require(destination < 63, "native register to uniform destination");
    const IR::U1 guard{guard_select ? *guard_select : ir.Imm1(true)};
    const IR::U32 ballot{ir.SubgroupBallot(guard)};
    const IR::U32 lowest{ir.BitwiseAnd(ballot, ir.INeg(ballot))};
    const IR::U32 lane{ir.FindUMsb(lowest)};
    const IR::U32 value{ir.ShuffleIndex(X(source), lane, ir.Imm32(31U), ir.Imm32(0U))};
    guard_select.reset();
    UR(destination, value);
}

// Shared memory byte offset: an optional register scaled by the stride code plus a 24 bit offset
// that is absolute without a register and signed with one.
IR::U32 Visitor::SharedOffset(u32 reg, u32 stride, u32 offset) {
    if (reg == 255) {
        return ir.Imm32(offset);
    }
    IR::U32 base{X(reg)};
    if (stride != 0) {
        base = ir.ShiftLeftLogical(base, ir.Imm32(stride + 1));
    }
    const s32 relative = static_cast<s32>(offset << 8) >> 8;
    return relative == 0 ? base : IR::U32{ir.IAdd(base, ir.Imm32(static_cast<u32>(relative)))};
}

// LDS: shared memory load.
void Visitor::LDS() {
    const u32 dest = Take(16, 8);
    const u32 reg = Take(24, 8);
    const u32 offset = Take(40, 24);
    const u32 type = Take(73, 3);
    const u32 stride = Take(78, 2);
    Require(Take(91, 1) == 0, "native shared uniform address");
    Require(type <= 6, "native shared load type");
    const IR::U32 address{SharedOffset(reg, stride, offset)};
    const bool is_signed = type == 1 || type == 3;
    switch (static_cast<MemType>(type)) {
    case MemType::U8:
    case MemType::S8:
        X(dest, IR::U32{ir.LoadShared(8, is_signed, address)});
        break;
    case MemType::U16:
    case MemType::S16:
        X(dest, IR::U32{ir.LoadShared(16, is_signed, address)});
        break;
    case MemType::B32:
        X(dest, IR::U32{ir.LoadShared(32, false, address)});
        break;
    case MemType::B64:
    case MemType::B128: {
        const u32 count = type == 5 ? 2 : 4;
        Require(dest == 255 || (dest % count == 0 && dest + count - 1 < 255), "native shared load destination");
        const IR::Value value{ir.LoadShared(count * 32, false, address)};
        for (u32 k = 0; dest != 255 && k < count; ++k) {
            X(dest + k, IR::U32{ir.CompositeExtract(value, k)});
        }
        break;
    }
    }
}

// STS: shared memory store.
void Visitor::STS() {
    const u32 reg = Take(24, 8);
    const u32 data = Take(32, 8);
    const u32 offset = Take(40, 24);
    const u32 type = Take(73, 3);
    const u32 stride = Take(78, 2);
    Require(Take(91, 1) == 0, "native shared uniform address");
    Require(type <= 6, "native shared store type");
    const IR::U32 address{SharedOffset(reg, stride, offset)};
    const auto word = [&](u32 k) -> IR::U32 { return data == 255 ? X(255) : X(data + k); };
    switch (static_cast<MemType>(type)) {
    case MemType::U8:
    case MemType::S8:
        ir.WriteShared(8, address, X(data));
        break;
    case MemType::U16:
    case MemType::S16:
        ir.WriteShared(16, address, X(data));
        break;
    case MemType::B32:
        ir.WriteShared(32, address, X(data));
        break;
    case MemType::B64:
        Require(data == 255 || (data % 2 == 0 && data + 1 < 255), "native shared store data");
        ir.WriteShared(64, address, ir.CompositeConstruct(word(0), word(1)));
        break;
    case MemType::B128:
        Require(data == 255 || (data % 4 == 0 && data + 3 < 255), "native shared store data");
        ir.WriteShared(128, address, ir.CompositeConstruct(word(0), word(1), word(2), word(3)));
        break;
    }
}

// SHFL with an immediate lane and segmentation mask: bits 40..52 hold clamp (low 5) and segment mask
// (bits 8..12), 53..57 the lane, 58..59 the mode (index, up, down, butterfly).
void Visitor::SHFL() {
    const bool register_lane = op == 0x589; // lane in a register (32..39), mask immediate at 40..52
    const u32 dest = Take(16, 8);
    const u32 source = Take(24, 8);
    u32 mask;
    u32 lane = 0;
    u32 lane_register = 0;
    if (register_lane) {
        lane_register = Take(32, 8);
        mask = Take(40, 13);
    } else {
        mask = Take(40, 13);
        lane = Take(53, 5);
    }
    const u32 mode = Take(58, 2);
    const u32 predicate = Take(81, 3);
    const IR::U32 value{X(source)};
    // Quad-wide immediate lanes in fragment shaders map to the quad operations the derivatives use.
    if (!register_lane && env.ShaderStage() == Stage::Fragment && mask == ((28U << 8) | 3U)) {
        if (mode == 0 && lane <= 3) {
            X(dest, ir.QuadBroadcast(value, ir.Imm32(lane)));
            PredOut(predicate, ir.Imm1(true));
            return;
        }
        if (mode == 3 && lane >= 1 && lane <= 3) {
            X(dest, ir.QuadSwap(value, ir.Imm32(lane - 1)));
            PredOut(predicate, ir.Imm1(true));
            return;
        }
    }
    const IR::U32 lane_index{register_lane ? X(lane_register) : IR::U32{ir.Imm32(lane)}};
    const IR::U32 clamp{ir.Imm32(mask & 0x1f)};
    const IR::U32 segment{ir.Imm32((mask >> 8) & 0x1f)};
    IR::U32 result;
    switch (mode) {
    case 0:
        result = ir.ShuffleIndex(value, lane_index, clamp, segment);
        break;
    case 1:
        result = ir.ShuffleUp(value, lane_index, clamp, segment);
        break;
    case 2:
        result = ir.ShuffleDown(value, lane_index, clamp, segment);
        break;
    default:
        result = ir.ShuffleButterfly(value, lane_index, clamp, segment);
        break;
    }
    PredOut(predicate, ir.GetInBoundsFromOp(result));
    X(dest, result);
}

// ATOMS: shared memory atomic on a 32-bit word, returning the previous value. The byte address is the
// address register (scaled by the stride code) plus the immediate offset.
void Visitor::ATOMS() {
    const u32 dest = Take(16, 8);
    const u32 reg = Take(24, 8);
    const u32 data = Take(32, 8);
    const u32 offset = Take(40, 24);
    const u32 type = Take(73, 3);
    const u32 stride = Take(78, 2);
    const u32 atom_op = Take(87, 4);
    Require(type <= 1, "native shared atomic type");
    Require(atom_op <= 8, "native shared atomic operation");
    const IR::U32 address{SharedOffset(reg, stride, offset)};
    const IR::U32 value{X(data)};
    const bool is_signed = type == 1;
    IR::U32 old;
    switch (atom_op) {
    case 0:
        old = ir.SharedAtomicIAdd(address, value);
        break;
    case 1:
        old = ir.SharedAtomicIMin(address, value, is_signed);
        break;
    case 2:
        old = ir.SharedAtomicIMax(address, value, is_signed);
        break;
    case 3:
        old = ir.SharedAtomicInc(address, value);
        break;
    case 4:
        old = ir.SharedAtomicDec(address, value);
        break;
    case 5:
        old = ir.SharedAtomicAnd(address, value);
        break;
    case 6:
        old = ir.SharedAtomicOr(address, value);
        break;
    case 7:
        old = ir.SharedAtomicXor(address, value);
        break;
    default:
        old = IR::U32{ir.SharedAtomicExchange(address, value)};
        break;
    }
    if (dest != 255) {
        X(dest, old);
    }
}

// REDUX: reduction of a register across the active lanes into a uniform register (add, and, or, xor,
// signed or unsigned min and max). Every active lane's value is read by a shuffle.
void Visitor::REDUX() {
    const u32 udst = Take(16, 6);
    const u32 source = Take(24, 8);
    const bool is_signed = Take(73, 1) != 0;
    const u32 operation = Take(78, 3);
    Require(operation <= 5 && udst < 63, "native warp reduction mode");
    const IR::U32 value{X(source)};
    const IR::U32 active{ir.SubgroupBallot(ir.Imm1(true))};
    IR::U32 accumulator;
    const IR::U32 identity{ir.Imm32(operation == 0 ? 0xffffffffU : operation == 4 ? (is_signed ? 0x7fffffffU : 0xffffffffU)
                                                  : operation == 5 ? (is_signed ? 0x80000000U : 0U) : 0U)};
    accumulator = identity;
    for (u32 lane = 0; lane < 32; ++lane) {
        const IR::U1 present{ir.INotEqual(ir.BitwiseAnd(ir.ShiftRightLogical(active, ir.Imm32(lane)), ir.Imm32(1U)),
                                          ir.Imm32(0U))};
        const IR::U32 other{ShuffleLane(value, ir.Imm32(lane))};
        IR::U32 combined;
        switch (operation) {
        case 0:
            combined = ir.BitwiseAnd(accumulator, other);
            break;
        case 1:
            combined = ir.BitwiseOr(accumulator, other);
            break;
        case 2:
            combined = ir.BitwiseXor(accumulator, other);
            break;
        case 3:
            combined = ir.IAdd(accumulator, other);
            break;
        case 4:
            combined = is_signed ? ir.SMin(accumulator, other) : ir.UMin(accumulator, other);
            break;
        default:
            combined = is_signed ? ir.SMax(accumulator, other) : ir.UMax(accumulator, other);
            break;
        }
        accumulator = IR::U32{ir.Select(present, combined, accumulator)};
    }
    UR(udst, accumulator);
}

// BREV: bit reversal of a register.
void Visitor::BREV() {
    const u32 dest = Take(16, uniform_op ? 6 : 8);
    const IR::U32 source{uniform_op ? UR(Take(32, 6)) : X(Take(32, 8))};
    WriteDst(dest, ir.BitReverse(source));
}

// VOTEU: warp vote of a predicate into a uniform register (ballot) and a uniform predicate.
void Visitor::VOTEU() {
    const u32 udst = Take(16, 6);
    const u32 vote = Take(72, 2);
    const u32 predicate_dst = Take(81, 3);
    const u32 pred_index = Take(87, 3);
    const bool pred_negated = Take(90, 1) != 0;
    Require(vote <= 2, "native uniform vote mode");
    const IR::U1 source{Pred(pred_index, pred_negated)};
    if (udst != 63) {
        UR(udst, IR::U32{ir.SubgroupBallot(source)});
    }
    if (predicate_dst != 7) {
        const IR::U1 result{vote == 0 ? ir.VoteAll(source) : vote == 1 ? ir.VoteAny(source) : ir.VoteEqual(source)};
        SetUPred(predicate_dst, result);
    }
}

// VOTE: warp vote of a predicate: the ballot goes to a register and the all/any/equal result to a
// predicate.
void Visitor::VOTE() {
    const u32 dest = Take(16, 8);
    const u32 vote = Take(72, 2);
    const u32 predicate_dst = Take(81, 3);
    const u32 pred_index = Take(87, 3);
    const bool pred_negated = Take(90, 1) != 0;
    Require(vote <= 2, "native warp vote mode");
    const IR::U1 source{Pred(pred_index, pred_negated)};
    if (dest != 255) {
        X(dest, IR::U32{ir.SubgroupBallot(source)});
    }
    if (predicate_dst != 7) {
        const IR::U1 result{vote == 0 ? ir.VoteAll(source) : vote == 1 ? ir.VoteAny(source) : ir.VoteEqual(source)};
        SetPred(predicate_dst, result);
    }
}

// LDGSTS.E.BYPASS.128: copies 16 bytes from global to shared memory. As in the reference lowering the
// copy completes synchronously: the global address is the register pair at 24, the shared address the
// register at 16, the signed global offset the low 12 bits of the second word and the shared offset
// its upper half in units of 16 bytes; the memory descriptor selector is ignored, and the enable
// predicate (87..89 plus the negate bit 70) zero-fills the destination when false.
void Visitor::LDGSTS() {
    const u32 shared_reg = Take(16, 8);
    const u32 global_reg = Take(24, 8);
    const u32 global_immediate = Take(32, 12);
    Require(Take(44, 4) == 0, "native global-to-shared copy mode");
    const s32 shared_offset = static_cast<s32>(static_cast<s16>(Take(48, 16))) * 16;
    Ignore(64, 6); // memory descriptor selector
    const bool negate_enable = Take(70, 1) != 0;
    Require(Take(72, 2) == 0 && Take(74, 3) == 7 && Take(77, 7) == 0 && Take(84, 1) == 1 &&
                Take(85, 5) == 0 && Take(90, 2) == 3,
            "native global-to-shared copy mode");
    const u32 enable_index = Take(87, 3);
    Require(global_reg % 2 == 0 && global_reg + 1 < 255 && shared_reg < 255, "native global-to-shared copy registers");
    const s32 global_offset = static_cast<s32>(global_immediate) - ((global_immediate & 0x800) != 0 ? 0x1000 : 0);
    const IR::U1 enabled{Pred(enable_index, negate_enable)};
    for (u32 component = 0; component < 4; ++component) {
        const IR::U64 address{GlobalAddress(global_reg, true, false, 0, false,
                                            static_cast<u32>(global_offset + static_cast<s32>(component * 4)) & 0xffffffU)};
        const IR::U32 loaded{ir.LoadGlobal32(address)};
        const IR::U32 value{ir.Select(enabled, loaded, ir.Imm32(0U))};
        const IR::U32 shared_address{ir.IAdd(X(shared_reg), ir.Imm32(static_cast<u32>(shared_offset + static_cast<s32>(component * 4))))};
        ir.WriteShared(32, shared_address, value);
    }
    ir.WorkgroupMemoryBarrier();
}

// MEMBAR: workgroup scope (0) or wider scopes map to the workgroup and device barriers.
void Visitor::MEMBAR() {
    const u32 scope = Take(76, 3);
    Require(scope == 0 || scope == 2 || scope == 3 || scope == 5, "native memory barrier scope");
    if (scope == 0) {
        ir.WorkgroupMemoryBarrier();
    } else {
        ir.DeviceMemoryBarrier();
    }
}

// ATOMS.POPC.INC.32 without a returned value: every active lane adds one to the shared word.
void Visitor::ATOMS_POPC() {
    const u32 dest = Take(16, 8);
    const u32 reg = Take(24, 8);
    Require(Take(32, 32) == 0 && Take(64, 6) == 63, "native shared increment mode");
    Require(Take(73, 3) == 0, "native shared increment type");
    const u32 stride = Take(78, 2);
    Require(Take(87, 4) == 10 && Take(91, 1) == 1, "native shared increment operation");
    Require(dest == 255, "native shared increment result");
    ir.SharedAtomicIAdd(SharedOffset(reg, stride, 0), ir.Imm32(1U));
}

// BAR.SYNC 0 over the whole workgroup; bit 80 is the deferred-blocking scheduling qualifier.
void Visitor::BAR() {
    Require(Take(32, 32) == 0 && Take(64, 16) == 0 && Take(81, 15) == 0, "native workgroup barrier mode");
    Ignore(80, 1);
    ir.Barrier();
}

// LDL: local (per-thread) memory load, one 32-bit word at a time.
void Visitor::LDL() {
    const u32 dest = Take(16, 8);
    const u32 reg = Take(24, 8);
    Require(Take(32, 6) == 0 && Take(91, 1) == 0, "native local uniform address");
    const u32 offset_field = Take(40, 24);
    const u32 type = Take(73, 3);
    Ignore(84, 3); // cache eviction priority
    Require(type <= 6, "native local load type");
    const IR::U32 offset{SharedOffset(reg, 0, offset_field)};
    const IR::U32 limit{ir.Imm32(env.LocalMemorySize())};
    const auto load_word = [&](u32 k) -> IR::U32 {
        const IR::U32 byte_offset{k == 0 ? offset : IR::U32{ir.IAdd(offset, ir.Imm32(k * 4))}};
        const IR::U32 word_offset{ir.ShiftRightArithmetic(byte_offset, ir.Imm32(2))};
        const IR::U1 in_bounds{ir.ILessThan(byte_offset, limit, false)};
        return IR::U32{ir.Select(in_bounds, ir.LoadLocal(word_offset), ir.Imm32(0U))};
    };
    switch (static_cast<MemType>(type)) {
    case MemType::U8:
    case MemType::S8: {
        const IR::U32 bit{ir.BitwiseAnd(ir.ShiftLeftLogical(offset, ir.Imm32(3)), ir.Imm32(24))};
        X(dest, ir.BitFieldExtract(load_word(0), bit, ir.Imm32(8), type == 1));
        break;
    }
    case MemType::U16:
    case MemType::S16: {
        const IR::U32 bit{ir.BitwiseAnd(ir.ShiftLeftLogical(offset, ir.Imm32(3)), ir.Imm32(16))};
        X(dest, ir.BitFieldExtract(load_word(0), bit, ir.Imm32(16), type == 3));
        break;
    }
    default: {
        const u32 count = type == 4 ? 1 : type == 5 ? 2 : 4;
        Require(dest == 255 || (dest % count == 0 && dest + count - 1 < 255), "native local load destination");
        for (u32 k = 0; k < count; ++k) {
            const IR::U32 word{load_word(k)};
            if (dest != 255) {
                X(dest + k, word);
            }
        }
        break;
    }
    }
}

// STL: local memory store.
void Visitor::STL() {
    const u32 reg = Take(24, 8);
    const u32 data = Take(32, 8);
    const u32 offset_field = Take(40, 24);
    Require(Take(91, 1) == 0, "native local uniform address");
    const u32 type = Take(73, 3);
    Ignore(84, 3); // cache eviction priority
    Require(type <= 6, "native local store type");
    const IR::U32 offset{SharedOffset(reg, 0, offset_field)};
    const IR::U32 word_offset{ir.ShiftRightArithmetic(offset, ir.Imm32(2))};
    switch (static_cast<MemType>(type)) {
    case MemType::U8:
    case MemType::S8: {
        const IR::U32 bit{ir.BitwiseAnd(ir.ShiftLeftLogical(offset, ir.Imm32(3)), ir.Imm32(24))};
        ir.WriteLocal(word_offset, ir.BitFieldInsert(ir.LoadLocal(word_offset), X(data), bit, ir.Imm32(8)));
        break;
    }
    case MemType::U16:
    case MemType::S16: {
        const IR::U32 bit{ir.BitwiseAnd(ir.ShiftLeftLogical(offset, ir.Imm32(3)), ir.Imm32(16))};
        ir.WriteLocal(word_offset, ir.BitFieldInsert(ir.LoadLocal(word_offset), X(data), bit, ir.Imm32(16)));
        break;
    }
    default: {
        const u32 count = type == 4 ? 1 : type == 5 ? 2 : 4;
        Require(data == 255 || (data % count == 0 && data + count - 1 < 255), "native local store data");
        for (u32 k = 0; k < count; ++k) {
            ir.WriteLocal(k == 0 ? word_offset : IR::U32{ir.IAdd(word_offset, ir.Imm32(k))},
                          data == 255 ? X(255) : X(data + k));
        }
        break;
    }
    }
}

// ATOMG / RED: global atomic on a 32-bit word, returning the previous value (RED discards it).
// 0x3a8 addresses through a register (pair); 0x9a8 adds the uniform-register base; 0x98e is the
// reduction form. The operation is bits 87..90 (87..89 for RED): add, min, max, inc, dec, and, or,
// xor, exchange.
void Visitor::ATOMG() {
    const bool reduction = op == 0x98e;
    const bool uniform_form = op == 0x9a8;
    const u32 dest = reduction ? 255 : Take(16, 8);
    const u32 reg = Take(24, 8);
    const u32 data = Take(32, 8);
    const u32 offset = Take(40, 24);
    bool use_uniform = false;
    u32 uniform = 0;
    bool uniform_pair = false;
    bool reg_pair;
    if (uniform_form) {
        uniform = Take(64, 6);
        uniform_pair = Take(72, 1) != 0;
        reg_pair = Take(70, 1) != 0;
        use_uniform = Take(91, 1) != 0;
    } else {
        reg_pair = Take(72, 1) != 0;
        Require(Take(91, 1) == 0, "native global atomic flags");
    }
    const u32 type = Take(73, 3);
    Ignore(76, 4); // memory ordering
    if (!reduction) {
        Require(Take(81, 3) == 7, "native global atomic predicate output");
    }
    Ignore(84, 3); // cache eviction priority
    const u32 atom_op = reduction ? Take(87, 3) : Take(87, 4);
    Require(type <= 1, "native global atomic type"); // U32 or S32
    Require(atom_op <= 8, "native global atomic operation");
    const bool is_signed = type == 1;
    const IR::U64 address{GlobalAddress(reg, reg_pair, use_uniform, uniform, uniform_pair, offset)};
    const IR::U32 value{X(data)};
    IR::U32 old;
    switch (atom_op) {
    case 0:
        old = IR::U32{ir.GlobalAtomicIAdd(address, value)};
        break;
    case 1:
        old = IR::U32{ir.GlobalAtomicIMin(address, value, is_signed)};
        break;
    case 2:
        old = IR::U32{ir.GlobalAtomicIMax(address, value, is_signed)};
        break;
    case 3:
        old = ir.GlobalAtomicInc(address, value);
        break;
    case 4:
        old = ir.GlobalAtomicDec(address, value);
        break;
    case 5:
        old = IR::U32{ir.GlobalAtomicAnd(address, value)};
        break;
    case 6:
        old = IR::U32{ir.GlobalAtomicOr(address, value)};
        break;
    case 7:
        old = IR::U32{ir.GlobalAtomicXor(address, value)};
        break;
    default:
        old = IR::U32{ir.GlobalAtomicExchange(address, value)};
        break;
    }
    if (dest != 255) {
        X(dest, old);
    }
}

} // namespace Shader::Ampere
