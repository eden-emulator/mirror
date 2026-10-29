// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// Encoding reference: Mesa NAK sm70_encode.rs (MIT), Copyright 2022 Collabora, Ltd.
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstdlib>
#include <string>
#include <span>
#include <set>
#include <optional>
#include <string_view>
#include <vector>
#include "common/common_types.h"
#include "shader_recompiler/frontend/ampere/instruction.h"
#include "shader_recompiler/frontend/ampere/register_pressure.h"

namespace Shader::Ampere {

// EDEN_NATIVE_LOWER_DISABLE=<group>[,<group>...] switches individual lowering fixes back to the
// previous behaviour (rejecting or ignoring the form) so a visual regression can be bisected.
// Groups: audit_fsel, audit_f2i, cube3d_lod, array2d_bias, byte_sel306. Unset leaves behaviour unchanged.
inline bool LowerGroupDisabled(std::string_view group) {
    static const std::string list = [] {
        const char* value = std::getenv("EDEN_NATIVE_LOWER_DISABLE");
        return std::string{value ? value : ""};
    }();
    if (list.empty()) return false;
    std::string_view rest = list;
    while (!rest.empty()) {
        const size_t comma = rest.find(',');
        if (rest.substr(0, comma) == group) return true;
        if (comma == std::string_view::npos) break;
        rest.remove_prefix(comma + 1);
    }
    return false;
}

// EDEN_NATIVE_LOWER_ENABLE=<group>[,<group>...] turns on lowering fixes that are off by default.
// Groups: audit_fsel (FSEL operand neg/abs, suspected of changing FastFusion colours).
inline bool LowerGroupEnabled(std::string_view group) {
    static const std::string list = [] {
        const char* value = std::getenv("EDEN_NATIVE_LOWER_ENABLE");
        return std::string{value ? value : ""};
    }();
    std::string_view rest = list;
    while (!rest.empty()) {
        const size_t comma = rest.find(',');
        if (rest.substr(0, comma) == group) return true;
        if (comma == std::string_view::npos) break;
        rest.remove_prefix(comma + 1);
    }
    return false;
}

struct LoweredProgram {
    std::vector<u64> instructions;
    std::vector<u32> source_instruction_offsets;
    u32 source_size{};
    u32 required_texture_buffer = ~0U;
    u32 error_offset{};
    std::string_view error;

    explicit operator bool() const { return error.empty(); }
};

// Lower only operations whose semantics are representable by the existing
// Maxwell frontend. These are new instructions, not reinterpretations of the
// native bytes. Scheduling/control words are added by the environment adapter.
inline LoweredProgram LowerToMaxwell(std::span<const Instruction> code,
                                     std::span<const u32> verified_traps = {}, bool allow_register_spills = false) {
    LoweredProgram result;
    struct Relocation { u32 output_index; s64 target; bool call = false; u32 output_target = ~0U; };
    std::vector<Relocation> relocations;
    std::vector<u32> barrier_tokens;
    std::vector<bool> barrier_sync_scopes;
    std::vector<u32> cancellation_parent_scopes;
    std::vector<s64> barrier_targets;
    std::vector<u32> validated_barrier_cancellations;
    s64 last_outer_sync_target = -1;
    u32 last_outer_sync_token = ~0U;
    const bool has_barrier_break = std::any_of(code.begin(), code.end(), [](const auto& instruction) {
        return instruction.Bits(0, 12) == 0x942;
    });
    const bool has_indirect_branch = std::any_of(code.begin(), code.end(), [](const auto& instruction) {
        const u32 op = instruction.Bits(0, 12);
        return op == 0x949 || op == 0x958;
    });
    std::vector<u32> call_targets;
    std::vector<u32> call_links;
    bool main_exited = false;
    bool uses_uniform_bank = false;
    bool uses_extended_uniform_bank = false;
    std::array<bool, 16> used_low_uniform{};
    const auto extended_uniform = [&](u32 reg) {
        if (reg < used_low_uniform.size()) used_low_uniform[reg] = true;
        return reg >= 16 && reg < 63;
    };
    for (const auto& instruction : code) {
        const u32 op = instruction.Bits(0, 12);
        const bool uniform_destination = op == 0x291 || op == 0x2be || op == 0x896 || op == 0x883 || op == 0x2a4 || op == 0x8a4 || op == 0x290 || op == 0x890 ||
            op == 0x891 || op == 0x892 || op == 0x899 || op == 0x882 || op == 0xc82 || op == 0x2bd || op == 0x3c4 ||
            op == 0x3c2 || op == 0x886 || op == 0xab9 || op == 0xabb || op == 0x9c3 || op == 0x287 || op == 0x887 ||
            op == 0x292 || op == 0x299;
        if (uniform_destination) uses_extended_uniform_bank |= extended_uniform(instruction.Bits(16, 8));
        switch (op) {
        case 0x291: case 0x2a4: case 0x290: case 0x292: case 0x299: // [merge-ulop3]
            uses_extended_uniform_bank |= extended_uniform(instruction.Bits(32, 8));
            [[fallthrough]];
        case 0x8a4: case 0x890: case 0x891: case 0x892: case 0x899:
            uses_extended_uniform_bank |= extended_uniform(instruction.Bits(24, 8)) || extended_uniform(instruction.Bits(64, 8));
            break;
        case 0x2be:
            uses_extended_uniform_bank |= extended_uniform(instruction.Bits(32, 8));
            break;
        case 0x896: case 0x526:
            uses_extended_uniform_bank |= extended_uniform(instruction.Bits(24, 8));
            break;
        case 0x984:
            if (instruction.Bits(91, 1)) uses_extended_uniform_bank |= extended_uniform(instruction.Bits(32, 8));
            break;
        case 0x988:
            uses_extended_uniform_bank |= extended_uniform(instruction.Bits(64, 8));
            break;
        case 0x98e:
            if (instruction.Bits(91, 1)) {
                uses_extended_uniform_bank |= extended_uniform(instruction.Bits(64, 8));
                uses_extended_uniform_bank |= extended_uniform(instruction.Bits(64, 8) + 1);
            }
            break;
        case 0x28c: case 0x287:
            uses_extended_uniform_bank |= extended_uniform(instruction.Bits(32, 8));
            [[fallthrough]];
        case 0x88c: case 0x887: case 0x958: case 0xabb:
            uses_extended_uniform_bank |= extended_uniform(instruction.Bits(24, 8));
            break;
        case 0xc0b: case 0xc0c: case 0xc10: case 0xc11: case 0xc12: case 0xc17: case 0xc20: case 0xc24:
        case 0xc45: case 0xd08: case 0xc82: case 0xc02: case 0xd00: case 0xd09: case 0xe21: case 0xe23:
        case 0xe24: case 0xc23: case 0x2bd:
            uses_extended_uniform_bank |= extended_uniform(instruction.Bits(32, 8));
            break;
        case 0xf60: case 0xf63: case 0xf66: // [merge-uniform-tex]
            uses_extended_uniform_bank |= extended_uniform(instruction.Bits(40, 6));
            break;
        case 0x981:
            uses_extended_uniform_bank |= extended_uniform(instruction.Bits(32, 6));
            break;
        case 0x986: case 0x9a8: case 0xfae:
            uses_extended_uniform_bank |= extended_uniform(instruction.Bits(64, 6));
            break;
        case 0x83b:
            if (instruction.Bits(91, 1)) uses_extended_uniform_bank |= extended_uniform(instruction.Bits(32, 8));
            break;
        default: break;
        }
        uses_uniform_bank |= op == 0xf60 || op == 0xf63 || op == 0xf66 || op == 0xd08 || (op == 0x83b && instruction.Bits(91, 1)) || op == 0x2be || op == 0x526 || op == 0x988 || (op == 0x984 && instruction.Bits(91, 1)) || op == 0xc11 || op == 0x883 || (op == 0x98e && instruction.Bits(91, 1)) || op == 0x291 || op == 0x896 || op == 0x892 || op == 0x89c || op == 0x3c4 || op == 0x2bd || op == 0xc10 || op == 0xc0b || op == 0xc0c || op == 0x986 || op == 0x981 || op == 0x9a8 || op == 0xc12 || op == 0xc17 || op == 0xe24 || op == 0xc24 || op == 0xd00 || op == 0xd09 || op == 0x9c3 || op == 0xc45 || op == 0x2a4 || op == 0x8a4 || op == 0x891 || op == 0x890 || op == 0x290 || op == 0x28c || op == 0x88c || op == 0x287 || op == 0x887 || op == 0x958 || op == 0xab9 || op == 0xabb || op == 0x899 || op == 0xc20 || op == 0xe21 || op == 0xc23 || op == 0xe23 || op == 0xc02 || op == 0x292 || op == 0x299 ||
                             op == 0x886 || op == 0x3c2 || op == 0xc82 || op == 0x882 || (op == 0x81c && instruction.Bits(67, 1));
    }
    const bool uses_async_copy = std::any_of(code.begin(), code.end(), [](const auto& instruction) {
        return instruction.Bits(0, 12) == 0xfae;
    });
    const bool uses_matrix = std::any_of(code.begin(), code.end(), [](const auto& instruction) {
        return instruction.Bits(0, 12) == 0x83b || instruction.Bits(0, 12) == 0x23c || instruction.Bits(0, 12) == 0x23a;
    });
    const bool uses_signed_wide = std::any_of(code.begin(), code.end(), [](const auto& instruction) {
        const u32 op = instruction.Bits(0, 12);
        return (op == 0x225 || op == 0x227 || op == 0x625 || op == 0x825) && instruction.Bits(73, 1);
    });
    const u32 first_uniform = static_cast<u32>(std::find(used_low_uniform.begin(), used_low_uniform.end(), true) - used_low_uniform.begin());
    const u32 register_limit = uses_uniform_bank ? (uses_extended_uniform_bank ? 168U : std::min(238U, 216U + first_uniform)) :
        uses_async_copy || uses_matrix || uses_signed_wide ? 240U : 250U;
    const u32 reserved_register = allow_register_spills ? register_limit & ~3U : register_limit;
    const auto address_of = [](u32 index) { return 0x50U + index * 8 + ((index + 1) / 3) * 8; };
    const auto fail = [&](u32 index, std::string_view reason) {
        result.instructions.clear();
        result.source_instruction_offsets.clear();
        result.error_offset = index * sizeof(Instruction);
        result.error = reason;
        return result;
    };
    const auto finish = [&](u32 index) {
        for (const auto& relocation : relocations) {
            if (relocation.target > index) return fail(index, "native branch or call leaves captured program");
            const u32 target = relocation.output_target != ~0U ? relocation.output_target :
                result.source_instruction_offsets[static_cast<size_t>(relocation.target)];
            const s64 relative = s64{address_of(target)} - address_of(relocation.output_index) - 8;
            if (relative < -0x800000 || relative > 0x7fffff) return fail(index, "lowered branch distance");
            result.instructions[relocation.output_index] |= (static_cast<u64>(relative) & 0xffffff) << 20;
        }
        result.source_size = (index + 1) * sizeof(Instruction);
        return result;
    };
    if (code.empty() || code.size() > 65536) return fail(0, "invalid native program size");
    const auto branches_to = [&](u32 at, u32 pred, s64 destination) {
        if (at >= code.size()) return false;
        const auto& branch = code[at];
        return branch.words[0] == ((pred << 12) | 0x947) && branch.words[2] == 0x03800000 &&
               s64{at + 1} * 16 + static_cast<s32>(branch.words[1]) == destination;
    };
    if (has_barrier_break) {
        std::vector<u32> scopes;
        for (u32 at = 0; at < code.size(); ++at) {
            const auto& op = code[at];
            if (op.Bits(0, 12) == 0x945) scopes.push_back(at);
            else if (op.Bits(0, 12) == 0x941 && !scopes.empty()) scopes.pop_back();
            else if (op.Bits(0, 12) == 0x942 && scopes.size() >= 2 && at + 1 < code.size()) {
                const auto& inner = code[scopes.back()];
                const u32 parent_index = scopes[scopes.size() - 2];
                const auto& parent = code[parent_index];
                const s64 join = s64{parent_index} * 16 + static_cast<s32>(parent.words[1]);
                const s64 inner_join = s64{scopes.back()} * 16 + static_cast<s32>(inner.words[1]);
                const u32 pred = op.Bits(12, 4);
                const bool split_exit = (pred & 7) != 7 &&
                    branches_to(at + 1, pred ^ 8U, inner_join) && branches_to(at + 2, 7, join);
                if (op.Bits(16, 4) == inner.Bits(16, 4) &&
                    (branches_to(at + 1, pred, join) || split_exit))
                    cancellation_parent_scopes.push_back(parent_index);
            }
        }
    }
    const auto writes_link = [](const Instruction& instruction, u32 link) {
        const u32 op = instruction.Bits(0, 12);
        if (op == 0x20c || op == 0x80c || op == 0xa0c || op == 0xc0c ||
            op == 0x20b || op == 0x80b || op == 0xa0b || op == 0x918 || op == 0x947 || op == 0x950) return false;
        const u32 destination = instruction.Bits(16, 8);
        const u32 count = op == 0x83b ? 4U : op == 0x23c || op == 0x225 || op == 0x625 || op == 0x311 ? 2U : 1U;
        return destination != 255 && link >= destination && link - destination < count;
    };
    // True when control can enter (first, last] from outside it: a branch, call or reconvergence
    // target there, or a computed branch anywhere.
    const auto entered_from_outside = [&](u32 first, u32 last) {
        for (u32 at = 0; at < code.size(); ++at) {
            const u32 op = code[at].Bits(0, 12);
            if (op == 0x949 || op == 0x958) return true;
            if (op != 0x947 && op != 0x945 && op != 0x944) continue;
            if (at > first && at <= last) continue;
            const auto& branch = code[at];
            const u64 raw = branch.words[1] | (u64{branch.words[2] & 0x3ffffU} << 32);
            s64 relative = op == 0x945 ? s64{static_cast<s32>(branch.words[1])} : (static_cast<s64>(raw << 14) >> 14);
            if (op == 0x947 && (branch.words[2] & ~0x3ffffU) == 0x0b800000U) relative &= ~s64{3};
            const s64 target = s64{at} * 16 + 16 + relative;
            if (target > s64{first} * 16 && target <= s64{last} * 16) return true;
        }
        return false;
    };
    for (u32 index = 0; index < code.size(); ++index) {
        auto insn = code[index];
        result.source_instruction_offsets.push_back(static_cast<u32>(result.instructions.size()));
        u32 opcode = insn.Bits(0, 12);
        const u32 original_opcode = opcode;
        std::optional<u32> active_link;
        u32 active_entry = 0;
        if (main_exited) {
            for (size_t call = 0; call < call_targets.size(); ++call) {
                if (call_targets[call] <= index && call_targets[call] >= active_entry) {
                    active_entry = call_targets[call];
                    active_link = call_links[call];
                }
            }
            if (active_link && writes_link(insn, *active_link))
                return fail(index, "native callee overwrites link register");
        }
        // Scratch values and emulated uniform state occupy reserved high GPRs.
        // Reject native accesses that could alias that state.
        RegisterPressure pressure{insn};
        bool requires_shadow = false;
        for (u32 bit : {16U, 24U, 32U, 64U}) {
            if (bit == 32 && insn.Bits(9, 3) != 1 && opcode != 0xb60 && opcode != 0xb66 && opcode != 0x986) continue;
            const u32 reg = insn.Bits(bit, 8);
            if (reg >= reserved_register && reg < 255) requires_shadow = true;
        }
        if (requires_shadow) {
            if (!allow_register_spills) return fail(index, "native program uses lowering scratch registers");
            pressure = PrepareRegisterPressure(insn, reserved_register);
            if (!pressure.valid) return fail(index, "native register shadowing mode");
            insn = pressure.instruction;
            result.instructions.insert(result.instructions.end(), pressure.prefix.begin(), pressure.prefix.end());
        }
        const auto uniform_register = [&](u32 reg) -> u32 {
            if (reg < 16) return 216 + reg;
            if (reg < 63) return 216 - (reg / 16) * 16 + reg % 16;
            if (reg == 63) return 255;
            return ~0U;
        };
        if (opcode == 0x292 && (insn.Bits(12, 4) != 7 || insn.Bits(40, 24) || insn.Bits(80, 1) ||
                                insn.Bits(81, 3) != 7 || insn.Bits(87, 4) != 15 || !insn.Bits(91, 1)))
            return fail(index, "native uniform logic mode"); // [merge-ulop3]
        if (opcode == 0x291 || opcode == 0x2a4 || opcode == 0x8a4 || opcode == 0x891 || opcode == 0x892 || opcode == 0x890 || opcode == 0x290 || opcode == 0x292) {
            const u32 dst = uniform_register(insn.Bits(16, 8));
            const u32 a = uniform_register(insn.Bits(24, 8));
            const u32 c = uniform_register(insn.Bits(64, 8));
            if (dst == ~0U || a == ~0U || c == ~0U) return fail(index, "native uniform arithmetic register");
            insn.words[0] = (insn.words[0] & 0xffff) | (dst << 16) | (a << 24);
            insn.words[2] = (insn.words[2] & ~0x080000ffU) | c;
            if (opcode == 0x291 || opcode == 0x2a4 || opcode == 0x290 || opcode == 0x292) {
                const u32 b = uniform_register(insn.Bits(32, 8));
                if (b == ~0U) return fail(index, "native uniform arithmetic second register");
                insn.words[1] = (insn.words[1] & ~0xffU) | b;
            }
            opcode = opcode == 0x291 ? 0x211 : opcode == 0x2a4 ? 0x224 : opcode == 0x8a4 ? 0x824 : opcode == 0x891 ? 0x811 : opcode == 0x892 ? 0x812 : opcode == 0x890 ? 0x810 : opcode == 0x292 ? 0x212 : 0x210;
        }
        const bool uniform_predicate_bank = original_opcode == 0x290 || original_opcode == 0x890 || original_opcode == 0x892;
        if (uniform_predicate_bank) {
            const auto map = [](u32 code) { return ((code & 7) == 7 ? 7U : 6U - (code & 7)) | (code & 8); };
            const u32 execution = map(insn.Bits(12, 4));
            insn.words[0] = (insn.words[0] & ~0xf000U) | (execution << 12);
            const u32 first = map(insn.Bits(81, 3));
            const u32 carry_a = map(insn.Bits(87, 4));
            if (original_opcode == 0x892) {
                insn.words[2] = (insn.words[2] & ~((7U << 17) | (15U << 23))) |
                    (first << 17) | (carry_a << 23);
            } else {
                const u32 second = map(insn.Bits(84, 3)), carry_b = map(insn.Bits(77, 4));
                insn.words[2] = (insn.words[2] & ~((7U << 17) | (7U << 20) | (15U << 23) | (15U << 13))) |
                    (first << 17) | (second << 20) | (carry_a << 23) | (carry_b << 13);
            }
        }
        const bool uniform_predication = !uniform_predicate_bank && (opcode == 0x899 || opcode == 0xabb || opcode == 0xab9 || opcode == 0xc82 || opcode == 0x882 ||
            original_opcode == 0x291 || original_opcode == 0x2a4 || original_opcode == 0x8a4 || original_opcode == 0x891 || original_opcode == 0x890 || original_opcode == 0x290);
        const u32 native_predicate = insn.Bits(12, 4);
        const bool guarded_uniform = uniform_predication && native_predicate != 7;
        if (guarded_uniform && (native_predicate & 7) == 7)
            return fail(index, "native inverted uniform true predicate");
        u64 predicate = u64{guarded_uniform ? ((native_predicate & 8) | 6) : native_predicate} << 16;
        const u64 dest = insn.Bits(16, 8);
        const auto base = [&](u64 opcode_bits) { return opcode_bits | predicate | dest; };
        const auto cbuf = [&]() -> u64 {
            return (u64{insn.Bits(38, 16) / 4} << 20) | (u64{insn.Bits(54, 5)} << 34);
        };
        const auto valid_cbuf = [&] {
            return !insn.Bits(91, 1) &&
                   (insn.Bits(38, 16) % 4) == 0;
        };
        std::string_view operand_error;
        const auto emit = [&](u64 word) { result.instructions.push_back(word); };
        const auto save_predicate = [&] {
            emit(0x38e8000000070000ULL | 253 | (u64{255} << 8) | (u64{0x40} << 20));
        };
        const auto restore_predicate = [&] {
            emit(0x38f0000000070000ULL | (u64{253} << 8) | (u64{0x40} << 20));
        };
        if (uniform_predicate_bank) {
            emit(0x38e8000000070000ULL | 238 | (u64{255} << 8) | (u64{0x7f} << 20));
            emit(0x38f0000000070000ULL | (u64{239} << 8) | (u64{0x7f} << 20));
        }
        if (guarded_uniform) {
            save_predicate();
            emit(0x3828000000070000ULL | 250 | (u64{239} << 8) |
                 (u64{6 - (native_predicate & 7)} << 20));
            emit(0x3848000000070000ULL | 250 | (u64{250} << 8) | (u64{6} << 20));
            emit(0x38f0000000070000ULL | (u64{250} << 8) | (u64{0x40} << 20));
        }
        const auto mov_imm = [&](u32 reg, u32 value) {
            emit(0x0100000000000000ULL | predicate | reg | (u64{15} << 12) | (u64{value} << 20));
        };
        const auto transpose_matrix = [&](u32 target, u32 source) {
            // Each lane owns two adjacent b16 elements of an 8x8 row-major matrix.
            emit(0xf0c8000000000000ULL | predicate | 244);
            emit(0x0400000000000000ULL | predicate | 245 | (u64{244} << 8) | (u64{3} << 20));
            emit(0x3848000000000000ULL | predicate | 245 | (u64{245} << 8) | (u64{3} << 20));
            emit(0x3828000000000000ULL | predicate | 246 | (u64{244} << 8) | (u64{3} << 20));
            emit(0x5c10000000000000ULL | predicate | 245 | (u64{245} << 8) | (u64{246} << 20));
            emit(0x0400000000000000ULL | predicate | 246 | (u64{244} << 8) | (u64{4} << 20));
            emit(0x3848000000000000ULL | predicate | 246 | (u64{246} << 8) | (u64{2} << 20));
            for (u32 half = 0; half < 2; ++half) {
                emit(0xef10000000000000ULL | predicate | (250 + half) | (u64{source} << 8) |
                     (u64{245} << 20) | (u64{1} << 29) | (u64{0x1f} << 34) | (u64{7} << 48));
                emit(0x5c28000000000000ULL | predicate | (250 + half) |
                     (u64{250 + half} << 8) | (u64{246} << 20));
                emit(0x0400000000000000ULL | predicate | (250 + half) |
                     (u64{250 + half} << 8) | (u64{0xffff} << 20));
                if (!half) {
                    mov_imm(247, 4);
                    emit(0x5c10000000000000ULL | predicate | 245 | (u64{245} << 8) | (u64{247} << 20));
                }
            }
            emit(0x3848000000000000ULL | predicate | 251 | (u64{251} << 8) | (u64{16} << 20));
            emit(0x5c40000000000000ULL | predicate | target | (u64{250} << 8) |
                 (u64{251} << 20) | (u64{1} << 41) | (u64{7} << 48));
        };
        const auto materialize = [&](u32 kind, u32 reg, u32 scratch, bool absolute, bool negate) -> u32 {
            if (kind == 3) {
                reg = uniform_register(reg);
                if (reg == ~0U) { operand_error = "native uniform source register"; return 255; }
            } else if (kind == 1) {
                mov_imm(scratch, insn.words[1]);
                reg = scratch;
            } else if (kind == 2) {
                if (!valid_cbuf()) { operand_error = "native constant buffer address"; return 255; }
                emit(0x4c98000000000000ULL | predicate | scratch | (u64{15} << 39) | cbuf());
                reg = scratch;
            }
            if (absolute || negate) {
                if (reg != scratch) emit(0x5c98000000000000ULL | predicate | scratch | (u64{reg} << 20) | (u64{15} << 39));
                if (absolute) emit(0x0400000000000000ULL | predicate | scratch | (u64{scratch} << 8) | (u64{0x7fffffff} << 20));
                if (negate) emit(0x0400000000000000ULL | predicate | scratch | (u64{scratch} << 8) | (u64{0x80000000} << 20) | (u64{2} << 53));
                reg = scratch;
            }
            return reg;
        };
        const auto operand = [&](u32 which, bool floating = true) -> u32 {
            const u32 form = insn.Bits(9, 3);
            u32 reg{}, kind{}, abs{}, neg{};
            if (which == 0) {
                reg = insn.Bits(24, 8); abs = insn.Bits(73, 1); neg = insn.Bits(72, 1);
            } else {
                if (form != 1 && form != 2 && form != 3 && form != 4 && form != 5 && form != 6 && form != 7) {
                    operand_error = "native ALU source form"; return 255;
                }
                const bool wide = which == 1 ? (form == 4 || form == 5 || form == 6) : (form == 2 || form == 3 || form == 7);
                if (wide) {
                    kind = form == 6 || form == 7 ? 3 : (form == 4 || form == 2) ? 1 : 2;
                    if (kind == 3) reg = insn.Bits(32, 8);
                    if (kind == 2 || kind == 3) { abs = insn.Bits(62, 1); neg = insn.Bits(63, 1); }
                } else {
                    const bool upper = which == 1 ? (form == 2 || form == 3 || form == 7) : true;
                    reg = insn.Bits(upper ? 64 : 32, 8);
                    abs = insn.Bits(upper ? 74 : 62, 1); neg = insn.Bits(upper ? 75 : 63, 1);
                }
            }
            return materialize(kind, reg, 250 + which, floating && abs, floating && neg);
        };
        struct HalfSource { u32 reg, swizzle; bool absolute, negate; };
        const auto half_source = [&](u32 which) -> HalfSource {
            if (which == 0) return {insn.Bits(24, 8), insn.Bits(74, 2),
                                    insn.Bits(73, 1) != 0, insn.Bits(72, 1) != 0};
            const u32 form = insn.Bits(9, 3);
            const bool wide = which == 1 ? (form == 4 || form == 5) : (form == 2 || form == 3);
            if (wide) {
                const bool immediate = form == 2 || form == 4;
                const u32 reg = materialize(immediate ? 1 : 2, 255, 250 + which, false, false);
                return {reg, immediate ? 0U : insn.Bits(60, 2),
                        !immediate && insn.Bits(62, 1), !immediate && insn.Bits(63, 1)};
            }
            const bool upper = which == 2 || form == 2 || form == 3;
            return {insn.Bits(upper ? 64 : 32, 8), insn.Bits(upper ? 81 : 60, 2),
                    insn.Bits(upper ? 83 : 62, 1) != 0, insn.Bits(upper ? 84 : 63, 1) != 0};
        };
        const auto extract_half = [&](HalfSource source, u32 lane, u32 target) {
            const bool high = source.swizzle == 3 || (source.swizzle == 0 && lane == 1);
            emit(0x5ca8000000000000ULL | predicate | target | (u64{2} << 8) |
                 (u64{source.swizzle == 1 ? 2U : 1U} << 10) | (u64{source.reg} << 20) |
                 (u64{high} << 41) | (u64{source.negate} << 45) | (u64{source.absolute} << 49));
        };
        const auto xmad = [&](u32 target, u32 a, u32 b, u32 c, bool a_high,
                              bool b_high, u32 mode, bool immediate) {
            emit((immediate ? 0x3600000000000000ULL : 0x5b00000000000000ULL) |
                 predicate | target | (u64{a} << 8) |
                 (u64{immediate ? ((b >> (b_high ? 16 : 0)) & 0xffff) : b} << 20) |
                 (u64{!immediate && b_high} << 35) | (u64{c} << 39) |
                 (u64{mode} << 50) | (u64{a_high} << 53));
        };
        const auto multiply_low = [&](u32 a, u32 b, u32 c, bool immediate, u32 target = ~0U) {
            xmad(250, a, b, c, false, false, 0, immediate);
            xmad(251, a, b, 255, true, false, 0, immediate);
            xmad(251, a, b, 251, false, true, 0, immediate);
            emit(0x5c18000000000000ULL | predicate | (target == ~0U ? dest : u64{target}) | (u64{251} << 8) |
                 (u64{250} << 20) | (u64{16} << 39));
        };
        const auto multiply_high = [&](u32 a, u32 b, bool immediate, u32 target) {
            xmad(250, a, b, 255, false, false, 0, immediate);
            xmad(251, a, b, 250, true, false, 2, immediate);
            xmad(250, a, b, 251, false, true, 1, immediate);
            xmad(252, a, b, 251, true, true, 2, immediate);
            emit(0x3828000000000000ULL | predicate | 250 | (u64{250} << 8) | (u64{16} << 20));
            emit(0x5c10000000000000ULL | predicate | target | (u64{250} << 8) | (u64{252} << 20));
        };
        switch (opcode) {
        case 0x304: { // F2F.F16.F32, scalar round-to-nearest conversion.
            if (insn.words[2] != 0x00200800 || insn.Bits(24, 8) ||
                (insn.words[1] & ~0xc00000ffU))
                return fail(index, "native scalar float conversion mode");
            emit(base(0x5ca8000000000000ULL) | (u64{1} << 8) | (u64{2} << 10) |
                 (u64{insn.Bits(32, 8)} << 20) | (u64{insn.Bits(62, 1)} << 49) |
                 (u64{insn.Bits(63, 1)} << 45));
            break;
        }
        case 0x23e: case 0xa3e: { // F2FP.PACK_AB / MERGE_C, round-to-nearest
            if (insn.Bits(62, 2) || insn.Bits(73, 5) || insn.Bits(79, 2) ||
                insn.Bits(81, 10) || insn.Bits(91, 1)) {
                return fail(index, "native float packing modifiers");
            }
            const bool merge = insn.Bits(78, 1);
            if (insn.Bits(72, 1)) return fail(index, "native float packing selector");
            const u32 a = merge ? operand(2, false) : operand(0, false);
            const u32 b = operand(1, false);
            // Canonical F2F writes the converted half in its low 16 bits.
            if (merge) {
                emit(0x5c98000000000000ULL | predicate | 250 | (u64{a} << 20) | (u64{15} << 39));
            } else {
                emit(0x5ca8000000000000ULL | predicate | 250 | (u64{1} << 8) |
                     (u64{2} << 10) | (u64{a} << 20));
            }
            emit(0x5ca8000000000000ULL | predicate | 251 | (u64{1} << 8) |
                 (u64{2} << 10) | (u64{b} << 20));
            emit(base(0x5c18000000000000ULL) | (u64{250} << 8) | (u64{251} << 20) | (u64{16} << 39));
            break;
        }
        case 0x232: case 0x832: case 0xa32: { // HMUL2
            if (insn.Bits(78, 2) || insn.Bits(81, 10) || insn.Bits(91, 1))
                return fail(index, "native half multiply mode");
            const auto a = half_source(0), b = half_source(1);
            const u32 precision = insn.Bits(76, 1) ? 2 : insn.Bits(80, 1);
            emit(base(0x5d08000000000000ULL) | (u64{a.reg} << 8) | (u64{b.reg} << 20) |
                 (u64{b.swizzle} << 28) | (u64{b.absolute} << 30) | (u64{a.negate != b.negate} << 31) |
                 (u64{insn.Bits(77, 1)} << 32) | (u64{precision} << 39) |
                 (u64{a.absolute} << 44) | (u64{a.swizzle} << 47));
            break;
        }
        case 0x230: case 0x430: case 0x630: { // HADD2
            if (insn.Bits(76, 1) || insn.Bits(79, 1) || insn.Bits(81, 10) || insn.Bits(91, 1))
                return fail(index, "native half add mode");
            const auto a = half_source(0);
            const auto b = half_source(opcode == 0x230 ? 1 : 2);
            if (insn.Bits(78, 1)) {
                if (a.reg != 255 || !a.negate || a.absolute || a.swizzle != 0 ||
                    b.swizzle < 2 || insn.Bits(77, 1) || insn.Bits(80, 1))
                    return fail(index, "native float-output half add mode");
                extract_half(b, 0, static_cast<u32>(dest));
            } else {
                emit(base(0x5d10000000000000ULL) | (u64{a.reg} << 8) | (u64{b.reg} << 20) |
                     (u64{b.swizzle} << 28) | (u64{b.absolute} << 30) | (u64{b.negate} << 31) |
                     (u64{insn.Bits(77, 1)} << 32) | (u64{insn.Bits(80, 1)} << 39) |
                     (u64{a.negate} << 43) | (u64{a.absolute} << 44) | (u64{a.swizzle} << 47));
            }
            break;
        }
        case 0x231: case 0x431: case 0x631: case 0x831: case 0xa31: { // HFMA2
            const bool relu = insn.Bits(79, 1) != 0;
            if (insn.Bits(78, 1) || insn.Bits(85, 1) || insn.Bits(91, 1) ||
                (relu ? insn.Bits(77, 1) != 0 : insn.Bits(87, 4) != 0))
                return fail(index, "native half fused multiply-add mode");
            if (relu && native_predicate != 7) {
                // Guard the whole expansion before its temporary P6 updates.
                relocations.push_back({static_cast<u32>(result.instructions.size()), index + 1});
                emit(0xe24000000000000fULL | (u64{native_predicate ^ 8U} << 16));
                predicate = u64{7} << 16;
            }
            auto a = half_source(0);
            auto b = half_source(1);
            const auto c = half_source(2);
            if (insn.Bits(86, 1)) { // H0_NH1: negate only the upper packed component.
                if (b.swizzle != 0 || b.absolute || b.reg >= 250)
                    return fail(index, "native half mixed-sign source mode");
                emit(0x0400000000000000ULL | predicate | 251 | (u64{b.reg} << 8) |
                     (u64{0x80000000} << 20) | (u64{2} << 53));
                b.reg = 251;
            }
            if (b.absolute || c.absolute || (a.absolute && a.reg >= 248))
                return fail(index, "native half fused multiply-add absolute source");
            if (a.absolute) {
                emit(0x0400000000000000ULL | predicate | 250 | (u64{a.reg} << 8) |
                     (u64{a.swizzle == 1 ? 0x7fffffffU : 0x7fff7fffU} << 20));
                a.reg = 250;
                a.absolute = false;
            }
            const u32 precision = insn.Bits(76, 1) ? 2 : insn.Bits(80, 1);
            emit(0x5d00000000000000ULL | predicate | (relu ? 254U : dest) | (u64{a.reg} << 8) | (u64{b.reg} << 20) |
                 (u64{b.swizzle} << 28) | (u64{c.negate} << 30) | (u64{a.negate != b.negate} << 31) |
                 (u64{insn.Bits(77, 1)} << 32) | (u64{c.swizzle} << 35) |
                 (u64{precision} << 37) | (u64{c.reg} << 39) | (u64{a.swizzle} << 47));
            if (relu) {
                // Clamp the rounded binary16 lanes, preserving positive infinity
                // and canonicalizing either sign of NaN without saturating to one.
                save_predicate();
                for (u32 lane = 0; lane < 2; ++lane) {
                    emit(0x3828000000070000ULL | 251 | (u64{254} << 8) | (u64{lane * 16} << 20));
                    emit(0x0400000000070000ULL | 251 | (u64{251} << 8) | (u64{0xffff} << 20));
                    emit(0x0400000000070000ULL | 252 | (u64{251} << 8) | (u64{0x7fff} << 20));
                    mov_imm(250, 0x7c00);
                    emit(0x5b60000000070000ULL | 7 | (u64{6} << 3) | (u64{250} << 8) |
                         (u64{252} << 20) | (u64{7} << 39) | (u64{1} << 49));
                    emit(0x3828000000070000ULL | 252 | (u64{251} << 8) | (u64{15} << 20));
                    mov_imm(250, 1);
                    emit(0x5c10000000070000ULL | 252 | (u64{252} << 8) | (u64{250} << 20) | (u64{1} << 48));
                    emit(0x5c40000000070000ULL | 251 | (u64{251} << 8) | (u64{252} << 20) | (u64{7} << 48));
                    mov_imm(250, 0x7fff);
                    emit(0x5ca0000000070000ULL | 251 | (u64{250} << 8) | (u64{251} << 20) | (u64{6} << 39));
                    if (lane == 0) {
                        emit(base(0x5c98000000000000ULL) | (u64{251} << 20) | (u64{15} << 39));
                    } else {
                        emit(0x3848000000070000ULL | 251 | (u64{251} << 8) | (u64{16} << 20));
                        emit(base(0x5c40000000000000ULL) | (dest << 8) | (u64{251} << 20) |
                             (u64{1} << 41) | (u64{7} << 48));
                    }
                }
                restore_predicate();
                if (insn.Bits(87, 4) != 7)
                    emit(base(0x5ca0000000000000ULL) | (dest << 8) | (u64{254} << 20) |
                         (u64{insn.Bits(87, 4)} << 39));
            }
            break;
        }
        case 0x434: { // HSETP2.GEU.AND, packed immediate and independent half predicates.
            constexpr u32 variable = (0x3fU << 17) | (3U << 10);
            if (insn.Bits(16, 8) || (insn.words[2] & ~variable) != 0x0380e000U ||
                (insn.Bits(74, 2) != 0 && insn.Bits(74, 2) != 2))
                return fail(index, "native packed half predicate mode");
            const auto a = half_source(0), b = half_source(2);
            emit(0x5d20000000000000ULL | predicate | (u64{insn.Bits(81, 3)} << 3) |
                 insn.Bits(84, 3) | (u64{a.reg} << 8) | (u64{b.reg} << 20) |
                 (u64{14} << 35) | (u64{7} << 39) | (u64{a.swizzle} << 47));
            break;
        }
        case 0x233: case 0x433: case 0x633: { // HSET2, packed half comparisons and predicate combination.
            const u32 combine = insn.Bits(69, 2);
            if (insn.Bits(64, 5) || combine > 2 || insn.Bits(81, 6) || insn.Bits(91, 1) ||
                (opcode == 0x233 && (insn.words[1] & 0x0fffff00U)))
                return fail(index, "native packed half comparison mode");
            const auto a = half_source(0), b = half_source(opcode == 0x233 ? 1 : 2);
            emit(base(0x5d18000000000000ULL) | (u64{a.reg} << 8) | (u64{b.reg} << 20) |
                 (u64{b.swizzle} << 28) | (u64{b.absolute} << 30) | (u64{b.negate} << 31) |
                 (u64{insn.Bits(76, 4)} << 35) | (u64{insn.Bits(87, 4)} << 39) |
                 (u64{a.negate} << 43) | (u64{a.absolute} << 44) | (u64{combine} << 45) |
                 (u64{a.swizzle} << 47) | (u64{insn.Bits(71, 1)} << 49) | (u64{insn.Bits(80, 1)} << 50));
            break;
        }
        case 0x240: case 0x840: { // HMNMX2, exact half promotion for min/max
            if (insn.Bits(76, 11) || insn.Bits(91, 1))
                return fail(index, "native half min/max mode");
            const auto a = half_source(0);
            const auto b = opcode == 0x840 ? HalfSource{255, 0, false, false} : half_source(1);
            if (a.swizzle == 1 || b.swizzle == 1 || b.reg >= 250 && b.reg != 255)
                return fail(index, "native half min/max source mode");
            for (u32 lane = 0; lane < 2; ++lane) {
                extract_half(a, lane, 250);
                if (opcode == 0x840) {
                    const u32 half = (insn.words[1] >> (lane * 16)) & 0xffff;
                    mov_imm(251, half);
                    extract_half(HalfSource{251, 2, false, false}, 0, 251);
                } else {
                    extract_half(b, lane, 251);
                }
                const u32 target = lane == 0 ? 252 : 250;
                emit(0x5c60000000000000ULL | predicate | target | (u64{250} << 8) | (u64{251} << 20) |
                     (u64{insn.Bits(87, 3)} << 39) | (u64{insn.Bits(90, 1)} << 42));
                emit(0x5ca8000000000000ULL | predicate | target | (u64{1} << 8) |
                     (u64{2} << 10) | (u64{target} << 20));
            }
            emit(base(0x5c18000000000000ULL) | (u64{250} << 8) | (u64{252} << 20) | (u64{16} << 39));
            break;
        }
        case 0xc24: case 0xe24: { // IMAD low word with a uniform multiplier or addend.
            if ((insn.words[2] & ~0x080002ffU) != 0x078e0000 || (insn.words[1] & ~0x800000ffU))
                return fail(index, "native uniform-addend multiply mode");
            const u32 a = operand(0, false);
            u32 b = operand(1, false), c = operand(2, false);
            if (insn.Bits(63, 1)) {
                const u32 source = opcode == 0xc24 ? b : c;
                emit(0x5c10000000000000ULL | predicate | 252 | (u64{255} << 8) |
                     (u64{source} << 20) | (u64{1} << 48));
                (opcode == 0xc24 ? b : c) = 252;
            }
            multiply_low(a, b, c, false);
            break;
        }
        case 0x224: { // IMAD register / zero-product move
            if (insn.Bits(24, 8) == 255 && insn.words[1] == 255 &&
                (insn.words[2] & ~0x078008ffU) == 0x000e0600) {
                const u32 addend = insn.Bits(64, 8);
                if (addend >= 250 && addend != 255)
                    return fail(index, "native zero-product carry add source");
                mov_imm(250, 1);
                emit(0x5ca0000000000000ULL | predicate | 250 | (u64{250} << 8) | (u64{255} << 20) |
                     (u64{insn.Bits(87, 3)} << 39) | (u64{insn.Bits(90, 1)} << 42));
                u32 source = addend;
                if (insn.Bits(75, 1)) {
                    emit(0x5c40000000000000ULL | predicate | 251 | (u64{255} << 8) |
                         (u64{addend} << 20) | (u64{1} << 40) | (u64{3} << 41) | (u64{7} << 48));
                    source = 251;
                }
                emit(base(0x5c10000000000000ULL) | (u64{source} << 8) | (u64{250} << 20));
                break;
            }
            if (insn.Bits(24, 8) == 255 && insn.words[1] == 255 && insn.Bits(75, 1) &&
                (insn.words[2] & ~0xaffU) == 0x078e0000) {
                emit(base(0x5c10000000000000ULL) | (u64{255} << 8) |
                    (u64{insn.Bits(64, 8)} << 20) | (u64{1} << 48));
                break;
            }
            if (insn.Bits(24, 8) == 255 && insn.words[1] == 255 &&
                (insn.words[2] & ~0xffU) == 0x078e0000) {
                emit(base(0x5c98000000000000ULL) | (u64{15} << 39) | (u64{insn.Bits(64, 8)} << 20));
                break;
            }
            if ((insn.words[2] & ~0x2ffU) != 0x078e0000 || (insn.words[1] & ~0xffU))
                return fail(index, "native integer register multiply mode");
            multiply_low(insn.Bits(24, 8), insn.Bits(32, 8), insn.Bits(64, 8), false);
            break;
        }
        case 0x23a: { // MOVM.16.MT88: transpose an 8x8 b16 register matrix across the warp.
            const u32 source = insn.Bits(24, 8);
            if (native_predicate != 7 || insn.words[1] || insn.words[2] ||
                dest >= reserved_register || source >= reserved_register)
                return fail(index, "native register matrix transpose mode");
            transpose_matrix(static_cast<u32>(dest), source);
            break;
        }
        case 0x83b: { // LDSM.16.M88.2/.4: distribute 8x8 matrices across the warp.
            // Diagnostic switch: reject the tensor forms as if unsupported.
            if (std::getenv("EDEN_NATIVE_DISABLE_TENSOR"))
                return fail(index, "native tensor instructions disabled");
            u32 address = insn.Bits(24, 8);
            const bool transpose = insn.Bits(78, 1);
            const u32 mode = insn.words[2] & ~0x08004000U;
            const u32 matrices = mode == 0x100 ? 2U : 4U;
            const bool uniform_offset = insn.Bits(91, 1);
            if (native_predicate != 7 || (mode != 0x100 && mode != 0x200) ||
                (!uniform_offset && insn.Bits(32, 8)) ||
                (dest & (matrices - 1)) || dest + matrices > reserved_register)
                return fail(index, "native shared matrix load mode");
            if (uniform_offset) {
                const u32 offset = uniform_register(insn.Bits(32, 8));
                if (offset == ~0U) return fail(index, "native shared matrix uniform offset");
                emit(0x5c10000000000000ULL | predicate | 253 | (u64{address} << 8) | (u64{offset} << 20));
                address = 253;
            }
            emit(0xf0c8000000000000ULL | predicate | 244);
            emit(0x3828000000000000ULL | predicate | 245 | (u64{244} << 8) | (u64{2} << 20));
            emit(0x0400000000000000ULL | predicate | 246 | (u64{244} << 8) | (u64{3} << 20));
            emit(0x3848000000000000ULL | predicate | 246 | (u64{246} << 8) | (u64{2} << 20));
            for (u32 matrix = 0; matrix < matrices; ++matrix) {
                mov_imm(254, matrix * 8);
                emit(0x5c10000000000000ULL | predicate | 247 | (u64{245} << 8) | (u64{254} << 20));
                emit(0xef10000000000000ULL | predicate | 248 | (u64{address} << 8) |
                     (u64{247} << 20) | (u64{1} << 29) | (u64{0x1f} << 34) | (u64{7} << 48));
                emit(0x5c10000000000000ULL | predicate | 248 | (u64{248} << 8) | (u64{246} << 20));
                emit(0xef48000000000000ULL | predicate | (240 + matrix) | (u64{248} << 8) |
                     (u64{insn.Bits(40, 24)} << 20) | (u64{4} << 48));
            }
            if (transpose)
                for (u32 matrix = 0; matrix < matrices; ++matrix)
                    transpose_matrix(240 + matrix, 240 + matrix);
            for (u32 matrix = 0; matrix < matrices; ++matrix)
                emit(0x5c98000000000000ULL | predicate | (dest + matrix) |
                     (u64{240 + matrix} << 20) | (u64{15} << 39));
            break;
        }
        case 0x23c: { // HMMA.1688/.16816.F16, rounding after each eight-product step.
            // Diagnostic switch: reject the tensor forms as if unsupported.
            if (std::getenv("EDEN_NATIVE_DISABLE_TENSOR"))
                return fail(index, "native tensor instructions disabled");
            const u32 a = insn.Bits(24, 8), b = insn.Bits(32, 8), c = insn.Bits(64, 8);
            const u32 mode = insn.words[2] & ~0xffU;
            const u32 depth = mode == 0 ? 8U : 16U;
            const u32 a_count = depth / 4, b_count = depth / 8;
            if (native_predicate != 7 || (mode != 0 && mode != 0x800) || (insn.words[1] & ~0xffU) ||
                (dest & 1) || dest + 2 > reserved_register || (a & (a_count - 1)) || a + a_count > reserved_register ||
                (b & (b_count - 1)) || b + b_count > reserved_register || (c != 255 && ((c & 1) || c + 2 > reserved_register)))
                return fail(index, "native half matrix multiply mode");
            const auto add = [&](u32 target, u32 lhs, u32 rhs) {
                emit(0x5c10000000000000ULL | predicate | target | (u64{lhs} << 8) | (u64{rhs} << 20));
            };
            const auto shuffle = [&](u32 target, u32 source, u32 lane) {
                emit(0xef10000000000000ULL | predicate | target | (u64{source} << 8) |
                     (u64{lane} << 20) | (u64{1} << 29) | (u64{0x1f} << 34) | (u64{7} << 48));
            };
            const auto round_half = [&](u32 reg) {
                emit(0x5ca8000000000000ULL | predicate | reg | (u64{1} << 8) |
                     (u64{2} << 10) | (u64{reg} << 20));
            };
            const auto fma = [&](u32 target, u32 lhs, u32 rhs) {
                emit(0x5980000000000000ULL | predicate | target | (u64{lhs} << 8) |
                     (u64{rhs} << 20) | (u64{target} << 39));
            };
            for (u32 row = 0; row < 2; ++row) {
                for (u32 col = 0; col < 2; ++col) {
                    extract_half({c == 255 ? 255 : c + row, 0, false, false}, col, 240 + row * 2 + col);
                }
            }
            emit(0xf0c8000000000000ULL | predicate | 250);
            emit(0x0400000000000000ULL | predicate | 251 | (u64{250} << 8) | (u64{0x1c} << 20));
            emit(0x0400000000000000ULL | predicate | 252 | (u64{250} << 8) | (u64{3} << 20));
            emit(0x3848000000000000ULL | predicate | 252 | (u64{252} << 8) | (u64{3} << 20));
            for (u32 pair = 0; pair < depth / 2; ++pair) {
                mov_imm(253, pair & 3);
                add(254, 252, 253);
                add(253, 251, 253);
                shuffle(244, a + (pair >= 4 ? 2 : 0), 253);
                shuffle(245, a + (pair >= 4 ? 3 : 1), 253);
                shuffle(246, b + pair / 4, 254);
                mov_imm(253, 4);
                add(254, 254, 253);
                shuffle(247, b + pair / 4, 254);
                for (u32 half = 0; half < 2; ++half) {
                    extract_half({246, 0, false, false}, half, 253);
                    extract_half({247, 0, false, false}, half, 254);
                    for (u32 row = 0; row < 2; ++row) {
                        extract_half({244 + row, 0, false, false}, half, 250);
                        fma(240 + row * 2, 250, 253);
                        fma(241 + row * 2, 250, 254);
                    }
                }
                if ((pair & 3) == 3) {
                    for (u32 reg = 240; reg < 244; ++reg) {
                        round_half(reg);
                        if (pair + 1 < depth / 2) extract_half({reg, 2, false, false}, 0, reg);
                    }
                }
            }
            for (u32 row = 0; row < 2; ++row) {
                emit(0x5c18000000000000ULL | predicate | (dest + row) |
                     (u64{241 + row * 2} << 8) | (u64{240 + row * 2} << 20) | (u64{16} << 39));
            }
            break;
        }
        case 0x226: { // IDP.2A.LO.U16.U8, two packed products plus a 32-bit accumulator.
            if ((insn.words[2] & ~0xffU) != 0x1000 || (insn.words[1] & ~0xffU))
                return fail(index, "native packed dot-product mode");
            const u32 a = insn.Bits(24, 8), b = insn.Bits(32, 8), c = insn.Bits(64, 8);
            for (u32 component = 0; component < 2; ++component) {
                emit(0x3828000000000000ULL | predicate | (250 + component) |
                     (u64{b} << 8) | (u64{component * 8} << 20));
                emit(0x0400000000000000ULL | predicate | (250 + component) |
                     (u64{250 + component} << 8) | (u64{0xff} << 20));
            }
            xmad(252, a, 250, c, false, false, 0, false);
            xmad(static_cast<u32>(dest), a, 251, 252, true, false, 0, false);
            break;
        }
        case 0x225: case 0x227: case 0x625: case 0x825: { // Unsigned wide/high multiply-add with optional carry output.
            const bool immediate = opcode == 0x825;
            const bool constant_addend = opcode == 0x625;
            const bool wide = opcode != 0x227;
            const u32 a = insn.Bits(24, 8), b = immediate ? insn.words[1] : insn.Bits(constant_addend ? 64 : 32, 8);
            const u32 c = constant_addend ? 255U : insn.Bits(64, 8);
            const u32 carry_output = insn.Bits(81, 3);
            const bool signed_product = insn.Bits(73, 1);
            if ((signed_product && carry_output != 7) || (insn.words[2] & ~0x000e02ffU) != 0x07800000 || (!constant_addend && !immediate && (insn.words[1] & ~0xffU)) ||
                (constant_addend && (!valid_cbuf() || insn.Bits(38, 16) % 8 ||
                    insn.Bits(38, 16) > 0xfff8 || insn.Bits(54, 5) >= 18)) ||
                (a >= 250 && a != 255) || (!immediate && b >= 250 && b != 255) || (c >= 250 && c != 255) ||
                (c != 255 && ((c & 1) || c >= 249)) ||
                (wide && ((dest & 1) || dest >= 250)) ||
                (!wide && dest >= 250 && dest != 255))
                return fail(index, "native wide/high unsigned multiply-add mode");
            const auto move = [&](u32 target, u32 source) {
                emit(0x5c98000000000000ULL | predicate | target | (u64{15} << 39) | (u64{source} << 20));
            };
            const auto add = [&](u32 target, u32 lhs, u32 rhs) {
                emit(0x5c10000000000000ULL | predicate | target | (u64{lhs} << 8) | (u64{rhs} << 20));
            };
            const auto carry_mask = [&](u32 target, u32 sum, u32 addend) {
                emit(0x5b50000000000000ULL | predicate | target | (u64{sum} << 8) |
                     (u64{addend} << 20) | (u64{7} << 39) | (u64{1} << 49));
            };
            if (constant_addend) {
                emit(0x4c98000000000000ULL | predicate | 253 | (u64{15} << 39) | cbuf());
                emit(0x4c98000000000000ULL | predicate | 254 | (u64{15} << 39) | (cbuf() + (u64{1} << 20)));
            } else {
                move(253, c);
                move(254, c == 255 ? 255 : c + 1);
            }
            multiply_high(a, b, immediate, 252);
            const u32 signed_b = immediate ? 242U : b;
            if (signed_product) {
                if (immediate) mov_imm(signed_b, b);
                emit(0x3828000000000000ULL | predicate | 240 | (u64{a} << 8) | (u64{31} << 20) | (u64{1} << 48));
                emit(0x5c40000000000000ULL | predicate | 240 | (u64{240} << 8) | (u64{signed_b} << 20) | (u64{7} << 48));
                emit(0x3828000000000000ULL | predicate | 241 | (u64{signed_b} << 8) | (u64{31} << 20) | (u64{1} << 48));
                emit(0x5c40000000000000ULL | predicate | 241 | (u64{241} << 8) | (u64{a} << 20) | (u64{7} << 48));
                add(240, 240, 241);
                emit(0x5c10000000000000ULL | predicate | 252 | (u64{252} << 8) | (u64{240} << 20) | (u64{1} << 48));
            }
            const u32 target = dest == 255 ? 250U : static_cast<u32>(dest);
            multiply_low(a, b, 255, immediate, 250);
            const u32 low_target = wide ? target : 250;
            const u32 high_target = wide ? target + 1 : target;
            add(low_target, 250, 253);
            carry_mask(251, low_target, 253);
            emit(0x3828000000000000ULL | predicate | 251 | (u64{251} << 8) | (u64{31} << 20));
            add(252, 252, 254);
            if (carry_output != 7) carry_mask(254, 252, 254);
            add(high_target, 252, 251);
            if (carry_output != 7) {
                carry_mask(253, high_target, 252);
                emit(0x5c40000000000000ULL | predicate | 254 | (u64{254} << 8) |
                     (u64{253} << 20) | (u64{1} << 41) | (u64{7} << 48));
                emit(0x5b60000000000000ULL | predicate | 7 | (u64{carry_output} << 3) |
                     (u64{254} << 8) | (u64{255} << 20) | (u64{7} << 39) | (u64{5} << 49));
            }
            break;
        }
        case 0xa27: { // Unsigned high product with a constant-buffer multiplier.
            if (insn.words[2] != 0x078e00ff || !valid_cbuf() || insn.Bits(59, 5) || insn.Bits(54, 5) >= 18)
                return fail(index, "native constant high multiply mode");
            emit(0x4c98000000000000ULL | predicate | 253 | (u64{15} << 39) | cbuf());
            multiply_high(insn.Bits(24, 8), 253, false, static_cast<u32>(dest));
            break;
        }
        case 0x827: { // High product with an immediate multiplier and zero addend.
            if ((insn.words[2] & ~0x200U) != 0x078e00ff) return fail(index, "native high multiply mode");
            const u32 a = insn.Bits(24, 8), b = insn.words[1];
            const bool signed_product = insn.Bits(73, 1);
            if (signed_product) {
                emit(0x3828000000000000ULL | predicate | 253 | (u64{a} << 8) |
                     (u64{31} << 20) | (u64{1} << 48));
                mov_imm(254, b);
                emit(0x5c40000000000000ULL | predicate | 253 | (u64{253} << 8) |
                     (u64{254} << 20) | (u64{7} << 48));
                if (b & 0x80000000U)
                    emit(0x5c10000000000000ULL | predicate | 253 | (u64{253} << 8) | (u64{a} << 20));
            }
            multiply_high(a, b, true, static_cast<u32>(dest));
            if (signed_product)
                emit(base(0x5c10000000000000ULL) | (dest << 8) | (u64{253} << 20) | (u64{1} << 48));
            break;
        }
        case 0xa24: { // IMAD, constant-buffer multiplier
            if ((insn.words[2] & ~0x2ffU) != 0x078e0000 || !valid_cbuf() || insn.Bits(62, 2))
                return fail(index, "native integer constant-buffer multiply mode");
            emit(0x4c98000000000000ULL | predicate | 252 | (u64{15} << 39) | cbuf());
            multiply_low(insn.Bits(24, 8), 252, insn.Bits(64, 8), false);
            break;
        }
        case 0x424: case 0x624: { // IMAD.MOV.U32: zero product plus immediate/cbuf
            if (opcode == 0x624 && insn.Bits(24, 8) == 255 &&
                (insn.words[2] & ~0x07800000U) == 0x000e06ffU) {
                const u32 source = operand(2, false);
                mov_imm(250, 1);
                emit(0x5ca0000000000000ULL | predicate | 250 | (u64{250} << 8) | (u64{255} << 20) |
                    (u64{insn.Bits(87, 3)} << 39) | (u64{insn.Bits(90, 1)} << 42));
                emit(base(0x5c10000000000000ULL) | (u64{source} << 8) | (u64{250} << 20));
                break;
            }
            if (insn.Bits(24, 8) != 255 || insn.Bits(64, 8) != 255 || insn.words[2] != 0x078e00ff) {
                if ((insn.words[2] & ~0x2ffU) != 0x078e0000 || (opcode == 0x624 && insn.Bits(62, 2)))
                    return fail(index, "native multiply wide addend mode");
                const u32 a = operand(0, false), b = operand(1, false), c = operand(2, false);
                multiply_low(a, b, c, false);
                break;
            }
            if (insn.Bits(24, 8) != 255 || insn.Bits(64, 8) != 255 ||
                insn.words[2] != 0x078e00ff)
                return fail(index, "native integer move mode");
            if (opcode == 0x624) {
                if (!valid_cbuf() || insn.Bits(62, 2))
                    return fail(index, "native integer move constant buffer");
                emit(base(0x4c98000000000000ULL) | (u64{15} << 39) | cbuf());
            } else {
                emit(base(0x0100000000000000ULL) | (u64{15} << 12) |
                     (u64{insn.words[1]} << 20));
            }
            break;
        }
        case 0x824: { // Captured low-word IMAD forms
            if (insn.words[1] == 1 && (insn.words[2] & ~0x078008ffU) == 0x000e0600) {
                const u32 a = insn.Bits(24, 8), c = insn.Bits(64, 8);
                if ((a >= 250 && a != 255) || (c >= 250 && c != 255) || dest >= 250)
                    return fail(index, "native unit-multiplier carry add mode");
                mov_imm(250, 1);
                emit(0x5ca0000000000000ULL | predicate | 250 | (u64{250} << 8) | (u64{255} << 20) |
                     (u64{insn.Bits(87, 3)} << 39) | (u64{insn.Bits(90, 1)} << 42));
                u32 addend = c;
                if (insn.Bits(75, 1)) {
                    emit(0x5c40000000000000ULL | predicate | 251 | (u64{255} << 8) |
                         (u64{c} << 20) | (u64{1} << 40) | (u64{3} << 41) | (u64{7} << 48));
                    addend = 251;
                }
                emit(base(0x5c10000000000000ULL) | (u64{a} << 8) | (u64{addend} << 20));
                emit(base(0x5c10000000000000ULL) | (dest << 8) | (u64{250} << 20));
                break;
            }
            if (insn.words[1] == 1 && (insn.words[2] & 0xffffff00U) == 0x078e0a00) {
                emit(base(0x5c10000000000000ULL) | (u64{insn.Bits(24, 8)} << 8) |
                     (u64{insn.Bits(64, 8)} << 20) | (u64{1} << 48));
                break;
            }
            if (insn.words[1] == 1 && (insn.words[2] & 0xffffff00U) == 0x078e0200) {
                emit(base(0x5c10000000000000ULL) | (u64{insn.Bits(24, 8)} << 8) |
                     (u64{insn.Bits(64, 8)} << 20));
                break;
            }
            if (insn.words[1] != 48 || insn.words[2] != 0x078e02ff) {
                if ((insn.words[2] & ~0x2ffU) != 0x078e0000)
                    return fail(index, "native integer multiply mode");
                multiply_low(insn.Bits(24, 8), insn.words[1], insn.Bits(64, 8), true);
                break;
            }
            const u64 source = insn.Bits(24, 8);
            emit(0x5c18000000000000ULL | predicate | 250 | (source << 8) | (source << 20) | (u64{1} << 39));
            emit(base(0x3848000000000000ULL) | (u64{250} << 8) | (u64{4} << 20));
            break;
        }
        case 0x806: { // VOTE ballot in an ordinary register and independent predicate result.
            if (insn.Bits(24, 8) || insn.words[1] || insn.Bits(72, 2) > 2 ||
                (insn.words[2] & ~(0x300U | 0xe0000U | 0x7800000U)))
                return fail(index, "native register vote mode");
            emit(base(0x50d8000000000000ULL) | (u64{insn.Bits(87, 4)} << 39) |
                 (u64{insn.Bits(81, 3)} << 45) | (u64{insn.Bits(72, 2)} << 48));
            break;
        }
        case 0x886: { // VOTEU ballot and independent uniform predicate output.
            const u32 output = insn.Bits(81, 3), target = insn.Bits(16, 8);
            if (target > 63 || insn.words[1] ||
                (output > 1 && output != 7) || insn.Bits(72, 2) > 2 || native_predicate != 7)
                return fail(index, "native uniform vote mode");
            const u32 temporary = output == 7 ? 7 : 6 - output;
            const u32 mask = 1U << temporary;
            if (output != 7) emit(0x38e8000000070000ULL | 253 | (u64{255} << 8) | (u64{mask} << 20));
            emit(0x50d8000000070000ULL | uniform_register(target) |
                (u64{insn.Bits(87, 4)} << 39) | (u64{temporary} << 45) | (u64{insn.Bits(72, 2)} << 48));
            if (output != 7) {
                emit(0x38e8000000070000ULL | 239 | (u64{239} << 8) | (u64{mask} << 20));
                emit(0x38f0000000070000ULL | (u64{253} << 8) | (u64{mask} << 20));
            }
            break;
        }
        case 0x3c2: { // R2UR: broadcast the first lane satisfying the execution predicate.
            if (dest >= 63 || insn.words[1] || insn.words[2] != 0x000e0000)
                return fail(index, "native register-to-uniform mode");
            save_predicate();
            emit(0x50d8000000070000ULL | 250 | (u64{native_predicate} << 39) |
                 (u64{6} << 45) | (u64{1} << 48));
            emit(0x5c10000000070000ULL | 251 | (u64{250} << 8) | (u64{255} << 20) | (u64{1} << 49));
            emit(0x5c40000000070000ULL | 250 | (u64{250} << 8) | (u64{251} << 20) | (u64{7} << 48));
            emit(0x5c30000000070000ULL | 250 | (u64{250} << 20));
            emit(0xef10000000070000ULL | 251 | (u64{insn.Bits(24, 8)} << 8) |
                 (u64{250} << 20) | (u64{1} << 29) | (u64{31} << 34) | (u64{7} << 48));
            emit(0x5ca0000000070000ULL | (uniform_register(static_cast<u32>(dest))) | (u64{251} << 8) |
                 (u64{uniform_register(static_cast<u32>(dest))} << 20) | (u64{6} << 39));
            restore_predicate();
            break;
        }
        case 0x89c: { // Uniform predicate AND with an optionally inverted third source.
            const u32 target = insn.Bits(81, 3);
            const u32 lut = insn.Bits(64, 3) | (insn.Bits(72, 5) << 3);
            if (native_predicate != 7 || target == 7 || insn.Bits(84, 3) != 7 ||
                insn.Bits(16, 8) || insn.words[1] || (lut != 0x80 && lut != 0x40))
                return fail(index, "native uniform predicate LUT mode");
            const auto mapped = [](u32 source) { return ((source & 7) == 7 ? 7 : 6 - (source & 7)) | (source & 8); };
            emit(0x38e8000000070000ULL | 253 | (u64{255} << 8) | (u64{0x7f} << 20));
            emit(0x38f0000000070000ULL | (u64{239} << 8) | (u64{0x7f} << 20));
            emit(0x5090000000070000ULL | (u64{6 - target} << 3) | 7 |
                 (u64{mapped(insn.Bits(87, 4))} << 12) | (u64{mapped(insn.Bits(77, 4))} << 29) |
                 (u64{mapped(insn.Bits(68, 4)) ^ (lut == 0x40 ? 8U : 0U)} << 39));
            emit(0x38e8000000070000ULL | 239 | (u64{239} << 8) | (u64{1} << (26 - target)));
            emit(0x38f0000000070000ULL | (u64{253} << 8) | (u64{0x7f} << 20));
            break;
        }
        case 0x81c: { // Observed predicate LUT constants and UP0-to-predicate copy.
            const u32 target = insn.Bits(81, 3);
            const u32 lut = insn.Bits(64, 3) | (insn.Bits(72, 5) << 3);
            if ((native_predicate != 7 && insn.Bits(67, 1)) || target == 7 || insn.Bits(84, 3) != 7 ||
                insn.Bits(16, 8) || insn.words[1])
                return fail(index, "native predicate LUT mode");
            if (!insn.Bits(67, 1) && (lut == 0xfe || lut == 0xa8 || lut == 0xef)) {
                emit(0x5090000000000000ULL | predicate | (u64{target} << 3) | 7 |
                     (u64{insn.Bits(87, 4) ^ (lut == 0xef ? 8U : 0U)} << 12) | (u64{1} << 24) |
                     (u64{insn.Bits(77, 4)} << 29) | (u64{insn.Bits(68, 4)} << 39) |
                     (u64{lut != 0xa8} << 45));
                break;
            }
            if (!insn.Bits(67, 1) && (lut == 0x80 || lut == 8 || lut == 0x20 || lut == 2)) {
                emit(0x5090000000000000ULL | predicate | (u64{target} << 3) | 7 |
                     (u64{insn.Bits(87, 4) ^ ((lut == 8 || lut == 2) ? 8U : 0U)} << 12) |
                     (u64{insn.Bits(77, 4) ^ ((lut == 0x20 || lut == 2) ? 8U : 0U)} << 29) |
                     (u64{insn.Bits(68, 4)} << 39));
                break;
            }
            if (insn.Bits(67, 1) && insn.Bits(68, 4) < 7 && insn.Bits(77, 4) == 7 &&
                (lut == 0x80 || lut == 0x40) && native_predicate == 7 && target < 6 &&
                insn.Bits(87, 3) != 6) {
                save_predicate();
                emit(0x3828000000070000ULL | 250 | (u64{239} << 8) |
                     (u64{6 - insn.Bits(68, 4)} << 20));
                emit(0x3848000000070000ULL | 250 | (u64{250} << 8) | (u64{6} << 20));
                emit(0x38f0000000070000ULL | (u64{250} << 8) | (u64{0x40} << 20));
                emit(0x5090000000070000ULL | (u64{target} << 3) | 7 |
                     (u64{insn.Bits(87, 4)} << 12) | (u64{7} << 29) |
                     (u64{lut == 0x40 ? 14U : 6U} << 39));
                restore_predicate();
                break;
            }
            const bool bounded_plop3 = insn.Bits(77, 4) == 7 && insn.Bits(87, 4) == 7 &&
                ((insn.Bits(67, 1) && insn.Bits(68, 4) < 7 && (lut == 0x80 || lut == 0x40)) ||
                 (!insn.Bits(67, 1) && insn.Bits(68, 4) == 7 && (lut == 0x80 || lut == 8)));
            if (!bounded_plop3) { // [merge-plop3-generic]
                // Any other three-input truth table over P0-P6/UP0-UP6: search PSETP's (x1 op1 x2) op2 x3.
                // Operand k is A (bits 87..90), B (77..80) or C (68..71); the uniform form reads UPs from
                // all three, PLOP3 reads a UP only from C (selected by bit 67).
                const u32 fields[3] = {insn.Bits(87, 4), insn.Bits(77, 4), insn.Bits(68, 4)};
                u32 reg[3], neg[3];
                bool is_uniform[3];
                for (u32 k = 0; k < 3; ++k) {
                    is_uniform[k] = (false || (k == 2 && insn.Bits(67, 1))) && (fields[k] & 7) != 7;
                    neg[k] = fields[k] >> 3;
                    reg[k] = is_uniform[k] ? 6 - (fields[k] & 7) : (fields[k] & 7);
                }
                // Search PSETP's (x1 op1 x2) op2 x3 for a form with exactly the requested truth table.
                // Slot choices: 0..5 = A, !A, B, !B, C, !C; 6 = constant false; 7 = constant true.
                const auto slot_value = [](u32 choice, u32 row) -> u32 {
                    if (choice >= 6) return choice == 7 ? 1U : 0U;
                    return ((row >> (2 - choice / 2)) & 1) ^ (choice & 1);
                };
                const auto combine_bits = [](u32 op, u32 x, u32 y) -> u32 {
                    return op == 0 ? (x & y) : op == 1 ? (x | y) : (x ^ y);
                };
                u32 best = ~0U, best_cost = ~0U;
                for (u32 candidate = 0; candidate < 8 * 8 * 8 * 9; ++candidate) {
                    const u32 s1 = candidate % 8, s2 = (candidate / 8) % 8, s3 = (candidate / 64) % 8, ops = candidate / 512;
                    const u32 op1 = ops % 3, op2 = ops / 3;
                    bool match = true;
                    for (u32 row = 0; row < 8 && match; ++row) {
                        const u32 value = combine_bits(op2, combine_bits(op1, slot_value(s1, row), slot_value(s2, row)),
                                                       slot_value(s3, row));
                        match = value == ((lut >> row) & 1);
                    }
                    if (!match) continue;
                    u32 used = 0;
                    for (u32 choice : {s1, s2, s3}) if (choice < 6) used |= 1U << (choice / 2);
                    u32 cost = 0;
                    for (u32 k = 0; k < 3; ++k) if ((used >> k) & 1) cost += is_uniform[k] ? 4 : 1;
                    if (cost < best_cost) { best = candidate; best_cost = cost; }
                }
                if (best == ~0U) return fail(index, "native predicate LUT truth table");
                const u32 chosen[3] = {best % 8, (best / 8) % 8, (best / 64) % 8};
                const u32 chosen_ops = best / 512;
                u64 slot_bits[3];
                u32 load_mask = 0, regular_mask = 0;
                for (u32 slot = 0; slot < 3; ++slot) {
                    const u32 choice = chosen[slot];
                    u32 register_index, negate;
                    if (choice >= 6) {
                        register_index = 7; negate = choice == 6 ? 1 : 0;
                    } else {
                        const u32 k = choice / 2;
                        register_index = reg[k]; negate = neg[k] ^ (choice & 1);
                        if (is_uniform[k]) load_mask |= 1U << register_index;
                        else if (register_index != 7) regular_mask |= 1U << register_index;
                    }
                    slot_bits[slot] = register_index | (u64{negate} << 3);
                }
                // A loaded uniform predicate must not overwrite a real predicate that is still read.
                if (load_mask & regular_mask) return fail(index, "native predicate LUT truth table");
                const u32 dest_position = false ? 6 - target : target;
                const u32 dest_mask = 1U << dest_position;
                // The PLOP3 destination is a live output; the UPLOP3 destination is only a staging slot.
                const u32 save_mask = false ? (load_mask | dest_mask) : (load_mask & ~dest_mask);
                if (save_mask) emit(0x38e8000000070000ULL | 253 | (u64{255} << 8) | (u64{save_mask} << 20));
                if (load_mask) emit(0x38f0000000070000ULL | (u64{239} << 8) | (u64{load_mask} << 20));
                emit(0x5090000000000000ULL | (false ? (u64{7} << 16) : predicate) |
                     (u64{dest_position} << 3) | 7 | (slot_bits[0] << 12) | (u64{chosen_ops % 3} << 24) |
                     (slot_bits[1] << 29) | (slot_bits[2] << 39) | (u64{chosen_ops / 3} << 45));
                if (false)
                    emit(0x38e8000000070000ULL | 239 | (u64{239} << 8) | (u64{dest_mask} << 20));
                if (save_mask) emit(0x38f0000000070000ULL | (u64{253} << 8) | (u64{save_mask} << 20));
                break;
            }
            if (insn.Bits(67, 1) && insn.Bits(68, 4) < 7 && (lut == 0x80 || lut == 0x40)) {
                emit(0x3828000000070000ULL | 250 | (u64{239} << 8) | (u64{6 - insn.Bits(68, 4)} << 20));
                if (lut == 0x40) emit(0x3840000000070000ULL | 250 | (u64{250} << 8) | (u64{1} << 20) |
                     (u64{2} << 41) | (u64{7} << 48));
                emit(0x3848000000070000ULL | 250 | (u64{250} << 8) | (u64{target} << 20));
            } else if (!insn.Bits(67, 1) && insn.Bits(68, 4) == 7 && (lut == 0x80 || lut == 8)) {
                emit(0x010000000007f000ULL | 250 | ((lut == 0x80 ? u64{1} << target : 0ULL) << 20));
            } else return fail(index, "native predicate LUT truth table");
            emit(0x38f0000000070000ULL | (u64{250} << 8) | (u64{1} << (target + 20)));
            break;
        }
        case 0x299: { // [merge-ushf-reg] USHF.L.W.U32.HI Ud, URZ, Us, Uc: Ud = Uc << (Us & 31), register shift amount
            const u32 high = insn.Bits(64, 8);
            const u32 source = uniform_register(insn.words[1]), carrier = uniform_register(high);
            if (insn.Bits(24, 8) != 63 || insn.words[2] != (0x08010e00U | high) || dest >= 63 ||
                insn.words[1] >= 64 || source == ~0U || carrier == ~0U || native_predicate != 7)
                return fail(index, "native uniform shift mode");
            emit(0x5c48000000070000ULL | uniform_register(static_cast<u32>(dest)) | (u64{carrier} << 8) |
                 (u64{source} << 20) | (u64{1} << 39));
            break;
        }
        case 0x882: { // UMOV immediate.
            if (dest >= 63 || insn.words[2]) return fail(index, "native uniform immediate move mode");
            mov_imm(uniform_register(static_cast<u32>(dest)), insn.words[1]);
            break;
        }
        case 0xc82: { // UMOV UR, UR, including uniform execution predicates.
            if (dest >= 63 || insn.words[1] >= 64 || insn.words[2] != 0x08000000)
                return fail(index, "native uniform move mode");
            emit(0x5c98000000000000ULL | predicate | (uniform_register(static_cast<u32>(dest))) |
                 (u64{uniform_register(insn.words[1])} << 20) | (u64{15} << 39));
            break;
        }
        case 0xab9: case 0xabb: { // ULDC U32/U64, direct or indexed.
            const u32 uniform_dest = insn.Bits(16, 8), index_reg = insn.Bits(24, 8);
            const u32 width = insn.Bits(73, 3);
            if (uniform_dest >= 63 || (width != 4 && width != 5) ||
                (width == 5 && ((uniform_dest & 1) || uniform_dest >= 62)) ||
                insn.Bits(38, 16) % 4 || insn.Bits(62, 2) ||
                (insn.words[2] & ~0xe00U) != (opcode == 0xab9 ? 0U : 0x08000000U) ||
                (opcode == 0xabb && index_reg >= 63))
                return fail(index, "native uniform constant load mode");
            if (opcode == 0xab9 && width == 4) {
                emit(0x4c98000000000000ULL | predicate | (uniform_register(uniform_dest)) | (u64{15} << 39) | cbuf());
            } else {
                emit(0xef90000000000000ULL | predicate | (uniform_register(uniform_dest)) |
                     (u64{opcode == 0xab9 ? 255U : uniform_register(index_reg)} << 8) | (u64{insn.Bits(38, 16)} << 20) |
                     (u64{insn.Bits(54, 5)} << 36) | (u64{width} << 48));
            }
            break;
        }
        case 0x899: { // USHF; accept a zero word outside the selected half.
            const u32 dst = insn.Bits(16, 8), low = insn.Bits(24, 8), high = insn.Bits(64, 8);
            const bool signed_right = insn.words[2] == (0x08011400U | high);
            const bool unsigned_right = insn.words[2] == (0x08011600U | high);
            if ((signed_right || unsigned_right) && low == 63 && high < 63 &&
                dst < 63 && insn.words[1] <= 32) {
                emit(0x3828000000000000ULL | predicate | uniform_register(static_cast<u32>(dst)) |
                     (u64{uniform_register(high)} << 8) | (u64{insn.words[1]} << 20) | (u64{signed_right} << 48));
                break;
            }
            const bool high_result = insn.words[2] == (0x08010600U | high) && low == 63 && high < 63;
            const bool low_result = insn.words[2] == 0x0800063f && low < 63;
            if (dst >= 63 || insn.words[1] > 32 || (!high_result && !low_result))
                return fail(index, "native uniform shift mode");
            emit(0x3848000000000000ULL | predicate | uniform_register(static_cast<u32>(dst)) |
                 (u64{uniform_register(high_result ? high : low)} << 8) | (u64{insn.words[1]} << 20));
            break;
        }
        case 0xb82: { // LDC.64 indexed byte offset
            const u32 width = insn.Bits(73, 3);
            if ((width != 0 && width != 4 && width != 5) || insn.Bits(78, 2) || insn.Bits(91, 1) ||
                insn.Bits(54, 5) >= 18 ||
                (width == 5 && ((dest & 1) || dest >= 254))) return fail(index, "native indexed constant load mode");
            emit(base(0xef90000000000000ULL) | (u64{insn.Bits(24, 8)} << 8) |
                 (u64{insn.Bits(38, 16)} << 20) | (u64{insn.Bits(54, 5)} << 36) | (u64{width} << 48));
            break;
        }
        case 0xf89: { // SHFL.IDX/BFLY with immediate lane and segmentation mask.
            const u32 lane = insn.Bits(53, 5), mask = insn.Bits(40, 13);
            const u32 mode = insn.Bits(58, 6);
            if ((insn.words[2] & ~0x000e0000U) || insn.Bits(32, 8) ||
                (mode != 0 && mode != 2 && mode != 3) || (mask & 0xe0))
                return fail(index, "native butterfly shuffle mode");
            emit(base(0xef10000000000000ULL) | (u64{insn.Bits(24, 8)} << 8) |
                 (u64{lane} << 20) | (u64{1} << 28) | (u64{1} << 29) |
                 (u64{mode} << 30) | (u64{mask} << 34) | (u64{insn.Bits(81, 3)} << 48));
            break;
        }
        case 0x822: { // FSWZADD, bounded quad derivative sign patterns.
            const u32 swizzle = insn.words[1];
            if ((swizzle != 0x66 && swizzle != 0x5a) || insn.words[2] > 255)
                return fail(index, "native swizzled add mode");
            // Maxwell encodes operand signs rather than the native operation enum.
            emit(base(0x50f8000000000000ULL) | (u64{insn.Bits(24, 8)} << 8) |
                 (u64{insn.Bits(64, 8)} << 20) | (u64{swizzle == 0x66 ? 0x99U : 0xa5U} << 28));
            break;
        }
        case 0xb6c: { // TXD, 2D and array-2D explicit gradients.
            const u32 binding = insn.Bits(54, 5), offset = insn.Bits(40, 14);
            const u32 coordinates = insn.Bits(24, 8), derivatives = insn.Bits(32, 8);
            const u32 dimension = insn.Bits(61, 3), mask = insn.Bits(72, 4);
            const u32 channels = std::popcount(mask), second_dest = insn.Bits(64, 8);
            if ((insn.words[2] & ~(0xfffU | (1U << 26))) != 0x001e0000 || insn.Bits(59, 2) ||
                (dimension != 1 && dimension != 5) || binding >= 18 || offset > 0x1fff ||
                !mask || dest >= 248 || (channels > 1 && (dest & 1)) ||
                (channels > 2 ? second_dest >= 248 || (channels == 4 && (second_dest & 1)) : second_dest != 255) ||
                coordinates >= 248 || derivatives >= 247)
                return fail(index, "native gradient texture mode");
            const bool bindless = result.required_texture_buffer != ~0U && result.required_texture_buffer != binding;
            if (!bindless) result.required_texture_buffer = binding;
            if (bindless) {
                emit(0x4c98000000000000ULL | predicate | 250 | (u64{15} << 39) |
                     (u64{offset} << 20) | (u64{binding} << 34));
                for (u32 component = 0; component < (dimension == 5 ? 3U : 2U); ++component)
                    emit(0x5c98000000000000ULL | predicate | (251 + component) | (u64{15} << 39) |
                         (u64{coordinates + component} << 20));
            }
            const bool split_output = channels > 2 && second_dest != dest + 2;
            const u32 output = split_output ? 250U : static_cast<u32>(dest);
            emit((bindless ? 0xde40000000000000ULL : 0xde00000000000000ULL) | predicate | output |
                 (u64{bindless ? 250U : coordinates} << 8) | (u64{derivatives} << 20) |
                 (u64{dimension == 5 ? 3U : 2U} << 28) | (u64{mask} << 31) |
                 (bindless ? 0 : u64{offset} << 36) | (u64{insn.Bits(90, 1)} << 49) | (u64{7} << 51));
            if (split_output) {
                for (u32 component = 0; component < channels; ++component) {
                    const u32 target = component < 2 ? static_cast<u32>(dest) + component : second_dest + component - 2;
                    emit(0x5c98000000000000ULL | predicate | target | (u64{15} << 39) |
                         (u64{250 + component} << 20));
                }
            }
            break;
        }
        case 0x989: { // SHFL.UP, immediate lane distance and register segmentation mask.
            if ((insn.words[2] & ~0x000e00ffU) || insn.Bits(32, 21) ||
                insn.Bits(58, 6) != 1)
                return fail(index, "native upward shuffle mode");
            emit(base(0xef10000000000000ULL) | (u64{insn.Bits(24, 8)} << 8) |
                 (u64{insn.Bits(53, 5)} << 20) | (u64{1} << 28) | (u64{1} << 30) |
                 (u64{insn.Bits(64, 8)} << 39) | (u64{insn.Bits(81, 3)} << 48));
            break;
        }
        case 0x589: { // SHFL.IDX/.DOWN, register lane and immediate segmentation mask.
            // [merge-shfl-down] Bits 58-59 select the mode with the same numbering as Maxwell (0 IDX, 2 DOWN).
            const u32 shuffle_mode = insn.Bits(58, 2);
            if ((insn.words[2] & ~0x000e0000U) || insn.Bits(53, 5) || insn.Bits(60, 4) ||
                (shuffle_mode != 0 && shuffle_mode != 2) || (insn.Bits(40, 13) & 0xe0))
                return fail(index, "native shuffle mode");
            emit(0xef10000000000000ULL | predicate | dest |
                (u64{insn.Bits(24, 8)} << 8) | (u64{insn.Bits(32, 8)} << 20) |
                (u64{1} << 29) | (u64{shuffle_mode} << 30) | (u64{insn.Bits(40, 13)} << 34) | (u64{insn.Bits(81, 3)} << 48));
            break;
        }
        case 0x98e: { // RED.E.ADD/MAX.STRONG.GPU U32; uniform pointers support ADD.
            u32 address = insn.Bits(24, 8);
            if ((insn.words[2] & ~0xffU) == 0x0c10e100 && address == 255 &&
                insn.Bits(40, 24) == 0 && insn.Bits(64, 8) < 63 - 1 && !(insn.Bits(64, 8) & 1)) {
                const u32 uniform = uniform_register(insn.Bits(64, 8));
                for (u32 word = 0; word < 2; ++word)
                    emit(0x5c98000000000000ULL | predicate | (250 + word) |
                         (u64{uniform + word} << 20) | (u64{15} << 39));
                address = 250;
            } else if ((insn.words[2] & ~0x03800000U) != 0x0010e100 || // [merge-red-ops] bits 87-89 pick ADD/MIN/MAX/INC/DEC/AND/OR/XOR
                       insn.Bits(40, 24) || (address & 1)) {
                return fail(index, "native global reduction mode");
            }
            emit(0xebf8000000000000ULL | predicate | insn.Bits(32, 8) |
                (u64{address} << 8) | (u64{insn.Bits(87, 3)} << 23) | (u64{1} << 48));
            break;
        }
        case 0x2bd: // UFLO.U32, optionally returning the normalization shift.
            if (dest >= 63 || insn.words[1] >= 64 || (insn.words[2] & ~0x400U) != 0x080e0000 || native_predicate != 7)
                return fail(index, "native uniform leading bit mode");
            emit(0x5c30000000000000ULL | predicate | (uniform_register(static_cast<u32>(dest))) | (u64{uniform_register(insn.words[1])} << 20) | (u64{insn.Bits(74, 1)} << 41));
            break;
        case 0x3a1: { // MATCH.ALL.U32/U64 with only the all-equal predicate retained.
            const u32 source = insn.Bits(24, 8), target = insn.Bits(81, 3);
            const bool wide = insn.Bits(73, 1);
            if (dest != 255 || source >= 254 || (wide && (source & 1)) || target >= 6 ||
                native_predicate != 7 || insn.words[1] || (insn.words[2] & ~0xe0200U))
                return fail(index, "native warp match mode");
            save_predicate();
            emit(0x50d8000000070000ULL | 250 | (u64{7} << 39) | (u64{7} << 45) | (u64{1} << 48));
            emit(0x5c10000000070000ULL | 251 | (u64{250} << 8) | (u64{255} << 20) | (u64{1} << 49));
            emit(0x5c40000000070000ULL | 250 | (u64{250} << 8) | (u64{251} << 20) | (u64{7} << 48));
            emit(0x5c30000000070000ULL | 250 | (u64{250} << 20));
            for (u32 word = 0; word < (wide ? 2U : 1U); ++word)
                emit(0xef10000000070000ULL | (251 + word) | (u64{source + word} << 8) |
                    (u64{250} << 20) | (u64{1} << 29) | (u64{31} << 34) | (u64{7} << 48));
            emit(0x5b60000000070000ULL | 7 | (u64{6} << 3) | (u64{source} << 8) |
                (u64{251} << 20) | (u64{7} << 39) | (u64{2} << 49));
            if (wide) emit(0x5b60000000070000ULL | 7 | (u64{6} << 3) | (u64{source + 1} << 8) |
                (u64{252} << 20) | (u64{6} << 39) | (u64{2} << 49));
            emit(0x50d8000000070000ULL | 255 | (u64{6} << 39) | (u64{target} << 45));
            restore_predicate();
            break;
        }
        case 0x3c4: { // REDUX.SUM/OR/MIN/MAX.U32 over the actual active lanes.
            const u32 source = insn.Bits(24, 8);
            if (dest >= 63 || insn.words[1] || (insn.words[2] != 0xc000 && insn.words[2] != 0x4000 && insn.words[2] != 0x10000 && insn.words[2] != 0x14000) || native_predicate != 7)
                return fail(index, "native warp sum mode");
            emit(0x50d8000000070000ULL | 250 | (u64{7} << 39) | (u64{7} << 45) | (u64{1} << 48));
            mov_imm(uniform_register(static_cast<u32>(dest)), insn.words[2] == 0x10000 ? 0xffffffffU : 0U);
            for (u32 lane = 0; lane < 32; ++lane) {
                emit(0xef10000000070000ULL | 251 | (u64{source} << 8) | (u64{lane} << 20) |
                    (u64{1} << 28) | (u64{1} << 29) | (u64{31} << 34) | (u64{7} << 48));
                emit(0x3828000000070000ULL | 252 | (u64{250} << 8) | (u64{lane} << 20));
                emit(0x0400000000070000ULL | 252 | (u64{252} << 8) | (u64{1} << 20));
                emit(0x5c10000000070000ULL | 252 | (u64{255} << 8) | (u64{252} << 20) | (u64{1} << 48));
                emit(0x5c40000000070000ULL | 251 | (u64{251} << 8) | (u64{252} << 20) |
                     (insn.words[2] == 0x10000 ? (u64{1} << 40) | (u64{1} << 41) : 0) | (u64{7} << 48));
                if (insn.words[2] == 0x4000) {
                    emit(0x5c40000000070000ULL | (uniform_register(static_cast<u32>(dest))) | (u64{uniform_register(static_cast<u32>(dest))} << 8) |
                         (u64{251} << 20) | (u64{1} << 41) | (u64{7} << 48));
                } else if (insn.words[2] == 0x10000 || insn.words[2] == 0x14000) {
                    emit(0x5c20000000070000ULL | (uniform_register(static_cast<u32>(dest))) | (u64{uniform_register(static_cast<u32>(dest))} << 8) |
                         (u64{251} << 20) | (u64{7} << 39) | (u64{insn.words[2] == 0x14000} << 42));
                } else {
                    emit(0x5c10000000070000ULL | (uniform_register(static_cast<u32>(dest))) | (u64{uniform_register(static_cast<u32>(dest))} << 8) | (u64{251} << 20));
                }
            }
            break;
        }
        case 0xf8c: { // ATOMS.POPC.INC.32 with no returned value: one increment per active lane.
            const u32 stride = insn.Bits(78, 2);
            if (dest != 255 || insn.words[1] || (insn.words[2] & ~0xc000U) != 0x0d00003f)
                return fail(index, "native shared increment mode");
            u32 address = insn.Bits(24, 8);
            if (stride && address != 255) {
                emit(0x3848000000000000ULL | predicate | 251 | (u64{address} << 8) |
                    (u64{stride + 1} << 20));
                address = 251;
            }
            mov_imm(250, 1);
            emit(0xec00000000000000ULL | predicate | 255 | (u64{address} << 8) | (u64{250} << 20));
            break;
        }
        case 0x38c: { // ATOMS.ADD/MIN/MAX/OR.U32, naturally aligned shared byte offset.
            const u32 offset = insn.Bits(40, 24);
            const u32 operation = insn.Bits(87, 4);
            if ((operation != 0 && operation != 1 && operation != 2 && operation != 6) ||
                insn.words[2] != (operation << 23) || (offset & 3))
                return fail(index, "native shared atomic mode");
            emit(0xec00000000000000ULL | predicate | dest |
                (u64{insn.Bits(24, 8)} << 8) | (u64{insn.Bits(32, 8)} << 20) |
                (u64{offset >> 2} << 30) | (u64{operation} << 52));
            break;
        }
        case 0x3a8: { // [merge-atomg-reg] ATOMG.E.ADD.STRONG.GPU U32 through a register address pair, returning the old value.
            // SM70 encoding: address 24..31 with the .E bit at 72, data 32..39, immediate offset 40..63,
            // type 73..75, ordering 76..79, no predicate output (81..83 = PT), operation 87..90 and no
            // uniform-register addressing (bit 91). Only a zero offset and the captured ADD U32 form pass.
            const u32 address = insn.Bits(24, 8), data = insn.Bits(32, 8);
            if (address >= 254 || (address & 1) || data == 255 || insn.Bits(40, 24) ||
                insn.words[2] != 0x001ee100)
                return fail(index, "native global register atomic mode");
            emit(0xed00000000000000ULL | predicate | dest | (u64{address} << 8) |
                (u64{data} << 20) | (u64{1} << 48));
            break;
        }
        case 0x9a8: { // ATOMG.E.ADD.STRONG.GPU U32 through a uniform pointer.
            const u32 address = uniform_register(insn.Bits(64, 6));
            if (insn.Bits(24, 8) != 255 || insn.Bits(40, 24) ||
                (insn.words[2] & ~0x3fU) != 0x081ee140 || address == ~0U || (address & 1))
                return fail(index, "native global atomic mode");
            emit(0xed00000000000000ULL | predicate | dest | (u64{address} << 8) |
                (u64{insn.Bits(32, 8)} << 20) | (u64{1} << 48));
            break;
        }
        case 0xb97: { // SULD.P.2D.IGN, direct descriptor and R, RG, or RGBA channels.
            const u32 binding = insn.Bits(54, 5), offset = insn.Bits(40, 14);
            const u32 coordinate = insn.Bits(24, 8);
            const u32 mask = insn.Bits(72, 4);
            if ((insn.words[2] & ~0xf00U) != 0x001e0000 || (mask != 1 && mask != 3 && mask != 15) ||
                insn.Bits(61, 3) != 3 || binding >= 18 || offset > 0x1fff ||
                insn.Bits(59, 2) || dest == 255 || coordinate > 253 ||
                (mask == 3 && ((dest & 1) || dest > 254)) ||
                (mask == 15 && ((dest & 3) || dest > 252)))
                return fail(index, "native formatted surface load mode");
            if (result.required_texture_buffer != ~0U && result.required_texture_buffer != binding)
                return fail(index, "native surface descriptor buffer conflict");
            result.required_texture_buffer = binding;
            emit(base(0xeb00000000000000ULL) | (u64{coordinate} << 8) |
                (u64{mask} << 20) | (u64{3} << 33) | (u64{offset} << 36) | (u64{1} << 51));
            break;
        }
        case 0xb9b: { // SUST.P.2D/array/3D.IGN, all four channels or R alone.
            const u32 binding = insn.Bits(54, 5), offset = insn.Bits(40, 14);
            const u32 data = insn.Bits(32, 8), coordinate = insn.Bits(24, 8);
            const u32 dimension = insn.Bits(61, 3);
            const bool strong = (insn.words[2] & ~0xf00U) == 0x0010a000;
            const u32 store_mask = (insn.words[2] >> 8) & 15;
            if ((!strong && (insn.words[2] & ~0xf00U) != 0x00100000) || (store_mask != 15 && store_mask != 1) ||
                (dimension != 3 && dimension != 4 && dimension != 5) ||
                binding >= 18 || offset > 0x1fff || insn.Bits(59, 2) || data > 252 || coordinate > (dimension == 3 ? 253U : 252U))
                return fail(index, "native formatted surface store mode");
            const bool bindless = result.required_texture_buffer != ~0U && result.required_texture_buffer != binding;
            if (bindless) {
                emit(0x4c98000000000000ULL | predicate | 250 | (u64{15} << 39) |
                     (u64{offset} << 20) | (u64{binding} << 34));
            } else {
                result.required_texture_buffer = binding;
            }
            if (strong) emit(0xef98000000000000ULL | predicate | (u64{1} << 8));
            emit(0xeb20000000000000ULL | predicate | data | (u64{coordinate} << 8) |
                (u64{store_mask} << 20) | (u64{dimension} << 33) |
                (bindless ? (u64{250} << 39) : (u64{offset} << 36) | (u64{1} << 51)));
            if (strong) emit(0xef98000000000000ULL | predicate | (u64{1} << 8));
            break;
        }
        case 0x213: case 0xa13: { // IABS.S32 preserves the two's-complement minimum value.
            if (insn.Bits(24, 8) || insn.words[2] ||
                (opcode == 0x213 ? insn.words[1] > 255 :
                 (!valid_cbuf() || insn.Bits(62, 2))))
                return fail(index, "native integer absolute mode");
            const u32 source = operand(1, false);
            emit(base(0x5ce0000000000000ULL) | 0x3a00 | (u64{source} << 20) |
                 (u64{1} << 49));
            break;
        }
        case 0x2be: { // UBREV, full-width bit reversal in the uniform register bank.
            const u32 source = uniform_register(insn.words[1]);
            if (dest >= 63 || source == ~0U || native_predicate != 7 ||
                insn.Bits(24, 8) || insn.words[2] != 0x08000000)
                return fail(index, "native uniform bit reverse mode");
            emit(0x3828000000070000ULL | uniform_register(static_cast<u32>(dest)) | (u64{source} << 8) |
                 (u64{1} << 39) | (u64{1} << 40));
            break;
        }
        case 0x301: // BREV register.
            if (insn.words[2] || insn.words[1] > 255) return fail(index, "native bit reverse mode");
            emit(base(0x3828000000000000ULL) | (u64{insn.words[1]} << 8) |
                (u64{1} << 39) | (u64{1} << 40));
            break;
        case 0x219: case 0xa19: { // U32 shifts with a zero word outside the selected result.
            const u32 low = insn.Bits(24, 8), high = insn.Bits(64, 8);
            const u32 shift = opcode == 0xa19 ? materialize(2, 0, 251, false, false) : insn.Bits(32, 8);
            const bool right = insn.Bits(76, 1) != 0, high_result = insn.Bits(80, 1) != 0;
            if ((opcode == 0x219 && insn.words[1] > 255) ||
                (opcode == 0xa19 && insn.Bits(62, 2)) || insn.Bits(73, 2) != 3 ||
                (insn.words[2] & ~0x000118ffU) != 0x00000600U ||
                (right ? (!high_result || low != 255) : (high_result || high != 255)))
                return fail(index, "native register shift mode");
            emit(base(right ? 0x5cf8000000000000ULL : 0x5bf8000000000000ULL) |
                (u64{right ? high : 255U} << 8) | (u64{shift} << 20) |
                (u64{right ? 255U : low} << 39) | (u64{insn.Bits(75, 1)} << 50));
            break;
        }
        case 0x300: case 0xd00: { // FLO, including a uniform source.
            if ((insn.words[2] & ~(0x600U | (opcode == 0xd00 ? 0x08000000U : 0U))) != 0x000e0000U)
                return fail(index, "native leading bit mode");
            const u32 source = operand(1, false);
            emit(base(0x5c30000000000000ULL) | (u64{source} << 20) |
                (u64{insn.Bits(63, 1)} << 40) | (u64{insn.Bits(74, 1)} << 41) |
                (u64{insn.Bits(73, 1)} << 48));
            break;
        }
        case 0x309: case 0xd09: { // POPC, including a uniform source.
            if (insn.words[2] != (opcode == 0xd09 ? 0x08000000U : 0U))
                return fail(index, "native population count mode");
            const u32 source = operand(1, false);
            emit(base(0x5c08000000000000ULL) | (u64{source} << 20) | (u64{insn.Bits(63, 1)} << 40));
            break;
        }
        case 0x95c:
            if (insn.words[0] != 0x795c || insn.words[1] != 4 || insn.words[2] != 0x00300000 ||
                std::find(verified_traps.begin(), verified_traps.end(), index * sizeof(Instruction)) == verified_traps.end())
                return fail(index, "native trap requires a verified uniform guard");
            emit(0x50b0000000000f00ULL | predicate);
            break;
        case 0x918:
            if (insn.words[1] || insn.words[2]) return fail(index, "native NOP mode");
            emit(0x50b0000000000f00ULL | predicate);
            break;
        case 0x992: { // MEMBAR.SC: preserve workgroup/device visibility.
            const u32 scope = insn.Bits(76, 3);
            if (insn.words[1] || insn.words[2] != (scope << 12) ||
                (scope != 0 && scope != 2 && scope != 3 && scope != 5))
                return fail(index, "native memory barrier scope");
            const u32 legacy_scope = scope == 0 ? 0 : scope == 2 ? 1 : scope == 3 ? 2 : 3;
            emit(0xef98000000000000ULL | predicate | (u64{legacy_scope} << 8));
            break;
        }
        case 0x9ab: // ERRBAR: wait for prior memory operations on the host backend.
            if (insn.words[1] || insn.words[2]) return fail(index, "native error barrier mode");
            emit(0xef98000000000000ULL | predicate | (u64{1} << 8));
            break;
        case 0xb1d: // BAR.SYNC 0, full workgroup.
            if (insn.words[1] || (insn.words[2] & ~0x10000U) || native_predicate != 7)
                return fail(index, "native workgroup barrier mode");
            emit(0xf0a8000000000000ULL | predicate | (u64{1} << 43) |
                (u64{1} << 44) | (u64{1} << 39) | (u64{7} << 39));
            break;
        case 0x388: case 0x984: case 0x988: { // Shared memory load/store with scaled register offsets.
            const u32 width = insn.Bits(73, 3), stride = insn.Bits(78, 2);
            const u32 uniform_source = opcode == 0x988 ? insn.Bits(64, 8) : insn.Bits(32, 8);
            const bool uniform_address = ((opcode == 0x984 && insn.words[2] == 0x08000800) ||
                (opcode == 0x988 && (insn.words[2] & ~0xffU) == 0x08000800)) &&
                insn.Bits(24, 8) == 255 && uniform_register(uniform_source) != ~0U;
            if (!uniform_address && (opcode == 0x988 || width > 6 || insn.words[2] != ((width << 9) | (stride << 14)) ||
                (opcode == 0x984 && insn.Bits(32, 8))))
                return fail(index, "native shared memory addressing mode");
            u32 address = uniform_address ? uniform_register(uniform_source) : insn.Bits(24, 8);
            if (stride && address != 255) {
                emit(0x3848000000000000ULL | predicate | 250 |
                    (u64{address} << 8) | (u64{stride + 1} << 20));
                address = 250;
            }
            u32 data = (opcode == 0x388 || opcode == 0x988) ? insn.Bits(32, 8) : static_cast<u32>(dest);
            const u32 count = width == 5 ? 2U : width == 6 ? 4U : 1U;
            if ((opcode == 0x388 || opcode == 0x988) && data == 255 && count > 1) {
                if (address == 250) {
                    emit(0x5c98000000000000ULL | predicate | 254 | (u64{250} << 20) | (u64{15} << 39));
                    address = 254;
                }
                for (u32 component = 0; component < count; ++component) mov_imm(250 + component, 0);
                data = 250;
            } else if ((data != 255 && data + count > reserved_register) || (data == 255 && count != 1)) {
                return fail(index, "native shared register vector overlaps uniform state");
            }
            emit(((opcode == 0x388 || opcode == 0x988) ? 0xef58000000000000ULL : 0xef48000000000000ULL) |
                predicate | data | (u64{address} << 8) | (u64{insn.Bits(40, 24)} << 20) |
                (u64{width} << 48));
            break;
        }
        case 0x919: case 0x9c3: { // S2R / S2UR thread and workgroup identities.
            const u32 special = insn.Bits(72, 8);
            if (insn.words[1] || insn.words[2] != (special << 8) ||
                (special != 0 && special != 33 && special != 34 && special != 35 &&
                 special != 37 && special != 38 && special != 39 &&
                 special != 56 && special != 57 && special != 58 && special != 59 && special != 60))
                return fail(index, "native special register");
            const u32 target = opcode == 0x9c3 ? uniform_register(static_cast<u32>(dest)) : static_cast<u32>(dest);
            if (target == ~0U) return fail(index, "native uniform special destination");
            emit(0xf0c8000000000000ULL | predicate | target | (u64{special} << 20));
            break;
        }
        case 0x28c: case 0x88c: { // UISETP with independent uniform predicate accumulation.
            const u32 output = insn.Bits(81, 3), condition = insn.Bits(87, 3);
            const u32 combine = insn.Bits(74, 2);
            const u32 a = uniform_register(insn.Bits(24, 8)), b = opcode == 0x88c ? 252U : uniform_register(insn.Bits(32, 8));
            if (output == 7 || insn.Bits(84, 3) != 7 || insn.Bits(72, 1) || combine > 2 ||
                native_predicate != 7 || a == ~0U || b == ~0U)
                return fail(index, "native uniform comparison mode");
            const u32 temporary = 6 - output, mask = 1U << temporary;
            if (opcode == 0x88c) mov_imm(252, insn.words[1]);
            emit(0x38e8000000070000ULL | 253 | (u64{255} << 8) | (u64{0x7f} << 20));
            emit(0x38f0000000070000ULL | (u64{239} << 8) | (u64{0x7f} << 20));
            emit(0x5b60000000070000ULL | 7 | (u64{temporary} << 3) |
                (u64{a} << 8) | (u64{b} << 20) | (u64{condition == 7 ? 7 : 6 - condition} << 39) |
                (u64{insn.Bits(90, 1)} << 42) | (u64{combine} << 45) |
                (u64{insn.Bits(73, 1)} << 48) | (u64{insn.Bits(76, 3)} << 49));
            emit(0x38e8000000070000ULL | 239 | (u64{239} << 8) | (u64{mask} << 20));
            emit(0x38f0000000070000ULL | (u64{253} << 8) | (u64{0x7f} << 20));
            break;
        }
        case 0x287: case 0x887: { // USEL; preserve ordinary predicates.
            const u32 condition = insn.Bits(87, 3);
            const u32 target = uniform_register(static_cast<u32>(dest));
            const u32 a = uniform_register(insn.Bits(24, 8)), b = opcode == 0x887 ? 252U : uniform_register(insn.Bits(32, 8));
            if (condition > 6 || target == ~0U || a == ~0U || b == ~0U ||
                insn.Bits(12, 4) != 7 || (insn.words[2] & ~0x07800000U) != 0x08000000U)
                return fail(index, "native uniform select mode");
            if (opcode == 0x887 && index > 0 && insn.words[1] <= 0x7ffff &&
                code[index - 1].Bits(0, 12) == 0x88c &&
                code[index - 1].Bits(24, 8) == insn.Bits(24, 8) &&
                code[index - 1].words[1] == insn.words[1] &&
                code[index - 1].words[2] == (0x0bf01070U | (condition << 17)) &&
                insn.words[2] == (0x08000000U | (condition << 23))) {
                // Preserve the unsigned clamp idiom for indirect-table analysis.
                emit(0x3820000000070000ULL | target | (u64{a} << 8) |
                     (u64{insn.words[1]} << 20) | (u64{7} << 39));
                break;
            }
            if (opcode == 0x887) mov_imm(252, insn.words[1]);
            const u32 temporary = 6 - condition, mask = 1U << temporary;
            emit(0x38e8000000070000ULL | 253 | (u64{255} << 8) | (u64{mask} << 20));
            emit(0x38f0000000070000ULL | (u64{239} << 8) | (u64{mask} << 20));
            emit(0x5ca0000000070000ULL | target | (u64{a} << 8) | (u64{b} << 20) |
                (u64{temporary} << 39) | (u64{insn.Bits(90, 1)} << 42));
            emit(0x38f0000000070000ULL | (u64{253} << 8) | (u64{mask} << 20));
            break;
        }
        case 0x381: case 0x981: { // LDG.E.32/64/128 with scalar or uniform address words.
            u32 address = insn.Bits(24, 8);
            const u32 size = insn.Bits(73, 3);
            u32 load_predicate = 0;
            if ((size != 2 && size != 4 && size != 5 && size != 6) || dest > 247 ||
                (size == 6 && ((dest & 3) || dest > 244)) ||
                (size == 5 && ((dest & 1) || dest > 246)))
                return fail(index, "native global vector load mode");
            if (opcode == 0x381) {
                if ((insn.words[2] & ~(0xe00U | 0x2000U)) != 0x001e8100 || insn.Bits(32, 8) ||
                    (address & 1) || address > 246)
                    return fail(index, "native global vector load mode");
            } else {
                const u32 uniform_address = uniform_register(insn.Bits(32, 6));
                const bool descriptor_address = (insn.words[2] & ~0xe00U) == 0x0c1e1100;
                // [merge-ldg-pred] Bits 64-67 are a reversed predicate operand (index 7 - bits 64-66,
                // negate in bit 67): when it is false the load returns zero. The default (0) is PT.
                load_predicate = descriptor_address ? 0U : insn.Bits(64, 4);
                if ((!descriptor_address && (insn.words[2] & ~(0xe00U | 0x04000000U | 0x2000U | 0xfU)) != 0x081e8100) ||
                    (load_predicate && (native_predicate != 7 || (load_predicate & 7) == 0)) ||
                    insn.Bits(38, 2) || uniform_address == ~0U || (uniform_address & 1))
                    return fail(index, "native global load uniform addressing mode");
                if (descriptor_address) {
                    if ((address & 1) || address + 2 > reserved_register)
                        return fail(index, "native descriptor load address pair");
                } else if (address == 255) address = uniform_address;
                else {
                    if (insn.Bits(90, 1) && ((address & 1) || address > 246))
                        return fail(index, "native global load register address pair");
                    emit(0x5c10000000000000ULL | predicate | 250 | (u64{uniform_address} << 8) |
                         (u64{address} << 20) | (u64{1} << 47));
                    emit(0x5c10000000000000ULL | predicate | 251 | (u64{uniform_address + 1} << 8) |
                         (u64{insn.Bits(90, 1) ? address + 1 : 255} << 20) | (u64{1} << 43));
                    address = 250;
                }
            }
            if (load_predicate) {
                // Zero the destination, then load under the load's own predicate.
                for (u32 i = 0; i < (size == 6 ? 4U : size == 5 ? 2U : 1U); ++i)
                    emit(0x5c98000000000000ULL | predicate | (dest + i) | (u64{255} << 20) | (u64{15} << 39));
                predicate = u64{(7 - (load_predicate & 7)) | ((load_predicate >> 3) << 3)} << 16;
            }
            const bool strong = insn.Bits(77, 4) == 5;
            // Conservatively order CTA-strong loads at device scope on every stage.
            if (strong) emit(0xef98000000000000ULL | predicate | (u64{1} << 8));
            emit(base(0xeed0000000000000ULL) | (u64{address} << 8) |
                 (u64{insn.Bits(40, 24)} << 20) | (u64{1} << 45) | (u64{size} << 48));
            if (strong) emit(0xef98000000000000ULL | predicate | (u64{1} << 8));
            break;
        }
        case 0x386: case 0x986: { // Global stores retain consecutive words and their 64-bit address.
            const u32 data = insn.Bits(32, 8);
            u32 address = insn.Bits(24, 8);
            bool extended = insn.Bits(72, 1) != 0;
            const u32 width = insn.Bits(73, 3);
            const bool sized_store = opcode == 0x986 &&
                (insn.words[2] & ~(0x3fU | 0xe00U)) == 0x0c101100 &&
                (width == 0 || width == 2 || width == 4 || width == 5);
            if (!sized_store && (width < 4 || width > 6 || (data == 255 ? width != 4 :
                ((data & ((1U << (width - 4)) - 1)) || data + (1U << (width - 4)) > reserved_register))))
                return fail(index, "native global store width/register range");
            if (sized_store && width == 5 && ((data & 1) || data + 2 > reserved_register))
                return fail(index, "native global store data pair");
            if (opcode == 0x986) {
                const u32 uniform_address = uniform_register(insn.Bits(64, 6));
                if ((!sized_store && (insn.words[2] & ~(0x04000000U | 0x3fU)) != (0x0810a100U | (width << 9))) ||
                    uniform_address == ~0U || (uniform_address & 1) || !extended)
                    return fail(index, "native global store uniform addressing mode");
                if (sized_store) {
                    if ((address & 1) || address + 2 > reserved_register)
                        return fail(index, "native descriptor store address pair");
                } else if (address == 255) address = uniform_address;
                else {
                    if (insn.Bits(90, 1) && ((address & 1) || address >= 254))
                        return fail(index, "native global store register address pair");
                    emit(0x5c10000000000000ULL | predicate | 250 | (u64{uniform_address} << 8) |
                        (u64{address} << 20) | (u64{1} << 47));
                    emit(0x5c10000000000000ULL | predicate | 251 | (u64{uniform_address + 1} << 8) |
                        (u64{insn.Bits(90, 1) ? address + 1 : 255} << 20) | (u64{1} << 43));
                    address = 250;
                }
            } else if (insn.words[2] != (0x0010a100U | (width << 9))) {
                return fail(index, "native global store register addressing mode");
            }
            if (sized_store) {
            emit(0xeed8000000000000ULL | predicate | data |
                (u64{address} << 8) | (u64{insn.Bits(40, 24)} << 20) |
                (u64{extended} << 45) | (u64{sized_store ? width : 4U} << 48));
            } else {
            const s32 displacement = static_cast<s32>(insn.Bits(40, 24) << 8) >> 8;
            const u32 words = 1U << (width - 4);
            if (displacement > 0x7fffff - static_cast<s32>((words - 1) * 4))
                return fail(index, "native global store displacement range");
            for (u32 word = 0; word < words; ++word)
                emit(0xeed8000000000000ULL | predicate | (data + word) |
                     (u64{address} << 8) | (u64{static_cast<u32>(displacement + word * 4) & 0xffffffU} << 20) |
                     (u64{extended} << 45) | (u64{4} << 48));
            }
            break;
        }
        case 0x95b: // Fragment discard; execution predicate is preserved.
            if (insn.words[1] || insn.words[2] != 0x03800000)
                return fail(index, "native fragment discard mode");
            emit(0xe33000000000000fULL | predicate);
            break;
        case 0x311: { // F2I.FTZ.U64.TRUNC, scalar register input and paired output
            if (insn.words[2] != 0x0021d800 || (insn.words[1] & ~0xffU) ||
                insn.Bits(24, 8) || (dest & 1) || dest >= 254)
                return fail(index, "native unsigned 64-bit float conversion mode");
            emit(base(0x5cb0000000000000ULL) | (u64{3} << 8) | (u64{2} << 10) |
                 (u64{insn.Bits(32, 8)} << 20) | (u64{3} << 39) | (u64{1} << 44));
            break;
        }
        case 0x305: case 0xb05: { // F2I NTZ, register/constant-buffer source
            const bool half = opcode == 0x305 && insn.words[2] == 0x0010f000;
            const bool small = insn.words[2] == 0x00212800;
            const bool floor = (insn.words[2] & ~0x10100U) == 0x00207000;
            const bool nearest = (insn.words[2] & ~0x10100U) == 0x00203000;
            if ((!half && !small && !floor && !nearest && (insn.words[2] & ~0x10100U) != 0x0020f000) || (opcode == 0x305 && (insn.words[1] & 0xffffff00U))) {
                return fail(index, "native float-to-integer conversion mode");
            }
            const bool is_signed = insn.Bits(72, 1);
            u32 source = opcode == 0xb05 ? materialize(2, 0, 252, insn.Bits(62, 1) != 0 && !LowerGroupDisabled("audit_f2i"), insn.Bits(63, 1) != 0 && !LowerGroupDisabled("audit_f2i")) // [merge-audit-f2i]
                                     : insn.Bits(32, 8);
            if (half) {
                extract_half({source, 2, false, false}, 0, 252);
                source = 252;
            }
            if (!is_signed) {
                // Clear NaNs without consuming a guest predicate. Clamp the
                // conversion below 2^32, retaining an overflow mask for saturation.
                emit(0x5800000000000000ULL | predicate | 250 | (u64{source} << 8) |
                     (u64{source} << 20) | (u64{7} << 39) | (u64{7} << 48));
                emit(0x5c40000000000000ULL | predicate | 250 | (u64{source} << 8) |
                     (u64{250} << 20) | (u64{7} << 48));
                if (!small) {
                mov_imm(252, 0x4f800000);
                emit(0x5800000000000000ULL | predicate | 251 | (u64{250} << 8) |
                     (u64{252} << 20) | (u64{7} << 39) | (u64{6} << 48));
                mov_imm(252, 0x4f7fffff);
                emit(0x5c60000000000000ULL | predicate | 250 | (u64{250} << 8) |
                     (u64{252} << 20) | (u64{7} << 39));
                }
                source = 250;
            }
            emit(base(0x5cb0000000000000ULL) | (u64{small ? 1U : 2U} << 8) | (u64{2} << 10) |
                 (u64{is_signed} << 12) | (u64{source} << 20) |
                 (u64{small || nearest ? 0U : floor ? 1U : 3U} << 39) | (u64{insn.Bits(80, 1)} << 44));
            if (!is_signed && !small) {
                emit(base(0x5c40000000000000ULL) | (dest << 8) | (u64{251} << 20) |
                     (u64{1} << 41) | (u64{7} << 48));
            }
            break;
        }
        case 0x370: { // Bindless TXQ dimensions from a handle/mip register pair.
            const u32 mask = insn.Bits(72, 4), source = insn.Bits(24, 8);
            if (insn.words[1] != 0x08000000 || insn.Bits(64, 8) != 255 ||
                (mask != 1 && mask != 3 && mask != 8) ||
                (insn.words[2] & ~0xf00U) != 255 || source > 253 || dest >= 254)
                return fail(index, "native bindless texture dimension query mode");
            emit(0xdf50000000000000ULL | predicate | dest | (u64{source} << 8) |
                 (u64{1} << 22) | (u64{mask} << 31) | (u64{insn.Bits(90, 1)} << 49));
            break;
        }
        case 0xb6f: { // TXQ dimensions or mip count.
            const u32 mask = insn.Bits(72, 4);
            if (insn.Bits(59, 5) || insn.Bits(64, 8) != 255 || (mask != 3 && mask != 8) ||
                insn.Bits(76, 14) || insn.Bits(91, 1) || insn.Bits(54, 5) >= 18 ||
                insn.Bits(40, 14) > 0x1fff ||
                (mask == 3 ? ((dest & 1) || dest >= 254) : dest >= 255))
                return fail(index, "native texture dimension query mode");
            const u32 binding = insn.Bits(54, 5);
            if (result.required_texture_buffer != ~0U && result.required_texture_buffer != binding)
                return fail(index, "native texture descriptor buffer conflict");
            result.required_texture_buffer = binding;
            emit(0xdf48000000000000ULL | predicate | dest | (u64{insn.Bits(24, 8)} << 8) |
                 (u64{1} << 22) | (u64{mask} << 31) | (u64{insn.Bits(40, 14)} << 36) |
                 (u64{insn.Bits(90, 1)} << 49));
            break;
        }
        case 0xb69: { // TMML.LOD 2D, one selected fixed-point LOD result.
            const u32 binding = insn.Bits(54, 5), offset = insn.Bits(40, 14);
            const u32 coordinates = insn.Bits(24, 8), mask = insn.Bits(72, 4);
            if (insn.Bits(32, 8) != 255 || insn.Bits(59, 2) ||
                insn.Bits(61, 3) != 1 || insn.Bits(64, 8) != 255 ||
                (mask != 1 && mask != 2) || insn.Bits(76, 16) ||
                binding >= 18 || offset > 0x1fff || dest >= reserved_register ||
                coordinates + 1 >= reserved_register)
                return fail(index, "native texture mipmap level query mode");
            if (result.required_texture_buffer != ~0U && result.required_texture_buffer != binding)
                return fail(index, "native texture descriptor buffer conflict");
            result.required_texture_buffer = binding;
            emit(base(0xdf58000000000000ULL) | (u64{coordinates} << 8) |
                 (u64{2} << 28) | (u64{mask} << 31) | (u64{offset} << 36));
            break;
        }
        case 0xb63: { // TLD4.SCR.2D, selected gathered values packed across two outputs.
            const u32 binding = insn.Bits(54, 5), offset = insn.Bits(40, 14);
            const u32 second = insn.Bits(64, 8), mask = insn.Bits(72, 4);
            const u32 count = static_cast<u32>(std::popcount(mask));
            if (insn.Bits(61, 3) == 5 && !insn.Bits(59, 1) && insn.Bits(60, 1) && mask && !insn.Bits(76, 5) && // [merge-tld4-array]
                insn.Bits(81, 3) == 7 && !insn.Bits(89, 1) && !insn.Bits(91, 1) &&
                binding < 18 && offset <= 0x1fff &&
                (second != 255 ? count >= 3 : count <= 2) && dest + 1 < reserved_register &&
                (count == 1 || !(dest & 1)) &&
                (second == 255 || ((count < 4 || !(second & 1)) && second + 1 < reserved_register)) &&
                insn.Bits(24, 8) + 1 < reserved_register && insn.Bits(32, 8) < reserved_register) {
                // Array gather: coordinates are packed from R250 as {array, x, y}; the components
                // come back packed from R250 in mask order.
                if (result.required_texture_buffer != ~0U && result.required_texture_buffer != binding)
                    return fail(index, "native texture descriptor buffer conflict");
                result.required_texture_buffer = binding;
                const auto copy = [&](u32 target, u32 source) {
                    emit(0x5c98000000000000ULL | predicate | target | (u64{15} << 39) | (u64{source} << 20));
                };
                copy(250, insn.Bits(24, 8));
                copy(251, insn.Bits(24, 8) + 1);
                copy(252, insn.Bits(32, 8));
                emit(0xc800000000000000ULL | (u64{insn.Bits(87, 2)} << 56) | predicate | 250 |
                     (u64{250} << 8) | (u64{3} << 28) | (u64{mask} << 31) | (u64{offset} << 36) |
                     (u64{insn.Bits(90, 1)} << 49) | (u64{7} << 51));
                for (u32 component = 0; component < count; ++component)
                    copy(component < 2 ? static_cast<u32>(dest) + component : second + component - 2,
                         250 + component);
                break;
            }
            if (insn.Bits(59, 1) || !insn.Bits(60, 1) || insn.Bits(61, 3) != 1 ||
                !mask || insn.Bits(76, 5) || insn.Bits(81, 3) != 7 ||
                insn.Bits(89, 1) || insn.Bits(91, 1) || binding >= 18 || offset > 0x1fff ||
                (insn.Bits(24, 8) >= reserved_register && insn.Bits(24, 8) != 255) ||
                (insn.Bits(32, 8) >= reserved_register && insn.Bits(32, 8) != 255) ||
                dest >= reserved_register || (count > 1 && ((dest & 1) || dest + 1 >= reserved_register)) ||
                (count > 2 && second >= reserved_register) ||
                (count == 4 && ((second & 1) || second + 1 >= reserved_register)))
                return fail(index, "native texture gather mode");
            if (result.required_texture_buffer != ~0U && result.required_texture_buffer != binding)
                return fail(index, "native texture descriptor buffer conflict");
            result.required_texture_buffer = binding;
            emit(0xdf00000000000000ULL | predicate | (mask == 15 ? dest : 250) |
                 (u64{insn.Bits(24, 8)} << 8) | (u64{insn.Bits(32, 8)} << 20) |
                 (u64{mask == 15 ? second : 252} << 28) | (u64{offset} << 36) |
                 (u64{insn.Bits(90, 1)} << 49) | (u64{insn.Bits(87, 2)} << 52));
            if (mask != 15) {
                u32 packed{};
                for (u32 component = 0; component < 4; ++component) {
                    if (!(mask & (1U << component))) continue;
                    const u32 target = packed < 2 ? static_cast<u32>(dest) + packed : second + packed - 2;
                    emit(0x5c98000000000000ULL | predicate | target | (u64{15} << 39) |
                         (u64{250 + component} << 20));
                    ++packed;
                }
            }
            break;
        }
        case 0xb66: { // TLD.SCR.LZ, direct texel fetch
            const u32 mask = insn.Bits(72, 4);
            const u32 dimension = insn.Bits(61, 3);
            const bool array = dimension == 5;
            const bool volume = dimension == 2;
            u32 source_a = insn.Bits(24, 8), source_b = insn.Bits(32, 8);
            const u32 second_dest = insn.Bits(64, 8);
            const bool split = second_dest != 255;
            constexpr std::array<u32, 8> rg_masks{1, 2, 4, 8, 3, 9, 10, 12};
            constexpr std::array<u32, 5> rgba_masks{7, 11, 13, 14, 15};
            u32 swizzle = ~0U;
            if (split) {
                for (u32 i = 0; i < rgba_masks.size(); ++i) if (rgba_masks[i] == mask) swizzle = i;
            } else {
                for (u32 i = 0; i < rg_masks.size(); ++i) if (rg_masks[i] == mask) swizzle = i;
            }
            if (!insn.Bits(60, 1) || (dimension != 0 && dimension != 1 && !array && !volume) ||
                swizzle == ~0U || insn.Bits(76, 5) ||
                insn.Bits(81, 3) != 7 || insn.Bits(87, 3) != 1 ||
                insn.Bits(59, 1) ||
                insn.Bits(40, 14) > 0x1fff || insn.Bits(54, 5) >= 18 ||
                dest == 255 || (mask & (mask - 1) && ((dest & 1) || dest >= 254)) ||
                (split && mask == 15 && ((second_dest & 1) || second_dest >= 254)) ||
                (array && ((source_a & 1) || source_a >= 249)) ||
                (volume && ((source_a & 1) || source_a >= 254))) {
                return fail(index, "native texel fetch mode or descriptor binding");
            }
            const u32 binding = insn.Bits(54, 5);
            if (result.required_texture_buffer != ~0U && result.required_texture_buffer != binding) {
                const auto copy = [&](u32 target, u32 source) {
                    emit(0x5c98000000000000ULL | predicate | target | (u64{15} << 39) | (u64{source} << 20));
                };
                copy(250, source_a);
                if (array || volume) copy(251, source_a + 1);
                if (dimension != 0) copy(array || volume ? 252 : 251, source_b);
                emit(0x4c98000000000000ULL | predicate | 253 | (u64{15} << 39) |
                     (u64{insn.Bits(40, 14)} << 20) | (u64{binding} << 34));
                emit(0xdd00000000000000ULL | predicate | 250 | (u64{250} << 8) |
                     (u64{253} << 20) | (u64{array ? 3U : volume ? 4U : dimension == 0 ? 0U : 2U} << 28) |
                     (u64{mask} << 31) | (u64{7} << 51) | (u64{insn.Bits(90, 1)} << 49));
                const u32 count = std::popcount(mask);
                for (u32 component = 0; component < count; ++component)
                    copy(component < 2 ? static_cast<u32>(dest) + component : second_dest + component - 2,
                         250 + component);
                break;
            }
            result.required_texture_buffer = binding;
            if (array) {
                emit(0x5c98000000000000ULL | predicate | 250 | (u64{15} << 39) | (u64{source_a + 1} << 20));
                emit(0x5c98000000000000ULL | predicate | 251 | (u64{15} << 39) | (u64{source_b} << 20));
                source_b = 250;
            }
            emit(0xda00000000000000ULL | predicate | dest | (u64{source_a} << 8) |
                 (u64{source_b} << 20) | (u64{insn.Bits(64, 8)} << 28) |
                 (u64{insn.Bits(40, 14)} << 36) | (u64{insn.Bits(90, 1)} << 49) |
                 (u64{swizzle} << 50) | (u64{array ? 8U : volume ? 7U : dimension == 0 ? 0U : 2U} << 53));
            break;
        }
        case 0xfae: { // LDGSTS.E.BYPASS.128, completed synchronously with zero fill.
            const u32 global_address = insn.Bits(24, 8), shared_address = insn.Bits(16, 8);
            const u32 descriptor = insn.Bits(64, 6);
            if (native_predicate != 7 || (global_address & 1) || global_address + 2 > reserved_register ||
                descriptor >= 63 || (descriptor & 1) || (insn.words[1] & 0xf000U) ||
                (insn.words[2] & ~(0x03800000U | 0x7fU)) != 0x0c101c00)
                return fail(index, "native global-to-shared copy mode");
            const u32 enable = insn.Bits(87, 3) | (insn.Bits(70, 1) << 3);
            const u32 global_immediate = insn.words[1] & 0xfff;
            const s32 global_offset = static_cast<s32>(global_immediate) - (global_immediate & 0x800 ? 0x1000 : 0);
            const s32 shared_offset = static_cast<s32>(static_cast<s16>(insn.words[1] >> 16)) * 16;
            for (u32 component = 0; component < 4; ++component) mov_imm(240 + component, 0);
            // The uniform selector names the memory descriptor; the .64 register pair is the address.
            if (enable != 15) for (u32 component = 0; component < 4; ++component) {
                emit(0xeed0000000000000ULL | (u64{enable} << 16) | (240 + component) |
                     (u64{global_address} << 8) |
                     (u64{static_cast<u32>(global_offset + component * 4) & 0xffffffU} << 20) |
                     (u64{1} << 45) | (u64{4} << 48));
            }
            for (u32 component = 0; component < 4; ++component) {
                emit(0xef58000000000000ULL | predicate | (240 + component) |
                     (u64{shared_address} << 8) |
                     (u64{static_cast<u32>(shared_offset + component * 4) & 0xffffffU} << 20) |
                     (u64{4} << 48));
            }
            emit(0xef98000000000000ULL | predicate);
            break;
        }
        case 0x9af: // Commit an already completed global-to-shared copy group.
            if (insn.words[0] != 0x79af || insn.words[1] || insn.words[2])
                return fail(index, "native copy-group commit mode");
            emit(0xef98000000000000ULL | predicate);
            break;
        case 0x91a: { // Native scoreboard waits; synchronous copies have no outstanding groups.
            const u32 scoreboard = insn.words[1] & ~0xfc0U;
            if (insn.words[0] != 0x791a ||
                (scoreboard != 0xd000 && insn.words[1] != 0x800e && !(uses_async_copy && scoreboard == 0x8000)) || insn.words[2]) {
                return fail(index, "native dependency barrier mode");
            }
            if (scoreboard == 0x8000) emit(0xef98000000000000ULL | predicate);
            break;
        }
        case 0x361: { // Scalar bindless cube sample: XY, then Z and descriptor handle.
            const u32 a = insn.Bits(24, 8), b = insn.Bits(32, 8);
            if ((insn.words[1] & ~0xffU) == 0x38000000) {
                const bool offset = insn.Bits(76, 1);
                if ((insn.words[2] & ~(0x04000000U | 0x1000U)) != 0x001e01ff ||
                    (a & 1) || a > 248 || b > (offset ? 252U : 253U) || dest > 247)
                    return fail(index, "native bindless 2D sample mode");
                emit(0xde80000000000000ULL | predicate | dest | (u64{a} << 8) |
                     (u64{b} << 20) | (u64{2} << 28) | (u64{1} << 31) |
                     (u64{offset} << 36) | (u64{7} << 51) | (u64{insn.Bits(90, 1)} << 49));
                break;
            }
            const bool scalar = insn.Bits(60, 1);
            const u32 mask = insn.Bits(72, 4), lod = insn.Bits(87, 3);
            const u32 second = insn.Bits(64, 8), count = std::popcount(mask);
            const u32 limit = reserved_register;
            if ((insn.words[1] & ~0xffU) != (scalar ? 0x78000000U : 0x68000000U) ||
                (insn.words[2] & ~(0x07800fffU)) != 0x001e0000 ||
                (mask != 1 && mask != 3 && mask != 7 && mask != 15) ||
                (lod != 0 && lod != 1 && lod != 3) || (scalar && lod == 3) ||
                a + (scalar ? 1 : 2) >= limit || b + (scalar || lod == 3 ? 1 : 0) >= limit ||
                (scalar && (a & 1)) || dest >= limit ||
                (count > 1 && ((dest & 1) || dest + 1 >= limit)) ||
                (count <= 2 ? second != 255 : second >= limit) ||
                (count == 4 && ((second & 1) || second + 1 >= limit)))
                return fail(index, "native bindless cube sample mode");
            const auto copy = [&](u32 target, u32 source) {
                emit(0x5c98000000000000ULL | predicate | target | (u64{15} << 39) | (u64{source} << 20));
            };
            copy(252, a); copy(253, a + 1); copy(254, scalar ? b : a + 2);
            copy(250, scalar ? b + 1 : b);
            if (lod == 3) copy(251, b + 1);
            emit(0xde80000000000000ULL | predicate | 250 | (u64{252} << 8) |
                 (u64{250} << 20) | (u64{6} << 28) | (u64{mask} << 31) | (u64{lod} << 37) |
                 (u64{7} << 51) | (u64{insn.Bits(90, 1)} << 49));
            for (u32 component = 0; component < count; ++component)
                copy(component < 2 ? static_cast<u32>(dest) + component : second + component - 2,
                     250 + component);
            break;
        }
        case 0xf60: { // Scalar 2D sample with a uniform bindless descriptor handle.
            const u32 a = insn.Bits(24, 8), b = insn.Bits(32, 8);
            const u32 handle = uniform_register(insn.Bits(40, 6));
            const u32 mask = insn.Bits(72, 4), count = std::popcount(mask), second = insn.Bits(64, 8);
            const u32 sample_lod = insn.Bits(87, 3); // [merge-uniform-tex] explicit zero-LOD form
            if ((insn.words[1] & ~0x3fffU) != 0x30000000 || sample_lod > 1 ||
                (insn.words[2] & ~(0x04000000U | 0x00800000U | 0xfffU)) != 0x081e0000 || !mask ||
                handle == ~0U || a >= reserved_register || b >= reserved_register || dest >= reserved_register ||
                (count > 1 && ((dest & 1) || dest + 1 >= reserved_register)) ||
                (count <= 2 ? second != 255 : second >= reserved_register) ||
                (count == 4 && ((second & 1) || second + 1 >= reserved_register)))
                return fail(index, "native uniform-handle texture sample mode");
            const auto copy = [&](u32 target, u32 source) {
                emit(0x5c98000000000000ULL | predicate | target | (u64{15} << 39) | (u64{source} << 20));
            };
            copy(250, a); copy(251, b);
            emit(0xde80000000000000ULL | predicate | 250 | (u64{250} << 8) |
                 (u64{handle} << 20) | (u64{2} << 28) | (u64{mask} << 31) | (u64{sample_lod} << 37) |
                 (u64{7} << 51) | (u64{insn.Bits(90, 1)} << 49));
            for (u32 component = 0; component < count; ++component)
                copy(component < 2 ? static_cast<u32>(dest) + component : second + component - 2,
                     250 + component);
            break;
        }
        case 0xf63: { // [merge-uniform-tex] TLD4.SCR with a packed texture handle in a uniform register.
            const u32 source_a = insn.Bits(24, 8), source_b = insn.Bits(32, 8);
            const u32 uniform_handle = insn.Bits(40, 6), second_dest = insn.Bits(64, 8);
            const u32 mask = insn.Bits(72, 4), count = std::popcount(mask);
            const bool split = second_dest != 255;
            if (!insn.Bits(91, 1) || insn.Bits(46, 14) != 0 || !insn.Bits(60, 1) ||
                insn.Bits(61, 3) != 1 || mask == 0 || insn.Bits(76, 5) != 0 || insn.Bits(81, 3) != 7 ||
                insn.Bits(84, 3) != 1 || insn.Bits(89, 2) != 0 || insn.Bits(87, 2) > 3 ||
                (split ? count < 3 : count > 2) || dest + 1 >= reserved_register ||
                (count > 1 && (dest & 1)) ||
                (split && ((count == 4 && (second_dest & 1)) || second_dest + 1 >= reserved_register)) ||
                source_a >= reserved_register || source_b >= reserved_register ||
                (uniform_handle & 1) || uniform_handle >= 15) {
                return fail(index, "native uniform gather mode");
            }
            const u32 handle = uniform_register(uniform_handle);
            u32 coordinates = source_a;
            if (source_b != source_a + 1) {
                emit(0x5c98000000000000ULL | predicate | 250 | (u64{source_a} << 20) | (u64{15} << 39));
                emit(0x5c98000000000000ULL | predicate | 251 | (u64{source_b} << 20) | (u64{15} << 39));
                coordinates = 250;
            }
            // TLD4 (b): handle in the meta register, component in bits 38-39.
            emit(0xdec0000000000000ULL | predicate | 250 | (u64{coordinates} << 8) |
                 (u64{handle} << 20) | (u64{2} << 28) | (u64{mask} << 31) |
                 (u64{insn.Bits(87, 2)} << 38) | (u64{insn.Bits(90, 1)} << 49) | (u64{7} << 51));
            for (u32 component = 0; component < count; ++component)
                emit(0x5c98000000000000ULL | predicate |
                     (component < 2 ? static_cast<u32>(dest) + component : second_dest + component - 2) |
                     (u64{250 + component} << 20) | (u64{15} << 39));
            break;
        }
        case 0xf66: { // [merge-uniform-tex] TLD.SCR.LZ with a packed texture handle in a uniform-register pair.
            const u32 source_a = insn.Bits(24, 8);
            const u32 source_b = insn.Bits(32, 8);
            const u32 uniform_handle = insn.Bits(40, 6);
            const u32 second_dest = insn.Bits(64, 8);
            const u32 fetch_mask = insn.Bits(72, 4);
            const bool split_fetch = second_dest != 255;
            const bool compact_fetch = !(fetch_mask == 7 && source_b == source_a + 1 && (dest & 1) == 0 &&
                                         dest + 1 < reserved_register && second_dest < reserved_register &&
                                         source_a + 1 < reserved_register);
            if (!insn.Bits(91, 1) || insn.Bits(46, 14) != 0 ||
                !insn.Bits(60, 1) || insn.Bits(61, 3) != 1 || insn.Bits(76, 5) != 0 ||
                insn.Bits(81, 3) != 7 || insn.Bits(84, 3) != 1 ||
                insn.Bits(87, 3) != 1 || insn.Bits(90, 1) != 0 ||
                (uniform_handle & 1) || uniform_handle >= 15) {
                return fail(index, "native uniform texel fetch mode");
            }
            const u32 handle = uniform_register(uniform_handle);
            if (compact_fetch) {
                // Scalar coordinates in independent registers, results packed from a scratch base.
                if (!(split_fetch ? (fetch_mask == 7 || fetch_mask == 15) : (fetch_mask == 1 || fetch_mask == 3)) ||
                    dest == 255 || dest + 1 >= reserved_register || (split_fetch && second_dest + 1 >= reserved_register) ||
                    source_a >= reserved_register || source_b >= reserved_register)
                    return fail(index, "native uniform texel fetch mode");
                const auto move = [&](u32 target, u32 source) {
                    emit(0x5c98000000000000ULL | predicate | target | (u64{15} << 39) | (u64{source} << 20));
                };
                move(250, source_a);
                move(251, source_b);
                emit(0xdd00000000000000ULL | predicate | 250 | (u64{250} << 8) |
                     (u64{handle} << 20) | (u64{2} << 28) | (u64{fetch_mask} << 31) | (u64{7} << 51));
                move(static_cast<u32>(dest), 250);
                if (fetch_mask != 1) move(static_cast<u32>(dest) + 1, 251);
                if (split_fetch) {
                    move(second_dest, 252);
                    if (fetch_mask == 15) move(second_dest + 1, 253);
                }
                break;
            }
            if ((dest & 1) || dest + 1 >= reserved_register || second_dest >= reserved_register ||
                source_a + 1 >= reserved_register)
                return fail(index, "native uniform texel fetch mode");
            emit(0xdd00000000000000ULL | predicate | 250 | (u64{source_a} << 8) |
                 (u64{handle} << 20) | (u64{2} << 28) | (u64{7} << 31) |
                 (u64{7} << 51));
            emit(0x5c98000000000000ULL | predicate | static_cast<u32>(dest) | (u64{250} << 20) |
                 (u64{15} << 39));
            emit(0x5c98000000000000ULL | predicate | (static_cast<u32>(dest) + 1) | (u64{251} << 20) |
                 (u64{15} << 39));
            emit(0x5c98000000000000ULL | predicate | second_dest | (u64{252} << 20) |
                 (u64{15} << 39));
            break;
        }
        case 0xb60: { // TEX, scalar 2D implicit-LOD sample
            const bool half = insn.Bits(79, 1);
            const u32 mask = insn.Bits(72, 4);
            const u32 second_dest = insn.Bits(64, 8);
            constexpr std::array<u32, 8> rg_masks{1, 2, 4, 8, 3, 9, 10, 12};
            constexpr std::array<u32, 5> rgba_masks{7, 11, 13, 14, 15};
            u32 swizzle = ~0U;
            const bool split = second_dest != 255;
            const bool depth_compare = insn.Bits(78, 1);
            const u32 dimension = insn.Bits(61, 3), lod = insn.Bits(87, 3);
            const u32 source_a = insn.Bits(24, 8), source_b = insn.Bits(32, 8);
            if (half && ((dimension != 1 && dimension != 5) || depth_compare || insn.Bits(76, 2)))
                return fail(index, "native half texture sample mode");
            if (dimension == 1 && lod == 3 && insn.Bits(76, 2) == 1) {
                const u32 count = static_cast<u32>(std::popcount(mask));
                if (!insn.Bits(60, 1) || depth_compare || !count ||
                    insn.Bits(81, 3) != 7 || insn.Bits(59, 1) ||
                    insn.Bits(40, 14) > 0x1fff || insn.Bits(54, 5) >= 18 ||
                    (source_a & 1) || source_a > 248 || (source_b & 1) || source_b > 248 ||
                    dest >= 250 || (count > 1 && (dest & 1)) || (count <= 2 && split) ||
                    (count > 2 && (second_dest >= 250 || (count == 4 && (second_dest & 1)))))
                    return fail(index, "native explicit-LOD offset sample mode");
                const u32 binding = insn.Bits(54, 5);
                if (result.required_texture_buffer != ~0U && result.required_texture_buffer != binding)
                    return fail(index, "native texture descriptor buffer conflict");
                result.required_texture_buffer = binding;
                const auto copy = [&](u32 target, u32 source) {
                    emit(0x5c98000000000000ULL | predicate | target |
                         (u64{15} << 39) | (u64{source} << 20));
                };
                // Coordinates and metadata are vector pairs. Snapshot them
                // before packing the selected result channels over any aliases.
                copy(250, source_a); copy(251, source_a + 1);
                copy(252, source_b); copy(253, source_b + 1);
                emit(0xc000000000000000ULL | predicate | 250 | (u64{250} << 8) |
                     (u64{252} << 20) | (u64{2} << 28) | (u64{mask} << 31) |
                     (u64{insn.Bits(40, 14)} << 36) | (u64{insn.Bits(90, 1)} << 49) |
                     (u64{7} << 51) | (u64{1} << 54) | (u64{3} << 55));
                for (u32 component = 0; component < count; ++component)
                    copy(component < 2 ? static_cast<u32>(dest) + component : second_dest + component - 2,
                         250 + component);
                break;
            }
            if (dimension == 7 && !depth_compare &&
                ((insn.Bits(60, 1) && lod == 0) || (!insn.Bits(60, 1) && lod == 3))) {
                const bool scalar = insn.Bits(60, 1);
                if (!mask || insn.Bits(76, 2) || insn.Bits(81, 3) != 7 ||
                    insn.Bits(59, 1) || insn.Bits(40, 14) > 0x1fff || insn.Bits(54, 5) >= 18 ||
                    (source_a & (scalar ? 1 : 3)) || (scalar && (source_b & 1)) ||
                    source_a >= (scalar ? 248U : 245U) || source_b >= 248 ||
                    dest >= 248 || (std::popcount(mask) > 1 && (dest & 1)) ||
                    (std::popcount(mask) > 2 && (!split || second_dest >= 248)) ||
                    (std::popcount(mask) > 3 && (second_dest & 1)))
                    return fail(index, "native scalar cube array sample mode");
                const u32 binding = insn.Bits(54, 5);
                if (result.required_texture_buffer != ~0U && result.required_texture_buffer != binding)
                    return fail(index, "native texture descriptor buffer conflict");
                result.required_texture_buffer = binding;
                const auto copy = [&](u32 target, u32 source) {
                    emit(0x5c98000000000000ULL | predicate | target | (u64{15} << 39) | (u64{source} << 20));
                };
                copy(250, source_a);
                copy(251, source_a + 1);
                copy(252, scalar ? source_b : source_a + 2);
                copy(253, scalar ? source_b + 1 : source_a + 3);
                emit(0xc000000000000000ULL | predicate | 250 | (u64{250} << 8) |
                     (u64{scalar ? 255U : source_b} << 20) | (u64{7} << 28) | (u64{mask} << 31) |
                     (u64{insn.Bits(40, 14)} << 36) | (u64{insn.Bits(90, 1)} << 49) |
                     (u64{7} << 51) | (u64{lod} << 55));
                for (u32 component = 0; component < static_cast<u32>(std::popcount(mask)); ++component)
                    copy(component < 2 ? static_cast<u32>(dest) + component : second_dest + component - 2,
                         250 + component);
                break;
            }
            if (dimension == 7 && depth_compare && lod == 0 && !split && mask == 1) {
                if (insn.Bits(60, 1) || insn.Bits(76, 2) || insn.Bits(81, 3) != 7 ||
                    insn.Bits(59, 1) || insn.Bits(40, 14) > 0x1fff || insn.Bits(54, 5) >= 18 ||
                    (source_a & 3) || source_a > 244 || source_b >= 248 || dest >= 248)
                    return fail(index, "native cube array depth comparison sample mode");
                const u32 binding = insn.Bits(54, 5);
                if (result.required_texture_buffer != ~0U && result.required_texture_buffer != binding)
                    return fail(index, "native texture descriptor buffer conflict");
                result.required_texture_buffer = binding;
                emit(0xc000000000000000ULL | predicate | dest | (u64{source_a} << 8) |
                     (u64{source_b} << 20) | (u64{7} << 28) | (u64{1} << 31) |
                     (u64{insn.Bits(40, 14)} << 36) | (u64{insn.Bits(90, 1)} << 49) |
                     (u64{1} << 50) | (u64{7} << 51));
                break;
            }
            const bool aoffi = insn.Bits(76, 1);
            if (dimension == 1 && (lod == 2 || (lod <= 1 && (mask == 5 || mask == 6 || aoffi)))) {
                const u32 count = std::popcount(mask), binding = insn.Bits(54, 5);
                if (half || depth_compare || !mask || !insn.Bits(60, 1) || insn.Bits(77, 1) || (aoffi && lod > 1) ||
                    insn.Bits(81, 3) != 7 || insn.Bits(59, 1) || insn.Bits(40, 14) > 0x1fff ||
                    binding >= 18 || ((lod == 2 || aoffi) && (source_a & 1)) || source_a >= 248 || source_b >= 248 ||
                    dest >= 248 || (count > 1 && ((dest & 1) || dest + 1 >= 248)) ||
                    (count <= 2 ? split : !split || second_dest >= 248) ||
                    (count == 4 && ((second_dest & 1) || second_dest + 1 >= 248)))
                    return fail(index, "native biased texture sample mode");
                const bool bindless = result.required_texture_buffer != ~0U && result.required_texture_buffer != binding;
                if (!bindless) result.required_texture_buffer = binding;
                const auto copy = [&](u32 target, u32 source) {
                    emit(0x5c98000000000000ULL | predicate | target | (u64{15} << 39) | (u64{source} << 20));
                };
                if (bindless) {
                    emit(0x4c98000000070000ULL | 248 | (u64{15} << 39) |
                         (u64{insn.Bits(40, 14)} << 20) | (u64{binding} << 34));
                    if (lod == 2 || aoffi) copy(249, source_b);
                }
                if (lod != 2 && !aoffi) { copy(250, source_a); copy(251, source_b); }
                emit((bindless ? 0xde80000000000000ULL : 0xc000000000000000ULL) | predicate | 250 |
                     (u64{lod == 2 || aoffi ? source_a : 250U} << 8) | (u64{bindless ? 248U : lod == 2 || aoffi ? source_b : 255U} << 20) |
                     (u64{2} << 28) | (u64{mask} << 31) | (u64{7} << 51) |
                     (u64{insn.Bits(90, 1)} << 49) | (u64{lod} << (bindless ? 37 : 55)) |
                     (u64{aoffi} << (bindless ? 36 : 54)) |
                     (bindless ? 0 : u64{insn.Bits(40, 14)} << 36));
                for (u32 component = 0; component < count; ++component)
                    copy(component < 2 ? static_cast<u32>(dest) + component : second_dest + component - 2,
                         250 + component);
                break;
            }
            if (dimension == 5 && depth_compare && lod <= 1 && !split && mask == 1) {
                if (!insn.Bits(60, 1) || insn.Bits(76, 2) || insn.Bits(81, 3) != 7 ||
                    insn.Bits(59, 1) || insn.Bits(40, 14) > 0x1fff || insn.Bits(54, 5) >= 18 ||
                    (source_a & 1) || (source_b & 1) || source_a + 1 >= reserved_register || source_b + 1 >= reserved_register || dest >= reserved_register)
                    return fail(index, "native array depth comparison sample mode");
                const u32 binding = insn.Bits(54, 5);
            if (result.required_texture_buffer != ~0U && result.required_texture_buffer != binding)
                return fail(index, "native texture descriptor buffer conflict");
            result.required_texture_buffer = binding;
                for (u32 i = 0; i < 3; ++i) {
                    const u32 source = i < 2 ? source_a + i : source_b;
                    emit(0x5c98000000000000ULL | predicate | (250 + i) |
                         (u64{15} << 39) | (u64{source} << 20));
                }
                emit(0xc000000000000000ULL | predicate | dest | (u64{250} << 8) |
                     (u64{source_b + 1} << 20) | (u64{3} << 28) | (u64{1} << 31) |
                     (u64{insn.Bits(40, 14)} << 36) | (u64{insn.Bits(90, 1)} << 49) |
                     (u64{1} << 50) | (u64{7} << 51) | (u64{lod} << 55));
                break;
            }
            if (dimension == 5 && lod == 2 && !LowerGroupDisabled("array2d_bias")) { // [merge-tex-array2d-bias]
                // Array2D bias samples split {array, x} and {y, bias} across the two sources.
                if (half || !insn.Bits(60, 1) || depth_compare || mask != 15 || !split ||
                    insn.Bits(76, 2) || insn.Bits(81, 3) != 7 || insn.Bits(59, 1) ||
                    insn.Bits(40, 14) > 0x1fff || insn.Bits(54, 5) != 2 ||
                    (source_a & 1) || (source_b & 1) || source_a >= 249 || source_b >= 249 ||
                    (dest & 1) || dest >= 254 || (second_dest & 1) || second_dest >= 254)
                    return fail(index, "native Array2D bias texture mode");
                if (result.required_texture_buffer != ~0U && result.required_texture_buffer != 2)
                    return fail(index, "native texture descriptor buffer conflict");
                result.required_texture_buffer = 2;
                const auto copy = [&](u32 target, u32 source) {
                    emit(0x5c98000000000000ULL | predicate | target |
                         (u64{15} << 39) | (u64{source} << 20));
                };
                copy(250, source_a);
                copy(251, source_a + 1);
                copy(252, source_b);
                copy(253, source_b + 1);
                emit(0xc000000000000000ULL | predicate | 250 | (u64{250} << 8) |
                     (u64{253} << 20) | (u64{3} << 28) | (u64{mask} << 31) |
                     (u64{insn.Bits(40, 14)} << 36) | (u64{insn.Bits(90, 1)} << 49) |
                     (u64{7} << 51) | (u64{2} << 55));
                copy(static_cast<u32>(dest), 250);
                copy(static_cast<u32>(dest) + 1, 251);
                copy(second_dest, 252);
                copy(second_dest + 1, 253);
                break;
            }
            if ((dimension == 3 && (lod == 0 || lod == 1 || lod == 3)) || (dimension == 2 && (lod <= 1 || (lod == 2 && !LowerGroupDisabled("cube3d_lod"))))) {
                // [merge-tex-3d-bias] 3D bias (lod 2) takes the bias value from the register after Z, like cube explicit LOD.
                const bool lod_register = (dimension == 3 && lod == 3) || (dimension == 2 && lod == 2);
                const u32 components = static_cast<u32>(std::popcount(mask));
                if (!insn.Bits(60, 1) || depth_compare || components == 0 ||
                    insn.Bits(76, 2) || insn.Bits(81, 3) != 7 ||
                    insn.Bits(59, 1) || insn.Bits(40, 14) > 0x1fff || insn.Bits(54, 5) >= 18 ||
                    (source_a & 1) || (lod_register && (source_b & 1)) || source_a >= 249 || source_b >= 249 ||
                    dest == 255 || (components > 1 && ((dest & 1) || dest >= 254)) ||
                    (components > 2 && (!split || second_dest >= 248)) ||
                    (components == 4 && (second_dest & 1)))
                    return fail(index, "native cube explicit-LOD sample mode");
                const u32 binding = insn.Bits(54, 5);
                const bool bindless = result.required_texture_buffer != ~0U && result.required_texture_buffer != binding;
                if (!bindless) result.required_texture_buffer = binding;
                const auto copy = [&](u32 target, u32 source) {
                    emit(0x5c98000000000000ULL | predicate | target | (u64{15} << 39) | (u64{source} << 20));
                };
                copy(bindless ? 252 : 250, source_a);
                copy(bindless ? 253 : 251, source_a + 1);
                copy(bindless ? 254 : 252, source_b);
                if (bindless) {
                    // Scratch descriptors must not merge with an unrelated value on the
                    // skipped path; only the texture operation needs predication.
                    emit(0x4c98000000070000ULL | 250 | (u64{15} << 39) |
                         (u64{insn.Bits(40, 14)} << 20) | (u64{binding} << 34));
                    if (lod_register) copy(251, source_b + 1);
                }
                emit((bindless ? 0xde80000000000000ULL : 0xc000000000000000ULL) | predicate | 250 | (u64{bindless ? 252U : 250U} << 8) |
                     (u64{bindless ? 250U : lod_register ? source_b + 1 : 255U} << 20) | (u64{dimension == 3 ? 6U : 4U} << 28) | (u64{mask} << 31) |
                     (bindless ? 0 : u64{insn.Bits(40, 14)} << 36) | (u64{insn.Bits(90, 1)} << 49) |
                     (u64{7} << 51) | (u64{lod} << (bindless ? 37 : 55)));
                for (u32 component = 0; component < components; ++component)
                    copy(component < 2 ? static_cast<u32>(dest) + component : second_dest + component - 2,
                         250 + component);
                break;
            }
            if (split) {
                for (u32 i = 0; i < rgba_masks.size(); ++i) if (rgba_masks[i] == mask) swizzle = i;
            } else {
                for (u32 i = 0; i < rg_masks.size(); ++i) if (rg_masks[i] == mask) swizzle = i;
            }
            // SM70 scalar coordinates and split RG/BA destinations match TEXS.
            // Descriptor offsets are words in both encodings, but only native
            // explicitly encodes the constant-buffer binding.
            if (!insn.Bits(60, 1) || (dimension != 1 && dimension != 5) ||
                swizzle == ~0U || insn.Bits(76, 2) ||
                insn.Bits(81, 3) != 7 || (lod > 1 && (lod != 3 || dimension != 1 || depth_compare)) ||
                (lod == 3 && ((source_a & 1) || source_a >= 249)) ||
                insn.Bits(59, 1) ||
                insn.Bits(40, 14) > 0x1fff || insn.Bits(54, 5) >= 18 ||
                dest == 255 || (!half && mask & (mask - 1) && ((dest & 1) || dest >= 254)) ||
                (!half && split && mask == 15 && ((second_dest & 1) || second_dest >= 254)) ||
                (depth_compare && (dimension != 1 || split || mask != 1 || (source_a & 1) || source_a >= 249)) ||
                (dimension == 5 && ((source_a & 1) || source_a >= 249))) {
                return fail(index, "native texture mode or descriptor binding");
            }
            const u32 binding = insn.Bits(54, 5);
            if (result.required_texture_buffer != ~0U && result.required_texture_buffer != binding) {
                if (half || dimension != 1 || depth_compare || lod > 1)
                    return fail(index, "native mixed-bank texture sample mode");
                const auto copy = [&](u32 target, u32 source) {
                    emit(0x5c98000000000000ULL | predicate | target | (u64{15} << 39) | (u64{source} << 20));
                };
                copy(250, source_a);
                copy(251, source_b);
                emit(0x4c98000000070000ULL | 253 | (u64{15} << 39) |
                     (u64{insn.Bits(40, 14)} << 20) | (u64{binding} << 34));
                emit(0xde80000000000000ULL | predicate | 250 | (u64{250} << 8) |
                     (u64{253} << 20) | (u64{2} << 28) | (u64{mask} << 31) |
                     (u64{lod} << 37) | (u64{7} << 51) | (u64{insn.Bits(90, 1)} << 49));
                const u32 count = std::popcount(mask);
                for (u32 component = 0; component < count; ++component)
                    copy(component < 2 ? static_cast<u32>(dest) + component : second_dest + component - 2,
                         250 + component);
                break;
            }
            result.required_texture_buffer = binding;
            emit((half ? 0xd000000000000000ULL : 0xd800000000000000ULL) | predicate | dest | (u64{insn.Bits(24, 8)} << 8) |
                 (u64{insn.Bits(32, 8)} << 20) | (u64{insn.Bits(64, 8)} << 28) |
                 (u64{insn.Bits(40, 14)} << 36) | (u64{insn.Bits(90, 1)} << 49) |
                 (u64{swizzle} << 50) | (u64{dimension == 5 ? (lod ? 8U : 7U) : depth_compare ? (lod ? 6U : 4U) : (lod == 3 ? 3U : lod ? 2U : 1U)} << 53));
            break;
        }
        case 0x290: { // Captured uniform carry chain, with a zero third addend.
            const u32 a = uniform_register(insn.Bits(24, 8));
            const u32 b = uniform_register(insn.words[1]);
            if (dest >= 63 || a == ~0U || b == ~0U || native_predicate != 7)
                return fail(index, "native uniform carry registers");
            const u32 output = insn.Bits(81, 3), condition = insn.Bits(87, 4);
            if (output < 7 && (insn.words[2] & ~0x000e0000U) == 0x0ff1e03f) {
                const u32 temporary = 6 - output, mask = 1U << temporary;
                emit(0x5c10000000070000ULL | 250 | (u64{a} << 8) | (u64{b} << 20));
                emit(0x38e8000000070000ULL | 253 | (u64{255} << 8) | (u64{mask} << 20));
                emit(0x5b60000000070000ULL | 7 | (u64{temporary} << 3) | (u64{250} << 8) |
                     (u64{b} << 20) | (u64{7} << 39) | (u64{1} << 49));
                emit(0x38e8000000070000ULL | 239 | (u64{239} << 8) | (u64{mask} << 20));
                emit(0x38f0000000070000ULL | (u64{253} << 8) | (u64{mask} << 20));
                emit(0x5c98000000070000ULL | uniform_register(static_cast<u32>(dest)) | (u64{250} << 20) | (u64{15} << 39));
            } else if (a == 255 && condition < 7 &&
                       insn.words[2] == (0x087fe43fU | (condition << 23))) {
                emit(0x3828000000070000ULL | 250 | (u64{239} << 8) | (u64{6 - condition} << 20));
                emit(0x0400000000070000ULL | 250 | (u64{250} << 8) | (u64{1} << 20));
                emit(0x5c10000000070000ULL | uniform_register(static_cast<u32>(dest)) | (u64{b} << 8) | (u64{250} << 20));
            } else return fail(index, "native uniform carry mode");
            break;
        }
        case 0x892: { // Captured uniform bit test, with only its predicate retained.
            const u32 output = insn.Bits(81, 3), source = uniform_register(insn.Bits(24, 8));
            if (dest != 63 || output == 7 || source == ~0U || native_predicate != 7 ||
                (insn.words[2] & ~0x000e0000U) != 0x0f80c03f)
                return fail(index, "native uniform LUT predicate mode");
            const u32 temporary = 6 - output, mask = 1U << temporary;
            emit(0x38e8000000070000ULL | 253 | (u64{255} << 8) | (u64{mask} << 20));
            mov_imm(252, insn.words[1]);
            emit(0x5c40000000070000ULL | 255 | (u64{source} << 8) | (u64{252} << 20) |
                 (u64{3} << 44) | (u64{temporary} << 48));
            emit(0x38e8000000070000ULL | 239 | (u64{239} << 8) | (u64{mask} << 20));
            emit(0x38f0000000070000ULL | (u64{253} << 8) | (u64{mask} << 20));
            break;
        }
        case 0x896: { // UPRMT sign extension of the low half-word.
            const u32 source = uniform_register(insn.Bits(24, 8));
            if (dest >= 63 || source == ~0U || native_predicate != 7 ||
                insn.words[1] != 0x9910 || insn.words[2] != 0x0800003f)
                return fail(index, "native uniform permutation mode");
            emit(0x3848000000000000ULL | predicate | 250 | (u64{source} << 8) |
                 (u64{16} << 20));
            emit(0x3828000000000000ULL | predicate | uniform_register(static_cast<u32>(dest)) | (u64{250} << 8) |
                 (u64{16} << 20) | (u64{1} << 48));
            break;
        }
        case 0x816: { // Captured PRMT half-word concatenation
            if (insn.words[1] == 0x7054 && !(insn.words[2] & ~0xffU)) {
                emit(0x0400000000000000ULL | predicate | 250 | (u64{insn.Bits(24, 8)} << 8) |
                     (u64{0xff} << 20));
                emit(0x0400000000000000ULL | predicate | 251 | (u64{insn.Bits(64, 8)} << 8) |
                     (u64{0xff00ffff} << 20));
                emit(0x3848000000000000ULL | predicate | 250 | (u64{250} << 8) | (u64{16} << 20));
                emit(base(0x5c40000000000000ULL) | (u64{251} << 8) | (u64{250} << 20) |
                     (u64{1} << 41) | (u64{7} << 48));
                break;
            }
            if (insn.words[1] == 0x9910 && !(insn.words[2] & ~0xffU)) {
                emit(0x3848000000000000ULL | predicate | 250 | (u64{insn.Bits(24, 8)} << 8) |
                     (u64{16} << 20));
                emit(base(0x3828000000000000ULL) | (u64{250} << 8) | (u64{16} << 20) | (u64{1} << 48));
                break;
            }
            if (insn.words[1] != 0x5410 && insn.words[1] != 0x5432 &&
                insn.words[1] != 0x7610 && insn.words[1] != 0x7632) {
                if ((insn.words[1] & ~0x7777U) || (insn.words[2] & ~0xffU))
                    return fail(index, "native byte permutation mode");
                const u32 a = insn.Bits(24, 8), b = insn.Bits(64, 8);
                for (u32 byte = 0; byte < 4; ++byte) {
                    const u32 selector = (insn.words[1] >> (byte * 4)) & 7;
                    emit(0x3828000000000000ULL | predicate | 250 |
                         (u64{selector < 4 ? a : b} << 8) | (u64{(selector & 3) * 8} << 20));
                    emit(0x0400000000000000ULL | predicate | 250 | (u64{250} << 8) | (u64{0xff} << 20));
                    if (byte == 0)
                        emit(0x5c98000000000000ULL | predicate | 251 | (u64{250} << 20) | (u64{15} << 39));
                    else
                        emit(0x5c18000000000000ULL | predicate | 251 | (u64{250} << 8) |
                             (u64{251} << 20) | (u64{byte * 8} << 39));
                }
                emit(base(0x5c98000000000000ULL) | (u64{251} << 20) | (u64{15} << 39));
                break;
            }
            if ((insn.words[1] != 0x5410 && insn.words[1] != 0x5432 && insn.words[1] != 0x7610 && insn.words[1] != 0x7632) ||
                (insn.words[2] & ~0xffU)) return fail(index, "native permutation mode");
            const u32 a = insn.Bits(24, 8), b = insn.Bits(64, 8);
            if (insn.words[1] != 0x5432 && insn.words[1] != 0x7632) {
                emit(0x0400000000000000ULL | predicate | 250 | (u64{a} << 8) | (u64{0xffff} << 20));
            } else {
                emit(0x3828000000000000ULL | predicate | 250 | (u64{a} << 8) | (u64{16} << 20));
            }
            if (insn.words[1] == 0x7610 || insn.words[1] == 0x7632) {
                emit(0x0400000000000000ULL | predicate | 251 | (u64{b} << 8) | (u64{0xffff0000} << 20));
                emit(base(0x5c10000000000000ULL) | (u64{250} << 8) | (u64{251} << 20));
            } else {
                emit(base(0x5c18000000000000ULL) | (u64{b} << 8) | (u64{250} << 20) | (u64{16} << 39));
            }
            break;
        }
        case 0x307: { // FRND.F32, round to an integral floating-point value
            if (insn.Bits(75, 2) != 2 || insn.Bits(84, 2) != 2 || insn.Bits(81, 3) ||
                insn.Bits(86, 6)) return fail(index, "native floating round format");
            emit(base(0x5ca8000000000000ULL) | (u64{2} << 8) | (u64{2} << 10) |
                 (u64{insn.Bits(32, 8)} << 20) | (u64{8 + insn.Bits(78, 2)} << 39) |
                 (u64{insn.Bits(80, 1)} << 44) | (u64{insn.Bits(63, 1)} << 45) |
                 (u64{insn.Bits(62, 1)} << 49));
            break;
        }
        case 0x321: // ALD
        case 0x322: { // AST
            if (insn.Bits(76, 2) || insn.Bits(79, 1)) return fail(index, "native indexed/patch attribute access");
            const u32 address = insn.Bits(40, 10);
            const u32 components = insn.Bits(74, 2) + 1;
            if (address % 4 || address + components * 4 > 0x400 || insn.Bits(24, 8) != 255) {
                return fail(index, "native attribute address or indexing");
            }
            const u32 value_reg = opcode == 0x321 ? static_cast<u32>(dest) : insn.Bits(32, 8);
            if (value_reg + components > reserved_register && !(opcode == 0x322 && value_reg == 255))
                return fail(index, "native attribute register range");
            const u32 vertex = opcode == 0x321 ? insn.Bits(32, 8) : insn.Bits(64, 8);
            if (opcode == 0x322 && value_reg == 255 && components > 1) { // [merge-ast-rz]
                // Maxwell increments a vector store's source register, which would wrap RZ.
                // Preserve the native all-zero vector by emitting scalar RZ stores instead.
                for (u32 component = 0; component < components; ++component) {
                    emit(0xeff0000000000000ULL | predicate | 255 | (u64{255} << 8) |
                         (u64{address + component * 4} << 20) | (u64{vertex} << 39));
                }
                break;
            }
            const u64 translated = (opcode == 0x321 ? 0xefd8000000000000ULL : 0xeff0000000000000ULL) |
                predicate | value_reg | (u64{255} << 8) | (u64{address} << 20) |
                (u64{vertex} << 39) | (u64{components - 1} << 47);
            result.instructions.push_back(translated);
            break;
        }
        case 0x387: case 0x983: { // Scalar local spills with register-relative byte offsets.
            const bool store = opcode == 0x387;
            const u32 data = store ? insn.Bits(32, 8) : static_cast<u32>(dest);
            if ((store && insn.Bits(16, 8)) || (!store && insn.Bits(32, 8)) ||
                (data >= reserved_register && data != 255) ||
                (insn.Bits(40, 24) & 3) ||
                (insn.words[2] != 0x00100800U && (store || insn.words[2] != 0x00300800U)))
                return fail(index, "native local scalar spill mode");
            emit((store ? 0xef50000000000000ULL : 0xef40000000000000ULL) | predicate |
                 data | (u64{insn.Bits(24, 8)} << 8) | (u64{insn.Bits(40, 24)} << 20) | (u64{4} << 48));
            break;
        }
        case 0x883: { // UP2UR with UP0 selected and a zero merge source.
            if (dest >= 63 || insn.Bits(24, 8) != 63 ||
                native_predicate != 7 || insn.words[1] != 1 || insn.words[2] != 0x08000000)
                return fail(index, "native uniform predicate-to-register mode");
            emit(0x3828000000070000ULL | uniform_register(static_cast<u32>(dest)) | (u64{239} << 8) | (u64{6} << 20));
            emit(0x0400000000070000ULL | uniform_register(static_cast<u32>(dest)) | (uniform_register(static_cast<u32>(dest)) << 8) | (u64{1} << 20));
            break;
        }
        case 0x803: { // P2R PR, register merge source, immediate mask
            if (insn.words[1] > 0x7f || insn.words[2])
                return fail(index, "native predicate-to-register mode");
            emit(base(0x38e8000000000000ULL) | (u64{insn.Bits(24, 8)} << 8) |
                 (u64{insn.words[1]} << 20));
            break;
        }
        case 0x804: { // R2P PR, register, immediate mask
            if (insn.Bits(16, 8) || insn.words[1] > 0x7f || (insn.words[2] & ~0x3000U))
                return fail(index, "native register-to-predicate mode");
            emit(0x38f0000000000000ULL | predicate | (u64{insn.Bits(24, 8)} << 8) |
                 (u64{insn.words[1]} << 20) | (u64{insn.Bits(76, 2)} << 41));
            break;
        }
        case 0xc02: // MOV uniform register
        case 0x202: // MOV register
        case 0x802: // MOV immediate
        case 0xa02: { // MOV constant buffer
            if (insn.Bits(72, 4) != 15 || (insn.Bits(76, 16) & ~(opcode == 0xc02 ? 0x8000U : 0U))) return fail(index, "native MOV modifiers");
            if (opcode == 0x802) {
                result.instructions.push_back(base(0x0100000000000000ULL) | (u64{15} << 12) |
                    (u64{insn.words[1]} << 20));
            } else if (opcode == 0xc02) {
                if (insn.words[1] >= 64 || insn.words[2] != 0x08000f00) return fail(index, "native uniform move source");
                emit(base(0x5c98000000000000ULL) | (u64{15} << 39) | (u64{uniform_register(insn.words[1])} << 20));
            } else if (opcode == 0x202) {
                if (insn.Bits(40, 24)) return fail(index, "native MOV register modifiers");
                result.instructions.push_back(base(0x5c98000000000000ULL) | (u64{15} << 39) |
                    (u64{insn.Bits(32, 8)} << 20));
            } else {
                if (!valid_cbuf() || insn.Bits(62, 2)) return fail(index, "native MOV constant buffer");
                result.instructions.push_back(base(0x4c98000000000000ULL) | (u64{15} << 39) | cbuf());
            }
            break;
        }
        case 0x223: case 0x423: case 0x623: case 0x823: case 0xa23: case 0xc23: case 0xe23: { // FFMA
            if (insn.Bits(76, 1) || (insn.Bits(81, 11) & ~((opcode == 0xc23 || opcode == 0xe23) ? 0x400U : 0U))) return fail(index, "native FFMA denormal/source mode");
            const u32 a = operand(0), b = operand(1), c = operand(2);
            emit(base(0x5980000000000000ULL) | (u64{a} << 8) | (u64{b} << 20) | (u64{c} << 39) |
                (u64{insn.Bits(77, 1)} << 50) | (u64{insn.Bits(78, 2)} << 51) | (u64{insn.Bits(80, 1)} << 53));
            break;
        }
        case 0x220: case 0x820: case 0xa20: case 0xc20: { // FMUL
            const u32 scale = insn.Bits(84, 3);
            if (insn.Bits(76, 1) || !scale || insn.Bits(81, 3) ||
                insn.Bits(87, 4) || (insn.Bits(91, 1) != 0) != (opcode == 0xc20) ||
                (scale != 4 && (!insn.Bits(80, 1) || insn.Bits(78, 2))))
                return fail(index, "native FMUL scale/mode");
            const u32 a = operand(0), b = operand(1);
            emit(base(0x5c68000000000000ULL) | (u64{a} << 8) | (u64{b} << 20) |
                (u64{insn.Bits(78, 2)} << 39) | (u64{scale < 4 ? 4U - scale : scale > 4 ? 11U - scale : 0U} << 41) | (u64{insn.Bits(80, 1)} << 44) | (u64{insn.Bits(77, 1)} << 50));
            break;
        }
        case 0x221: case 0x421: case 0x621: case 0x821: case 0xa21: case 0xe21: { // FADD
            if (insn.Bits(81, 10) || (insn.Bits(91, 1) != 0) != (opcode == 0xe21)) return fail(index, "native FADD mode");
            const u32 a = operand(0), b = operand(insn.Bits(9, 3) == 2 || insn.Bits(9, 3) == 3 || insn.Bits(9, 3) == 7 ? 2 : 1);
            emit(base(0x5c58000000000000ULL) | (u64{a} << 8) | (u64{b} << 20) |
                (u64{insn.Bits(78, 2)} << 39) | (u64{insn.Bits(80, 1)} << 44) | (u64{insn.Bits(77, 1)} << 50));
            break;
        }
        case 0x209: case 0x809: case 0xa09: { // FMNMX
            if (insn.Bits(74, 6) || insn.Bits(82, 5) || insn.Bits(91, 1))
                return fail(index, "native floating min/max mode");
            const u32 a = operand(0), b = operand(1);
            const bool propagate_nan = insn.Bits(81, 1);
            if (propagate_nan)
                emit(0x5800000000000000ULL | predicate | 253 | (u64{a} << 8) |
                     (u64{b} << 20) | (u64{7} << 39) | (u64{7} << 48));
            emit(base(0x5c60000000000000ULL) | (u64{a} << 8) | (u64{b} << 20) |
                 (u64{insn.Bits(87, 3)} << 39) | (u64{insn.Bits(90, 1)} << 42) |
                 (u64{insn.Bits(80, 1)} << 44));
            if (propagate_nan) {
                emit(base(0x5c40000000000000ULL) | (dest << 8) | (u64{253} << 20) | (u64{7} << 48));
                emit(0x0400000000000000ULL | predicate | 253 | (u64{253} << 8) |
                     (u64{0xffffffffU} << 20) | (u64{2} << 53));
                emit(0x0400000000000000ULL | predicate | 253 | (u64{253} << 8) | (u64{0x7fffffffU} << 20));
                emit(base(0x5c40000000000000ULL) | (dest << 8) | (u64{253} << 20) |
                     (u64{1} << 41) | (u64{7} << 48));
            }
            break;
        }
        case 0x308: case 0xb08: case 0xd08: { // MUFU
            const u32 operation = insn.Bits(74, 6);
            if (operation > 9 || (operation == 9 && insn.Bits(73, 1)) || operation == 6 || operation == 7 || insn.Bits(80, 11) ||
                (opcode == 0xd08 ? !insn.Bits(91, 1) || (insn.words[1] & ~0xc00000ffU) != 0 : insn.Bits(91, 1) != 0)) return fail(index, "native MUFU mode");
            if (insn.Bits(73, 1)) {
                if (opcode != 0x308 || insn.Bits(60, 2) > 1)
                    return fail(index, "native half special-function source");
                emit(0x5ca8000000000000ULL | predicate | 250 | (u64{2} << 8) | (u64{1} << 10) |
                     (u64{insn.Bits(32, 8)} << 20) | (u64{insn.Bits(60, 1)} << 41) |
                     (u64{insn.Bits(63, 1)} << 45) | (u64{insn.Bits(62, 1)} << 49));
                emit(0x5080000000000000ULL | predicate | 250 | (u64{250} << 8) | (u64{operation} << 20));
                emit(base(0x5ca8000000000000ULL) | (u64{1} << 8) | (u64{2} << 10) | (u64{250} << 20));
            } else {
                const u32 src = operand(1);
                emit(base(0x5080000000000000ULL) | (u64{src} << 8) | (u64{operation} << 20));
            }
            break;
        }
        case 0x526: { // IPA with a uniform byte offset into the attribute array.
            const u32 index_reg = uniform_register(insn.Bits(24, 8));
            if (index_reg == ~0U || insn.words[1] != 255 ||
                (insn.words[2] & ~0x40ffU) != 0x080e0000)
                return fail(index, "native uniform indexed interpolation mode");
            mov_imm(252, insn.Bits(64, 8) * 4);
            emit(0x5c10000000000000ULL | predicate | 250 | (u64{index_reg} << 8) | (u64{252} << 20));
            emit(base(0xe000000000000000ULL) | (u64{250} << 8) | (u64{255} << 20) |
                 (u64{insn.Bits(64, 8)} << 30) | (u64{1} << 38) | (u64{insn.Bits(78, 1)} << 55));
            break;
        }
        case 0x326: { // IPA.PASS / CONSTANT, default sample location
            if (insn.Bits(32, 8) != 255 || (insn.Bits(76, 4) != 0 && insn.Bits(76, 4) != 4) || insn.Bits(81, 3) != 7) return fail(index, "native IPA interpolation mode");
            emit(base(0xe000000000000000ULL) | (u64{255} << 8) | (u64{255} << 20) | (u64{insn.Bits(64, 8)} << 30) | (u64{insn.Bits(78, 1)} << 55));
            break;
        }
        case 0x805: { // CS2R SRZ
            if (insn.Bits(72, 8) != 255 || !insn.Bits(80, 1) || dest + 2 > reserved_register) return fail(index, "native special-register read");
            mov_imm(static_cast<u32>(dest), 0);
            mov_imm(static_cast<u32>(dest) + 1, 0);
            break;
        }
        case 0x312: { // I2F.F32.U64, paired register input, RN or RP.
            const u32 source = insn.Bits(32, 8);
            if ((insn.words[2] != 0x00301000 && insn.words[2] != 0x00309000) ||
                (insn.words[1] & ~0xffU) || insn.Bits(24, 8) || source >= 254 || (source & 1))
                return fail(index, "native unsigned 64-bit integer-to-float mode");
            emit(base(0x5cb8000000000000ULL) | (u64{2} << 8) | (u64{3} << 10) |
                 (u64{source} << 20) | (u64{insn.words[2] == 0x00309000 ? 2U : 0U} << 39));
            break;
        }
        case 0x306: { // I2F.F32 from the low 8/16/32 bits of a register, sign at bit 74 [merge-i2f306]
            // Source size (bits 84-85), signedness (74) and rounding (78-79) use the same value
            // order as the Maxwell I2F fields; only the F32 destination (75-76 == 2) is accepted.
            // Bits 60-61 select the source byte or half-word (Maxwell I2F bits 41-42).
            const u32 size = insn.Bits(84, 2), selector = insn.Bits(60, 2);
            if ((insn.words[2] & ~0x0030c400U) != 0x1000 || (insn.words[1] & ~0x300000ffU) ||
                size == 3 || (size == 1 && selector > 1) || (size == 2 && selector != 0) ||
                (insn.words[2] != 0x1000 && LowerGroupDisabled("byte_sel306")))
                return fail(index, "native byte-to-float conversion mode");
            emit(base(0x5cb8000000000000ULL) | (u64{2} << 8) | (u64{size} << 10) |
                 (u64{insn.Bits(74, 1)} << 13) | (u64{insn.Bits(78, 2)} << 39) |
                 (u64{selector} << 41) | (u64{insn.Bits(32, 8)} << 20));
            break;
        }
        case 0x245: case 0xa45: case 0xc45: { // I2FP.F32.U32
            const u32 rounding = insn.Bits(78, 2);
            if ((insn.words[2] & ~(0xc400U | (opcode == 0xc45 ? 0x08000000U : 0U))) != 0x00201000 ||
                (rounding != 0 && (rounding != 3 || insn.Bits(74, 1))))
                return fail(index, "native packed integer-to-float format");
            const u32 src = operand(1, false);
            emit(base(0x5cb8000000000000ULL) | (u64{2} << 8) | (u64{2} << 10) |
                 (u64{insn.Bits(74, 1)} << 13) | (u64{src} << 20) | (u64{rounding} << 39));
            break;
        }
        case 0x207: case 0x807: case 0xa07:
        case 0x208: case 0x808: case 0xa08: { // Bitwise SEL / FSEL
            // [merge-audit-fsel] FSEL carries abs/neg on both selected operands (bits 73/72 and 62/63, as for FADD).
            // Off by default until it is proven not to change FastFusion colours: EDEN_NATIVE_LOWER_ENABLE=audit_fsel.
            const bool floating = (opcode & 0x1ff) == 0x8 && LowerGroupEnabled("audit_fsel");
            const u32 a = operand(0, floating), b = operand(1, floating);
            emit(base(0x5ca0000000000000ULL) | (u64{a} << 8) | (u64{b} << 20) |
                (u64{insn.Bits(87, 3)} << 39) | (u64{insn.Bits(90, 1)} << 42));
            break;
        }
        case 0x20a: case 0x80a: case 0xa0a: { // FSET.BF with PT/AND
            // [merge-fset-or] One observed FSET.BF form combines with a PT-OR instead of the AND.
            const bool fast_fusion_or = insn.words[0] == 0x131e720a &&
                insn.words[1] == 0x000000ff && insn.words[2] == 0x0081d400;
            if (!fast_fusion_or &&
                (insn.Bits(64, 8) || insn.Bits(74, 2) || insn.Bits(81, 6) ||
                insn.Bits(87, 4) != 7 || insn.Bits(91, 1)))
                return fail(index, "native floating comparison output mode");
            const u32 a = operand(0), b = operand(1);
            emit(base(0x5800000000000000ULL) | (u64{a} << 8) | (u64{b} << 20) |
                 (u64{fast_fusion_or ? 1U : 7U} << 39) |
                 (u64{fast_fusion_or ? 1U : 0U} << 45) |
                 (u64{insn.Bits(76, 4)} << 48) | (u64{1} << 52) |
                 (u64{insn.Bits(80, 1)} << 55));
            break;
        }
        case 0x20b: case 0x80b: case 0xa0b: case 0xc0b: // FSETP
        case 0x20c: case 0x80c: case 0xa0c: case 0xc0c: { // ISETP
            const bool floating = (opcode & 0x1ff) == 0xb;
            if (!floating && insn.Bits(72, 1)) {
                const u32 comparison = insn.Bits(76, 3);
                if (insn.Bits(74, 2) || insn.Bits(84, 3) != 7 || insn.Bits(87, 4) != 7)
                    return fail(index, "native extended integer comparison");
                const u32 low_comparison = insn.Bits(68, 4);
                if (comparison == 1 || comparison == 3 || comparison == 4 || comparison == 6) {
                    const u32 a = insn.Bits(24, 8), b = insn.Bits(32, 8);
                    if (opcode != 0x20c || insn.Bits(73, 1) || insn.Bits(64, 4) ||
                        insn.Bits(80, 1) || (insn.words[1] & ~0xffU) ||
                        (a >= 250 && a != 255) || (b >= 250 && b != 255))
                        return fail(index, "native extended ordered comparison mode");
                    const auto set_mask = [&](u32 target, u32 operation, u32 lhs, u32 rhs) {
                        emit(0x5b50000000000000ULL | predicate | target | (u64{lhs} << 8) |
                             (u64{rhs} << 20) | (u64{7} << 39) | (u64{operation} << 49));
                    };
                    if ((low_comparison & 7) == 7) mov_imm(252, (low_comparison & 8) ? 0U : ~0U);
                    else {
                        emit(0x38e8000000000000ULL | predicate | 252 | (u64{255} << 8) |
                             (u64{1} << (20 + (low_comparison & 7))));
                        set_mask(252, (low_comparison & 8) ? 2U : 5U, 252, 255);
                    }
                    set_mask(250, comparison == 1 || comparison == 3 ? 1U : 4U, a, b);
                    set_mask(251, 2, a, b);
                    emit(0x5c40000000000000ULL | predicate | 251 | (u64{251} << 8) |
                         (u64{252} << 20) | (u64{7} << 48));
                    emit(0x5c40000000000000ULL | predicate | 250 | (u64{250} << 8) |
                         (u64{251} << 20) | (u64{1} << 41) | (u64{7} << 48));
                    emit(0x5b60000000000000ULL | predicate | 7 | (u64{insn.Bits(81, 3)} << 3) |
                         (u64{250} << 8) | (u64{255} << 20) | (u64{7} << 39) | (u64{5} << 49));
                    break;
                }
                if (comparison != 2 && comparison != 5)
                    return fail(index, "native extended integer comparison");
                insn.words[2] = (insn.words[2] & ~(0x100U | 0xc00U | 0x07800000U)) |
                    (low_comparison << 23) | (comparison == 5 ? 0x400U : 0U);
            }
            const u32 a = operand(0, floating), b = operand(1, floating);
            const u32 combine = insn.Bits(74, 2);
            if (combine > 2) return fail(index, "native predicate combination");
            u64 out = (floating ? 0x5bb0000000000000ULL : 0x5b60000000000000ULL) | predicate |
                insn.Bits(84, 3) | (u64{insn.Bits(81, 3)} << 3) | (u64{a} << 8) | (u64{b} << 20) |
                (u64{insn.Bits(87, 3)} << 39) | (u64{insn.Bits(90, 1)} << 42) | (u64{combine} << 45);
            if (floating) out |= (u64{insn.Bits(80, 1)} << 47) | (u64{insn.Bits(76, 4)} << 48);
            else out |= (u64{insn.Bits(73, 1)} << 48) | (u64{insn.Bits(76, 3)} << 49);
            emit(out);
            break;
        }
        case 0x212: case 0x812: case 0xa12: case 0xc12: { // Bounded LOP3 bitwise forms, no predicate output
            const u32 lut = insn.Bits(72, 8);
            const bool bounded_lut = lut == 0xc0 || lut == 0xfc || lut == 0xf8 || lut == 0x48 || lut == 0xe2 || lut == 0xfe ||
                lut == 0xc8 || lut == 0x78 || lut == 0x3c || lut == 0x30 || lut == 0xf4 || lut == 4 || lut == 0x0c;
            if (!bounded_lut && !insn.Bits(80, 1) && insn.Bits(87, 4) == 15 && insn.Bits(81, 3) == 7 &&
                !(opcode == 0x212 && insn.Bits(40, 24))) {
                // [merge-lop3-generic] Any other truth table maps one-to-one onto the Maxwell three-input
                // LOP3.LUT, which uses the same operand-to-table-bit order (A=0xf0, B=0xcc, C=0xaa).
                u32 b = opcode == 0xc12 ? uniform_register(insn.Bits(32, 8)) : insn.Bits(32, 8);
                if (b == ~0U) return fail(index, "native uniform logic source");
                if (opcode == 0x812) {
                    mov_imm(251, insn.words[1]);
                    b = 251;
                }
                if (opcode == 0xa12) {
                    if (!valid_cbuf() || insn.Bits(62, 2))
                        return fail(index, "native constant logic operand");
                    b = materialize(2, 0, 251, false, false);
                }
                emit(0x5be0000000000000ULL | predicate | dest | (u64{insn.Bits(24, 8)} << 8) |
                     (u64{b} << 20) | (u64{lut} << 28) | (u64{insn.Bits(64, 8)} << 39) | (u64{7} << 48));
                break;
            }
            if (!bounded_lut || insn.Bits(80, 1) ||
                insn.Bits(87, 4) != 15 ||
                (opcode == 0x212 && insn.Bits(40, 24))) return fail(index, "native LUT/predicate mode");
            u32 b = opcode == 0xc12 ? uniform_register(insn.Bits(32, 8)) : insn.Bits(32, 8);
            if (b == ~0U) return fail(index, "native uniform logic source");
            if (opcode == 0x812) {
                mov_imm(251, insn.words[1]);
                b = 251;
            }
            if (opcode == 0xa12) {
                if (!valid_cbuf() || insn.Bits(62, 2))
                    return fail(index, "native constant logic operand");
                b = materialize(2, 0, 251, false, false);
            }
            const u32 a = insn.Bits(24, 8), c = insn.Bits(64, 8);
            const auto logical = [&](u32 target, u32 lhs, u32 rhs, u32 operation, u32 pred_target = 7) {
                emit(0x5c40000000000000ULL | predicate | target | (u64{lhs} << 8) |
                     (u64{rhs} << 20) | (u64{operation} << 41) |
                     (u64{pred_target == 7 ? 0U : 3U} << 44) | (u64{pred_target} << 48));
            };
            const u32 pred_target = insn.Bits(81, 3);
            if (lut == 0x3c) {
                logical(static_cast<u32>(dest), a, b, 2, pred_target);
            } else if (lut == 4) {
                logical(250, b, a, 0);
                result.instructions.back() |= u64{1} << 40;
                logical(static_cast<u32>(dest), 250, c, 0, pred_target);
                result.instructions.back() |= u64{1} << 40;
            } else if (lut == 0x30 || lut == 0x0c) {
                logical(static_cast<u32>(dest), lut == 0x0c ? b : a,
                        lut == 0x0c ? a : b, 0, pred_target);
                result.instructions.back() |= u64{1} << 40;
            } else if (lut == 0xe2) {
                logical(250, a, b, 0);
                logical(252, c, b, 0);
                result.instructions.back() |= u64{1} << 40;
                logical(static_cast<u32>(dest), 250, 252, 1, pred_target);
            } else if (lut == 0x30) {
                logical(static_cast<u32>(dest), a, b, 0, pred_target);
                result.instructions.back() |= u64{1} << 40;
            } else if (lut == 0xf4) {
                logical(250, b, c, 0);
                result.instructions.back() |= u64{1} << 40;
                logical(static_cast<u32>(dest), a, 250, 1, pred_target);
            } else if (lut == 0x3c) {
                logical(static_cast<u32>(dest), a, b, 2, pred_target);
            } else if (lut == 0x78) {
                logical(250, b, c, 0);
                logical(static_cast<u32>(dest), a, 250, 2, pred_target);
            } else if (lut == 0xc8) {
                logical(250, a, c, 1);
                logical(static_cast<u32>(dest), 250, b, 0, pred_target);
            } else if (lut == 0xfe) {
                logical(250, a, b, 1);
                logical(static_cast<u32>(dest), 250, c, 1, pred_target);
            } else if (lut == 0xf8) {
                logical(250, b, c, false);
                logical(static_cast<u32>(dest), a, 250, 1, pred_target);
            } else if (lut == 0x48) {
                logical(250, a, c, 2);
                logical(static_cast<u32>(dest), 250, b, 0, pred_target);
            } else {
                logical(static_cast<u32>(dest), a, b, lut == 0xfc ? 1U : 0U, pred_target);
            }
            break;
        }
        case 0x217: case 0x817: case 0xa17: case 0xc17: { // IMNMX register, immediate, constant, or uniform operand.
            if ((insn.words[2] & ~(0x04000200U | (opcode == 0xc17 ? 0x08000000U : 0U))) != 0x03800000 ||
                (opcode != 0x817 && opcode != 0xa17 && insn.words[1] > 255U))
                return fail(index, "native integer min/max mode");
            if (opcode == 0xa17 && (!valid_cbuf() || insn.Bits(62, 2)))
                return fail(index, "native constant min/max operand");
            u32 source = opcode == 0xa17 ? operand(1, false) : opcode == 0xc17 ? uniform_register(insn.words[1]) : insn.words[1];
            const bool small_immediate = opcode == 0x817 && insn.words[1] <= 0x7ffffU;
            if (opcode == 0x817 && !small_immediate) { mov_imm(251, insn.words[1]); source = 251; }
            if (source == ~0U) return fail(index, "native uniform min/max operand");
            emit(base(small_immediate ? 0x3820000000000000ULL : 0x5c20000000000000ULL) |
                 (u64{insn.Bits(24, 8)} << 8) | (u64{source} << 20) |
                 (u64{insn.Bits(87, 4)} << 39) | (u64{insn.Bits(73, 1)} << 48));
            break;
        }
        case 0x419: { // SHF.L.U32.HI with zero low word and immediate high word.
            if (insn.Bits(24, 8) != 255 || (insn.words[2] & ~0x8ffU) != 0x00010600)
                return fail(index, "native immediate-word left shift mode");
            mov_imm(251, insn.words[1]);
            emit(base(0x5c48000000000000ULL) | (u64{251} << 8) |
                 (u64{insn.Bits(64, 8)} << 20) | (u64{insn.Bits(75, 1)} << 39));
            break;
        }
        case 0x819: { // SHF.HI, immediate shift
            const bool is_signed = insn.Bits(73, 2) == 2;
            if (!insn.Bits(80, 1) && insn.Bits(76, 1) && insn.Bits(64, 8) == 255 &&
                insn.Bits(73, 2) == 3 && !insn.Bits(75, 1) && insn.words[1] <= 32) {
                emit(base(0x3828000000000000ULL) | (u64{insn.Bits(24, 8)} << 8) | (u64{insn.words[1]} << 20));
                break;
            }
            if (!insn.Bits(80, 1) && !insn.Bits(76, 1) && insn.Bits(64, 8) == 255 &&
                insn.Bits(73, 2) == 3 && !insn.Bits(75, 1) && insn.words[1] <= 32) {
                if (insn.words[1] == 32) {
                    mov_imm(static_cast<u32>(dest), 0);
                } else {
                    const u64 operation = insn.Bits(76, 1)
                        ? 0x3828000000000000ULL : 0x3848000000000000ULL;
                    emit(base(operation) | (u64{insn.Bits(24, 8)} << 8) |
                         (u64{insn.words[1]} << 20));
                }
                break;
            }
            if ((insn.Bits(73, 2) != 3 && !is_signed) || insn.Bits(75, 1) || !insn.Bits(80, 1) || insn.words[1] > 32) return fail(index, "native funnel-shift mode");
            const u32 low = insn.Bits(24, 8), high = insn.Bits(64, 8), shift = insn.words[1];
            if (insn.Bits(76, 1)) {
                if (low != 255) return fail(index, "native nonzero low right-shift word");
                emit(base(0x3828000000000000ULL) | (u64{high} << 8) | (u64{shift} << 20) |
                     (u64{is_signed} << 48));
            } else {
                if (is_signed) return fail(index, "native signed left funnel shift");
                emit(base(0x36f8000000000000ULL) | (u64{low} << 8) | (u64{high} << 39) | (u64{shift} << 20));
            }
            break;
        }
        case 0xa11: { // LEA with constant base and carry predicate.
            if ((insn.words[2] & ~(0x0780f800U | 0xffU)) == 0x000f0400U && !insn.Bits(62, 2)) {
                const u32 base_high = operand(1, false);
                emit(0x36f8000000000000ULL | predicate | 250 | (u64{insn.Bits(24, 8)} << 8) |
                     (u64{insn.Bits(64, 8)} << 39) | (u64{insn.Bits(75, 5)} << 20));
                mov_imm(252, 1);
                emit(0x5ca0000000000000ULL | predicate | 252 | (u64{252} << 8) | (u64{255} << 20) |
                     (u64{insn.Bits(87, 4)} << 39));
                emit(base(0x5c10000000000000ULL) | (u64{250} << 8) | (u64{base_high} << 20));
                emit(base(0x5c10000000000000ULL) | (dest << 8) | (u64{252} << 20));
                break;
            }
            if ((insn.words[2] & ~(0x07800000U | 0x0000f800U)) == 0x000f06ff && !insn.Bits(62, 2)) {
                const u32 source = insn.Bits(24, 8), base_high = operand(1, false);
                emit(0x3828000000000000ULL | predicate | 250 | (u64{source} << 8) |
                     (u64{32 - insn.Bits(75, 5)} << 20) | (u64{1} << 48));
                mov_imm(252, 1);
                emit(0x5ca0000000000000ULL | predicate | 252 | (u64{252} << 8) | (u64{255} << 20) |
                     (u64{insn.Bits(87, 3)} << 39) | (u64{insn.Bits(90, 1)} << 42));
                emit(base(0x5c10000000000000ULL) | (u64{250} << 8) | (u64{base_high} << 20));
                emit(base(0x5c10000000000000ULL) | (dest << 8) | (u64{252} << 20));
                break;
            }
            if (insn.Bits(80, 1)) {
                if (insn.Bits(72, 1) || insn.Bits(74, 1) || insn.Bits(81, 3) != 7 ||
                    insn.Bits(64, 8) != 255 || insn.Bits(62, 2))
                    return fail(index, "native constant high effective address mode");
                const u32 b = operand(1, false);
                emit(0x3828000000000000ULL | predicate | 250 | (u64{insn.Bits(24, 8)} << 8) |
                     (u64{32 - insn.Bits(75, 5)} << 20));
                emit(base(0x5c10000000000000ULL) | (u64{250} << 8) | (u64{b} << 20));
                break;
            }
            if (insn.Bits(72, 1) || insn.Bits(74, 1) || insn.Bits(80, 1) ||
                insn.Bits(62, 2))
                return fail(index, "native constant LEA carry mode");
            const u32 b = operand(1, false);
            emit(base(0x5c18000000000000ULL) | (u64{insn.Bits(24, 8)} << 8) |
                (u64{b} << 20) | (u64{insn.Bits(75, 5)} << 39));
            if (insn.Bits(81, 3) != 7) emit(0x5b60000000000000ULL | predicate | 7 | (u64{insn.Bits(81, 3)} << 3) |
                (dest << 8) | (u64{b} << 20) | (u64{7} << 39) | (u64{1} << 49));
            break;
        }
        case 0x211: case 0x811: case 0xc11: { // LEA with a register, immediate, or uniform base.
            if (opcode == 0xc11 && ((insn.words[1] & ~0xffU) || !insn.Bits(91, 1)))
                return fail(index, "native uniform effective address mode");
            if (opcode == 0x211 && !(insn.words[1] & ~0xffU) && (insn.words[2] & ~(0x0780f800U | 0xffU)) == 0x000f0400U && !insn.Bits(62, 2)) {
                const u32 base_high = insn.Bits(32, 8);
                emit(0x36f8000000000000ULL | predicate | 250 | (u64{insn.Bits(24, 8)} << 8) |
                     (u64{insn.Bits(64, 8)} << 39) | (u64{insn.Bits(75, 5)} << 20));
                mov_imm(252, 1);
                emit(0x5ca0000000000000ULL | predicate | 252 | (u64{252} << 8) | (u64{255} << 20) |
                     (u64{insn.Bits(87, 4)} << 39));
                emit(base(0x5c10000000000000ULL) | (u64{250} << 8) | (u64{base_high} << 20));
                emit(base(0x5c10000000000000ULL) | (dest << 8) | (u64{252} << 20));
                break;
            }
            if (opcode == 0x211 && (insn.words[2] & ~(0x07800000U | 0x0000f800U)) == 0x000f06ff &&
                !(insn.words[1] & ~0xffU)) {
                const u32 a = insn.Bits(24, 8), b = insn.Bits(32, 8);
                emit(0x3828000000000000ULL | predicate | 250 | (u64{a} << 8) |
                     (u64{32 - insn.Bits(75, 5)} << 20) | (u64{1} << 48));
                mov_imm(252, 1);
                emit(0x5ca0000000000000ULL | predicate | 252 | (u64{252} << 8) | (u64{255} << 20) |
                     (u64{insn.Bits(87, 3)} << 39) | (u64{insn.Bits(90, 1)} << 42));
                emit(base(0x5c10000000000000ULL) | (u64{250} << 8) | (u64{b} << 20));
                emit(base(0x5c10000000000000ULL) | (dest << 8) | (u64{252} << 20));
                break;
            }
            if (insn.Bits(80, 1)) {
                if (insn.Bits(72, 1) || insn.Bits(74, 1) || insn.Bits(81, 3) != 7 ||
                    insn.Bits(64, 8) != 255 || (opcode != 0x811 && insn.Bits(62, 2)))
                    return fail(index, "native high effective address mode");
                const u32 b = opcode == 0xc11 || opcode == 0x811 ? operand(1, false) : insn.Bits(32, 8);
                emit(0x3828000000000000ULL | predicate | 250 | (u64{insn.Bits(24, 8)} << 8) |
                    (u64{32 - insn.Bits(75, 5)} << 20) | (u64{insn.Bits(73, 1)} << 48));
                emit(base(0x5c10000000000000ULL) | (u64{250} << 8) |
                    (u64{b} << 20));
                break;
            }
            if (opcode == 0x211 && (insn.words[2] & ~0x000ef800U) == 0x078000ffU &&
                !(insn.words[1] & ~0xffU) && insn.Bits(81, 3) != 7) {
                emit(0x5c98000000000000ULL | predicate | 252 | (u64{insn.Bits(32, 8)} << 20) | (u64{15} << 39));
                const u32 target = dest == 255 ? 250U : static_cast<u32>(dest);
                emit(0x5c18000000000000ULL | predicate | target | (u64{insn.Bits(24, 8)} << 8) |
                     (u64{252} << 20) | (u64{insn.Bits(75, 5)} << 39));
                emit(0x5b60000000000000ULL | predicate | 7 | (u64{insn.Bits(81, 3)} << 3) |
                     (u64{target} << 8) | (u64{252} << 20) | (u64{7} << 39) | (u64{1} << 49));
                break;
            }
            if (insn.Bits(72, 1) || insn.Bits(74, 1) || insn.Bits(80, 1) || insn.Bits(81, 3) != 7 ||
                (opcode == 0x211 && insn.Bits(62, 2))) return fail(index, "native LEA carry/high mode");
            u32 b = opcode == 0xc11 ? operand(1, false) : insn.Bits(32, 8);
            if (opcode == 0x811) { mov_imm(251, insn.words[1]); b = 251; }
            emit(base(0x5c18000000000000ULL) | (u64{insn.Bits(24, 8)} << 8) |
                 (u64{b} << 20) | (u64{insn.Bits(75, 5)} << 39));
            break;
        }
        case 0x210: case 0x810: case 0xa10: case 0xc10: { // IADD3, including a uniform second source.
            if (opcode == 0xc10 && ((insn.words[1] & ~0x800000ffU) || !insn.Bits(91, 1)))
                return fail(index, "native uniform add source mode");
            if ((insn.words[2] & ~(0x000e0000U | (opcode == 0xc10 ? 0x08000000U : 0U))) == 0x07f1e0ffU && insn.Bits(81, 3) != 7 &&
                (opcode == 0x810 || !insn.Bits(63, 1))) {
                const u32 a = insn.Bits(24, 8);
                if ((a >= 250 && a != 255) || (dest >= 250 && dest != 255) ||
                    (opcode != 0x810 && insn.Bits(63, 1)) ||
                    (opcode == 0x210 && (insn.words[1] & ~0xffU)))
                    return fail(index, "native add carry source mode");
                const u32 b = operand(1, false);
                if (opcode == 0x210 && b >= 250 && b != 255)
                    return fail(index, "native add carry reserved register");
                emit(0x5c98000000000000ULL | predicate | 252 | (u64{15} << 39) | (u64{b} << 20));
                const u32 target = dest == 255 ? 250U : static_cast<u32>(dest);
                emit(0x5c10000000000000ULL | predicate | target | (u64{a} << 8) | (u64{252} << 20));
                emit(0x5b60000000000000ULL | predicate | 7 | (u64{insn.Bits(81, 3)} << 3) |
                     (u64{target} << 8) | (u64{252} << 20) | (u64{7} << 39) | (u64{1} << 49));
                break;
            }
            if (opcode == 0xa10 && insn.Bits(74, 1) && !insn.Bits(62, 2) &&
                (insn.words[2] & ~0x07800000U) == 0x007fe4ffU) {
                const u32 b = operand(1, false);
                mov_imm(250, 1);
                emit(0x5ca0000000000000ULL | predicate | 250 | (u64{250} << 8) | (u64{255} << 20) |
                    (u64{insn.Bits(87, 3)} << 39) | (u64{insn.Bits(90, 1)} << 42));
                emit(base(0x5c10000000000000ULL) | (u64{insn.Bits(24, 8)} << 8) | (u64{b} << 20));
                emit(base(0x5c10000000000000ULL) | (dest << 8) | (u64{250} << 20));
                break;
            }
            const bool extended_add = insn.Bits(74, 1);
            const bool negate_a = insn.Bits(72, 1), negate_b = opcode != 0x810 && insn.Bits(63, 1);
            const bool negate_c = insn.Bits(75, 1);
            const bool outputs_carry = insn.Bits(81, 3) != 7 || insn.Bits(84, 3) != 7;
            if (extended_add || negate_c || insn.Bits(84, 3) != 7 ||
                (outputs_carry && (insn.Bits(64, 8) != 255 || negate_a || negate_b))) {
                const u32 a = insn.Bits(24, 8), c = insn.Bits(64, 8);
                const u32 first_output = insn.Bits(81, 3), second_output = insn.Bits(84, 3);
                if (guarded_uniform || (first_output != 7 && first_output == second_output) ||
                    (opcode == 0xa10 && (insn.Bits(62, 1) || insn.Bits(54, 5) >= 18)) || insn.Bits(73, 1) || insn.Bits(76, 1) ||
                    (a >= 250 && a != 255) || (c >= 250 && c != 255) ||
                    (dest >= 250 && dest != 255) ||
                    (opcode == 0x210 && ((insn.words[1] & ~0x800000ffU) ||
                        (insn.Bits(32, 8) >= 250 && insn.Bits(32, 8) != 255))) ||
                    (!extended_add && (insn.Bits(77, 4) != 15 || insn.Bits(87, 4) != 15)))
                    return fail(index, "native three-input carry mode");
                const auto move = [&](u32 target, u32 source) {
                    emit(0x5c98000000000000ULL | predicate | target | (u64{15} << 39) | (u64{source} << 20));
                };
                const auto invert = [&](u32 target) {
                    emit(0x5c40000000000000ULL | predicate | target | (u64{255} << 8) |
                         (u64{target} << 20) | (u64{1} << 40) | (u64{3} << 41) | (u64{7} << 48));
                };
                const auto accumulate = [&](u32 source) {
                    emit(0x5c10000000000000ULL | predicate | 253 | (u64{253} << 8) |
                         (u64{source} << 20) | (u64{1} << 47));
                    emit(0x5c10000000000000ULL | predicate | 254 | (u64{254} << 8) |
                         (u64{255} << 20) | (u64{1} << 43));
                };
                move(250, a);
                const u32 b = operand(1, false);
                move(251, b);
                move(252, c);
                if (negate_a) invert(250);
                if (negate_b) invert(251);
                if (negate_c) invert(252);
                emit(0x5c10000000000000ULL | predicate | 253 | (u64{250} << 8) |
                     (u64{251} << 20) | (u64{1} << 47));
                emit(0x5c10000000000000ULL | predicate | 254 | (u64{255} << 8) |
                     (u64{255} << 20) | (u64{1} << 43));
                accumulate(252);
                if (extended_add) {
                    for (u32 carry : {insn.Bits(87, 4), insn.Bits(77, 4)}) {
                        if (carry == 15) continue;
                        mov_imm(250, 1);
                        if (carry != 7)
                            emit(0x5ca0000000000000ULL | predicate | 250 | (u64{250} << 8) |
                                 (u64{255} << 20) | (u64{carry & 7} << 39) | (u64{carry >> 3} << 42));
                        accumulate(250);
                    }
                } else {
                    const u32 additions = static_cast<u32>(negate_a) + static_cast<u32>(negate_b) + static_cast<u32>(negate_c);
                    if (additions) { mov_imm(250, additions); accumulate(250); }
                }
                if (dest != 255) move(static_cast<u32>(dest), 253);
                std::array<u32, 2> outputs{0, 1};
                if ((native_predicate & 7) != 7 && first_output == (native_predicate & 7))
                    std::swap(outputs[0], outputs[1]);
                for (u32 output : outputs) {
                    const u32 target = insn.Bits(81 + output * 3, 3);
                    if (target == 7) continue;
                    mov_imm(250, output + 1);
                    emit(0x5b60000000000000ULL | predicate | 7 | (u64{target} << 3) |
                         (u64{254} << 8) | (u64{250} << 20) | (u64{7} << 39) | (u64{6} << 49));
                }
                break;
            }
            if (insn.Bits(64, 8) != 255 && !(insn.Bits(73, 4) & ~4U) && insn.Bits(77, 7) == 0x7f && insn.Bits(84, 7) == 0x7f) {
                const u32 b = operand(1, false);
                emit(0x5c10000000000000ULL | predicate | 250 | (u64{insn.Bits(24, 8)} << 8) |
                    (u64{b} << 20) | (u64{negate_b} << 48) | (u64{insn.Bits(72, 1)} << 49));
                emit(base(0x5c10000000000000ULL) | (u64{250} << 8) | (u64{insn.Bits(64, 8)} << 20) |
                     (u64{insn.Bits(75, 1)} << 48));
                break;
            }
            if (insn.Bits(64, 8) != 255 || insn.Bits(73, 4) || insn.Bits(77, 7) != 0x7f || insn.Bits(84, 7) != 0x7f) return fail(index, "native integer carry/third source");
            if (opcode == 0xa10 || opcode == 0xc10) {
                const u32 b = operand(1, false);
                emit(base(0x5c10000000000000ULL) | (u64{insn.Bits(24, 8)} << 8) | (u64{b} << 20) |
                    (u64{insn.Bits(63, 1)} << 48) | (u64{insn.Bits(72, 1)} << 49));
            } else if (opcode == 0x210 || opcode == 0xc10) {
                if (insn.words[1] & ~0x800000ffU) return fail(index, "native integer add register modifiers");
                const u32 b = opcode == 0xc10 ? operand(1, false) : insn.Bits(32, 8);
                emit(base(0x5c10000000000000ULL) | (u64{insn.Bits(24, 8)} << 8) |
                     (u64{b} << 20) | (u64{insn.Bits(63, 1)} << 48) | (u64{insn.Bits(72, 1)} << 49));
            } else {
                emit(base(0x1c00000000000000ULL) | (u64{insn.Bits(24, 8)} << 8) | (u64{insn.words[1]} << 20) | (u64{insn.Bits(72, 1)} << 56));
            }
            break;
        }
        case 0x949: case 0x958: { // Captured BRX / BRXU with absolute native-PC table values
            if (insn.Bits(12, 4) != 7 || (insn.words[2] & ~0x3ffffU) != (opcode == 0x958 ? 0x0b800000U : 0x03800000U))
                return fail(index, "native computed branch mode");
            const u64 raw = insn.words[1] | (u64{insn.words[2] & 0x3ffff} << 32);
            const s64 relative = static_cast<s64>(raw << 14) >> 14;
            if (s64{index} * 16 + 16 + relative != 0)
                return fail(index, "native computed branch relative base");
            const s64 offset = -s64{address_of(static_cast<u32>(result.instructions.size()))} - 8;
            if (offset < -0x800000) return fail(index, "native computed branch range");
            const u32 source = opcode == 0x958 ? uniform_register(insn.Bits(24, 8)) : insn.Bits(24, 8);
            if (source == ~0U) return fail(index, "native uniform computed branch source");
            emit(0xe25000000000000fULL | predicate | (u64{source} << 8) |
                 ((static_cast<u64>(offset) & 0xffffff) << 20));
            break;
        }
        case 0x944: { // Verified CALL.REL.NOINC with R55 holding the return PC
            if (!main_exited && call_targets.empty() && barrier_tokens.empty() && native_predicate != 7 &&
                insn.words[0] == ((native_predicate << 12) | 0x944U) && insn.words[1] == 16 &&
                insn.words[2] == 0x03c00000 && index + 2 < code.size()) {
                const auto& back = code[index + 1];
                const u64 raw = back.words[1] | (u64{back.words[2] & 0x3ffffU} << 32);
                const s64 target = s64{index + 2} * 16 + (static_cast<s64>(raw << 14) >> 14);
                if (back.words[0] == 0x7947 && (back.words[2] & ~0x3ffffU) == 0x03800000 &&
                    target >= 0 && target <= s64{index} * 16 && target % 16 == 0) {
                    relocations.push_back({static_cast<u32>(result.instructions.size()), index + 2});
                    emit(0xe24000000000000fULL | predicate);
                    break;
                }
            }
            // A predicated NOINC forward transfer can terminate a loop at its
            // reconvergence point without a return-link setup or matching RET.
            // [merge-call-noinc-skip] The same transfer also skips an unconditional backward branch that closes a loop.
            bool skips_loop_branch = false;
            if (index + 2 < code.size() && code[index + 1].words[0] == 0x00007947 &&
                code[index + 1].words[2] == 0x0383ffff) {
                const auto& closing = code[index + 1];
                const u64 closing_raw = closing.words[1] | (u64{closing.words[2] & 0x3ffffU} << 32);
                const s64 closing_target = s64{index + 1} * 16 + 16 + (static_cast<s64>(closing_raw << 14) >> 14);
                skips_loop_branch = closing_target <= s64{index} * 16;
            }
            if (native_predicate != 7 && insn.words[1] == 16 && insn.words[2] == 0x03c00000 &&
                index + 2 < code.size() && code[index + 1].Bits(0, 12) == 0x947 &&
                (code[index + 2].Bits(0, 12) == 0x941 || skips_loop_branch)) {
                relocations.push_back({static_cast<u32>(result.instructions.size()), index + 2});
                emit(0xe24000000000000fULL | predicate);
                break;
            }
            if (!main_exited && native_predicate != 7 &&
                insn.words[0] == ((native_predicate << 12) | 0x944U) &&
                insn.words[1] == 16 && insn.words[2] == 0x03c00000 &&
                index + 2 < code.size() && !barrier_targets.empty()) {
                const auto& back = code[index + 1];
                const u64 raw_back = back.words[1] | (u64{back.words[2] & 0x3ffffU} << 32);
                const s64 relative_back = static_cast<s64>(raw_back << 14) >> 14;
                const s64 back_target = s64{index + 2} * 16 + relative_back;
                const s64 join = barrier_targets.back() / 16 - 1;
                s64 scope_start = -1;
                for (u32 previous = index; previous > 0; --previous) {
                    const auto& setup = code[previous - 1];
                    if (setup.words[0] == ((barrier_tokens.back() << 16) | 0x7945U) &&
                        s64{previous} * 16 + static_cast<s32>(setup.words[1]) == barrier_targets.back()) {
                        scope_start = s64{previous} * 16;
                        break;
                    }
                }
                bool cleanup = back.words[0] == 0x7947 &&
                    (back.words[2] & ~0x3ffffU) == 0x03800000 &&
                    scope_start >= 0 && back_target >= scope_start && back_target % 16 == 0 && back_target < s64{index} * 16 &&
                    join >= index + 2 && join < static_cast<s64>(code.size()) &&
                    code[join].words[0] == ((barrier_tokens.back() << 16) | 0x7941U) &&
                    code[join].words[1] == 0 && code[join].words[2] == 0x03800000;
                for (s64 cursor = index + 2; cleanup && cursor < join;) {
                    const auto& move = code[cursor];
                    if ((move.words[0] & ~0x00ff0000U) == 0x7202 &&
                        move.words[1] <= 255 && move.words[2] == 0xf00) {
                        ++cursor;
                    } else if (const u32 op = move.Bits(0, 12);
                               op == 0x308 || op == 0xb08 ||
                               op == 0x220 || op == 0x820 || op == 0xa20 || op == 0xc20 ||
                               op == 0x223 || op == 0x423 || op == 0x623 || op == 0x823 ||
                               op == 0xa23 || op == 0xc23 || op == 0xe23 ||
                               op == 0x221 || op == 0x421 || op == 0x621 || op == 0x821 ||
                               op == 0xa21 || op == 0xe21) {
                        ++cursor;
                    } else if (move.words[0] == 0x7947 && move.words[2] == 0x03800000) {
                        const s64 target = cursor + 1 + s64{move.words[1]} / 16;
                        if (move.words[1] % 16 || target <= cursor || target > join) cleanup = false;
                        else cursor = target;
                    } else cleanup = false;
                }
                if (cleanup) {
                    relocations.push_back({static_cast<u32>(result.instructions.size()), index + 2});
                    emit(0xe24000000000000fULL | predicate);
                    break;
                }
            }
            const bool adjacent_link = !(main_exited || index == 0 || insn.words[0] != 0x7944 ||
                (insn.words[2] & ~0x3ffffU) != 0x03c00000 ||
                (code[index - 1].words[0] & ~0x00ff0000U) != 0x00007802 ||
                code[index - 1].Bits(16, 8) >= 250 ||
                code[index - 1].words[1] != (index + 1) * 16 ||
                code[index - 1].words[2] != 0xf00);
            u32 link = adjacent_link ? code[index - 1].Bits(16, 8) : 0;
            if (!adjacent_link) {
                // The return address may be set earlier, provided it is the same immediate, the link
                // register is not written afterwards, and nothing can enter between setup and call.
                if (main_exited || insn.words[0] != 0x7944 || (insn.words[2] & ~0x3ffffU) != 0x03c00000)
                    return fail(index, "native call linkage mode");
                u32 setup = ~0U;
                for (u32 cursor = index; cursor > 0; --cursor) {
                    const auto& move = code[cursor - 1];
                    if ((move.words[0] & ~0x00ff0000U) == 0x00007802 && move.Bits(16, 8) < 250 &&
                        move.words[1] == (index + 1) * 16 && move.words[2] == 0xf00) {
                        setup = cursor - 1;
                        break;
                    }
                }
                if (setup == ~0U) return fail(index, "native call linkage mode");
                link = code[setup].Bits(16, 8);
                for (u32 cursor = setup + 1; cursor < index; ++cursor) {
                    if (writes_link(code[cursor], link)) return fail(index, "native call linkage mode");
                }
                if (entered_from_outside(setup, index)) return fail(index, "native call linkage mode");
            }
            const u64 raw = insn.words[1] | (u64{insn.words[2] & 0x3ffff} << 32);
            const s64 relative = static_cast<s64>(raw << 14) >> 14;
            const s64 target = s64{index} * 16 + 16 + relative;
            if (target < 0 || target % 16) return fail(index, "native call destination alignment");
            for (size_t call = 0; call < call_targets.size(); ++call) {
                if (call_targets[call] == target / 16 && call_links[call] != link)
                    return fail(index, "native inconsistent call linkage");
            }
            call_targets.push_back(static_cast<u32>(target / 16));
            call_links.push_back(link);
            relocations.push_back({static_cast<u32>(result.instructions.size()), target / 16, true});
            emit(0xe26000000007000fULL);
            break;
        }
        case 0x950: { // Verified RET.REL.NODEC epilogue restoring the call link
            const u32 return_reg = insn.Bits(24, 8);
            bool inplace_link = active_link && return_reg == *active_link;
            if (inplace_link) {
                for (u32 previous = active_entry; previous + 1 < index; ++previous) {
                    const auto& body = code[previous];
                    const u32 op = body.Bits(0, 12);
                    if (op == 0x20b || op == 0x80b || op == 0xa0b || op == 0x918) continue;
                    if (op == 0x947) {
                        const u64 raw = body.words[1] | (u64{body.words[2] & 0x3ffffU} << 32);
                        const s64 target = s64{previous + 1} * 16 + (static_cast<s64>(raw << 14) >> 14);
                        if (target >= s64{active_entry} * 16 && target <= s64{index - 1} * 16) continue;
                        inplace_link = false;
                        break;
                    }
                    if ((op != 0x202 && op != 0x802 && op != 0x221 && op != 0x421 &&
                         op != 0x820 && op != 0x223 && op != 0x308 && op != 0x220 && op != 0x423 &&
                         op != 0x224 && op != 0x424 && op != 0x824 && op != 0x225 && op != 0x227 &&
                         op != 0x210 && op != 0x810 && op != 0x311 && op != 0x312 &&
                         op != 0x20c && op != 0x207 && op != 0x807 &&
                         op != 0xe24 && op != 0x348 && op != 0x589) ||
                        writes_link(body, return_reg)) {
                        inplace_link = false;
                        break;
                    }
                }
            }
            u32 upper_link_setup = index - 1;
            bool upper_link_ready = index > 0 &&
                code[index - 1].words[0] == (0x00007802U | ((return_reg + 1) << 16)) &&
                code[index - 1].words[1] == 0 && code[index - 1].words[2] == 0xf00;
            if (inplace_link) {
                upper_link_ready = false;
                for (u32 cursor = index; cursor > active_entry; --cursor) {
                    const auto& body = code[cursor - 1];
                    if (!writes_link(body, return_reg + 1)) continue;
                    upper_link_setup = cursor - 1;
                    upper_link_ready = body.words[1] == 0 &&
                        ((body.words[0] == (0x7802U | ((return_reg + 1) << 16)) && body.words[2] == 0xf00) ||
                         (body.words[0] == (0xff007424U | ((return_reg + 1) << 16)) && body.words[2] == 0x078e00ff));
                    break;
                }
            }
            // The callee may copy the link into the return register earlier than the instruction before
            // the RET, as long as nothing writes it again and nothing can enter after the copy.
            bool moved_link = false;
            if (main_exited && index >= 2 && active_link && !inplace_link && return_reg < 249 &&
                !(code[index - 2].words[0] == (0x00007202U | (return_reg << 16)) &&
                  code[index - 2].words[1] == *active_link && code[index - 2].words[2] == 0xf00 && upper_link_ready)) {
                u32 copy = ~0U, upper = ~0U;
                for (u32 cursor = index; cursor > active_entry; --cursor) {
                    if (copy == ~0U && writes_link(code[cursor - 1], return_reg)) copy = cursor - 1;
                    if (upper == ~0U && writes_link(code[cursor - 1], return_reg + 1)) upper = cursor - 1;
                }
                if (copy != ~0U && upper != ~0U && code[copy].words[0] == (0x00007202U | (return_reg << 16)) &&
                    code[copy].words[1] == *active_link && code[copy].words[2] == 0xf00 && code[upper].words[1] == 0 &&
                    ((code[upper].words[0] == (0x7802U | ((return_reg + 1) << 16)) && code[upper].words[2] == 0xf00) ||
                     (code[upper].words[0] == (0xff007424U | ((return_reg + 1) << 16)) &&
                      code[upper].words[2] == 0x078e00ff)) &&
                    !entered_from_outside(std::min(copy, upper), index)) {
                    moved_link = true;
                    upper_link_ready = true;
                }
            }
            if (!main_exited || index < 2 || !active_link ||
                (insn.words[0] & 0x00ffffffU) != 0x00007950 || return_reg >= 249 ||
                (insn.words[2] & ~0x3ffffU) != 0x03c00000 ||
                (!inplace_link && !moved_link && (code[index - 2].words[0] != (0x00007202U | (return_reg << 16)) ||
                 code[index - 2].words[1] != *active_link || code[index - 2].words[2] != 0xf00)) ||
                !upper_link_ready || !barrier_tokens.empty())
                return fail(index, "native return linkage mode");
            const u64 raw = insn.words[1] | (u64{insn.words[2] & 0x3ffff} << 32);
            const s64 relative = static_cast<s64>(raw << 14) >> 14;
            if (s64{index} * 16 + 16 + relative != 0)
                return fail(index, "native return relative base");
            for (const auto& relocation : relocations) {
                if ((inplace_link && relocation.target > upper_link_setup && relocation.target < index &&
                     relocation.output_index < result.source_instruction_offsets[upper_link_setup]) ||
                    (inplace_link && relocation.target > active_entry && relocation.target < index &&
                     relocation.output_index < result.source_instruction_offsets[active_entry]) ||
                    relocation.target == index ||
                    (relocation.target == index - 1 && (!inplace_link ||
                     relocation.output_index < result.source_instruction_offsets[active_entry])))
                    return fail(index, "native branch bypasses return linkage");
            }
            emit(0xe32000000007000fULL);
            bool complete = true;
            for (u32 target : call_targets) complete &= target <= index;
            if (complete) return finish(index);
            break;
        }
        case 0x348: // WARPSYNC with a register lane mask: lanes are emulated independently.
            if (insn.words[2] != 0x03800000 || insn.words[1] > 254)
                return fail(index, "native warp sync mode");
            emit(0x50b0000000000f00ULL | predicate);
            break;
        case 0x845: case 0x906: { // I2FP.F32 / I2F.F32 of a 32-bit immediate, folded to the exact float.
            const bool is_signed = insn.Bits(74, 1);
            if ((insn.words[2] & ~(0xc000U | 0x400U)) != 0x00201000 || dest == 255)
                return fail(index, "native integer immediate conversion mode");
            const s64 value = is_signed ? s64{static_cast<s32>(insn.words[1])} : s64{insn.words[1]};
            if (value > (1 << 24) || value < -(1 << 24))
                return fail(index, "native integer immediate conversion range");
            mov_imm(static_cast<u32>(dest), std::bit_cast<u32>(static_cast<float>(value)));
            break;
        }
        case 0x948: { // Full-mask warp sync at an existing outer reconvergence point.
            const bool full_mask_sync = insn.words[0] == 0x7948 && insn.words[1] == 0xffffffff &&
                insn.words[2] == 0x03800000;
            if (!full_mask_sync || index == 0 ||
                code[index - 1].words[0] != ((last_outer_sync_token << 16) | 0x7941) || !barrier_tokens.empty() ||
                last_outer_sync_target != s64{index} * 16) {
                // [merge-warpsync-bare] A full-mask WARPSYNC outside every reconvergence scope that no
                // branch or call reaches has nothing to join; lanes are emulated together.
                bool reached = full_mask_sync ? !barrier_tokens.empty() : true;
                for (u32 previous = 0; !reached && previous < code.size(); ++previous) {
                    const auto& branch = code[previous];
                    const u32 op = branch.Bits(0, 12);
                    if (op == 0x949 || op == 0x958) { reached = true; break; }
                    if (op != 0x945 && op != 0x947 && op != 0x944) continue;
                    const u64 raw = branch.words[1] | (u64{branch.words[2] & 0x3ffff} << 32);
                    const s64 relative = op == 0x945 ? static_cast<s32>(branch.words[1]) :
                                         static_cast<s64>(raw << 14) >> 14;
                    if (s64{previous + 1} * 16 + relative == s64{index} * 16) reached = true;
                }
                if (reached) return fail(index, "native warp synchronization mode");
                emit(0x50b0000000000f00ULL | predicate);
                break;
            }
            for (u32 previous = 0; previous < code.size(); ++previous) {
                const auto& branch = code[previous];
                const u32 op = branch.Bits(0, 12);
                // Computed targets are checked when resolving the native address map.
                if (op != 0x945 && op != 0x947 && op != 0x944) continue;
                const u64 raw = branch.words[1] | (u64{branch.words[2] & 0x3ffff} << 32);
                const s64 relative = op == 0x945 ? static_cast<s32>(branch.words[1]) :
                                     static_cast<s64>(raw << 14) >> 14;
                const s64 target = s64{previous + 1} * 16 + relative;
                if (target != s64{index} * 16) continue;
                if (op != 0x945 || branch.Bits(16, 4) != last_outer_sync_token)
                    return fail(index, "native warp synchronization bypass");
            }
            // SSY/SYNC already joins the lanes; retain memory ordering as well.
            emit(0xef98000000000000ULL | predicate | (u64{1} << 8));
            break;
        }
        case 0x942: { // Cancel every inner scope before an exit that reaches the outer sync.
            if (std::ranges::find(validated_barrier_cancellations, index) != validated_barrier_cancellations.end()) break;
            if (barrier_tokens.size() < 2) return fail(index, "native barrier cancellation mode");
            if (!barrier_sync_scopes.back() && barrier_sync_scopes[barrier_sync_scopes.size() - 2] &&
                index + 1 < code.size() && insn.words[0] ==
                    ((barrier_tokens.back() << 16) | (native_predicate << 12) | 0x942) &&
                insn.words[1] == 0 && insn.words[2] == 0x03800000) {
                const s64 join = barrier_targets[barrier_targets.size() - 2] - 16;
                const s64 inner_join = barrier_targets.back() - 16;
                const bool split_exit = (native_predicate & 7) != 7 &&
                    branches_to(index + 1, native_predicate ^ 8U, inner_join) &&
                    branches_to(index + 2, 7, join);
                if ((branches_to(index + 1, native_predicate, join) || split_exit) &&
                    join >= barrier_targets.back() && join % 16 == 0 &&
                    static_cast<u64>(join / 16) < code.size() &&
                    code[join / 16].words[0] == ((barrier_tokens[barrier_tokens.size() - 2] << 16) | 0x7941) &&
                    code[join / 16].words[1] == 0 && code[join / 16].words[2] == 0x03800000) {
                    // The parent SYNC discards this canceled inner PBK token.
                    break;
                }
            }
            const u32 count = static_cast<u32>(barrier_tokens.size() - 1);
            if (index + count >= code.size()) return fail(index, "native barrier cancellation bounds");
            for (u32 i = 0; i < count; ++i) {
                const auto& cancel = code[index + i];
                if (cancel.words[0] != ((barrier_tokens[count - i] << 16) | (native_predicate << 12) | 0x942) ||
                    cancel.words[1] || cancel.words[2] != 0x03800000 || barrier_sync_scopes[count - i])
                    return fail(index, "native barrier cancellation mode");
            }
            const auto prove_exit_path = [&]() {
                const u32 first = index + count;
                const auto& entry = code[first];
                if (entry.Bits(0, 12) != 0x947 ||
                    (entry.Bits(12, 4) != native_predicate &&
                     entry.Bits(12, 4) != (native_predicate ^ 8U))) return false;
                const s64 outer_join = barrier_targets.front() / 16 - 1;
                if (outer_join < 0 || static_cast<u64>(outer_join) >= code.size()) return false;
                struct State { u32 pc, known, values; std::vector<u32> frames; };
                State initial{first, 0, 0, {}};
                if ((native_predicate & 7) != 7) {
                    initial.known = 1U << (native_predicate & 7);
                    initial.values = (native_predicate & 8) ? 0 : initial.known;
                }
                for (u32 scope = 1; scope < barrier_tokens.size(); ++scope)
                    initial.frames.push_back(0x80000000U |
                        ((static_cast<u32>(barrier_targets[scope] / 16) - 1) << 4) | barrier_tokens[scope]);
                std::vector<State> pending{initial};
                std::set<std::vector<u32>> seen;
                bool reached_join = false;
                const auto constrain = [](State& state, u32 pred, bool taken) {
                    const u32 reg = pred & 7;
                    const bool value = taken != ((pred & 8) != 0);
                    if (reg == 7) return value;
                    const u32 bit = 1U << reg;
                    if ((state.known & bit) && ((state.values & bit) != 0) != value) return false;
                    state.known |= bit;
                    state.values = (state.values & ~bit) | (value ? bit : 0U);
                    return true;
                };
                while (!pending.empty()) {
                    State state = std::move(pending.back()); pending.pop_back();
                    std::vector<u32> key{state.pc, state.known, state.values};
                    key.insert(key.end(), state.frames.begin(), state.frames.end());
                    if (!seen.insert(std::move(key)).second) continue;
                    if (seen.size() > 65536 || state.pc > outer_join || state.pc <= index) return false;
                    if (state.pc == outer_join) {
                        if (std::any_of(state.frames.begin(), state.frames.end(), [](u32 frame) { return !(frame & 0x80000000U); }))
                            return false;
                        reached_join = true;
                        continue;
                    }
                    const auto& op = code[state.pc];
                    const u32 kind = op.Bits(0, 12), pred = op.Bits(12, 4);
                    if (kind == 0x947 || kind == 0x942) {
                        State skipped = state;
                        if (constrain(skipped, pred, false)) { ++skipped.pc; pending.push_back(std::move(skipped)); }
                        if (!constrain(state, pred, true)) continue;
                        if (kind == 0x947) {
                            if (op.words[2] != 0x03800000) return false;
                            const s64 target = s64{state.pc + 1} + static_cast<s32>(op.words[1]) / 16;
                            if (op.words[1] % 16 || target <= state.pc || target > outer_join) return false;
                            state.pc = static_cast<u32>(target);
                        } else {
                            if (op.words[1] || op.words[2] != 0x03800000 || state.frames.empty() ||
                                (state.frames.back() & 15) != op.Bits(16, 4)) return false;
                            state.frames.back() |= 0x80000000U;
                            ++state.pc;
                        }
                    } else if (kind == 0x945) {
                        if (pred != 7 || state.frames.size() >= 15 ||
                            std::ranges::find(cancellation_parent_scopes, state.pc) != cancellation_parent_scopes.end()) return false;
                        const s64 join = s64{state.pc} + static_cast<s32>(op.words[1]) / 16;
                        if (op.words[1] % 16 || join <= state.pc || join >= outer_join) return false;
                        state.frames.push_back((static_cast<u32>(join) << 4) | op.Bits(16, 4));
                        ++state.pc;
                    } else if (kind == 0x941) {
                        if (pred != 7 || state.frames.empty() || (state.frames.back() & 0x80000000U) ||
                            (state.frames.back() & 15) != op.Bits(16, 4) ||
                            ((state.frames.back() & 0x7fffffffU) >> 4) != state.pc) return false;
                        state.frames.pop_back(); ++state.pc;
                    } else {
                        if (kind == 0x944 || kind == 0x949 || kind == 0x958 || kind == 0x950 ||
                            kind == 0x94d || kind == 0x948) return false;
                        const u32 family = kind & 0x1ff;
                        if (family == 0x0b || family == 0x0c || kind == 0x81c || family == 0x12 ||
                            family == 0x10 || family == 0x11 || family == 0x24 || kind == 0x806 ||
                            kind == 0xf89 || kind == 0x589 || kind == 0x3a1) {
                            for (u32 bit : {81U, 84U}) {
                                const u32 target = op.Bits(bit, 3);
                                if (target < 7) { state.known &= ~(1U << target); state.values &= state.known; }
                            }
                        }
                        ++state.pc;
                    }
                    pending.push_back(std::move(state));
                }
                return reached_join;
            };
            const auto prove_linear_exit = [&]() {
            u32 branch_index = index + count;
            const auto& move = code[branch_index];
            const bool immediate_move = move.Bits(0, 12) == 0x424 && move.Bits(24, 8) == 255 &&
                                        move.words[2] == 0x078e00ff;
            const bool register_move = (move.Bits(0, 12) == 0x202 && move.words[2] == 0xf00) ||
                (move.Bits(0, 12) == 0x224 && move.Bits(24, 8) == 255 &&
                 move.words[1] == 255 && move.words[2] == 0x078e00ff);
            if (immediate_move || register_move) {
                if (move.Bits(12, 4) != native_predicate) return false;
                ++branch_index;
            }
            if (branch_index >= code.size()) return false;
            bool split_exit = false;
            if (count == 1 && native_predicate != 7 && branch_index + 1 < code.size()) {
                const auto& resume = code[branch_index];
                if (resume.words[0] == (((native_predicate ^ 8U) << 12) | 0x947) &&
                    resume.words[2] == 0x03800000 &&
                    s64{branch_index + 1} * 16 + static_cast<s32>(resume.words[1]) == barrier_targets.back() - 16 &&
                    code[branch_index + 1].words[0] == 0x7947) {
                    split_exit = true;
                    ++branch_index;
                }
            }
            const auto& branch = code[branch_index];
            if (branch.Bits(0, 12) != 0x947 || branch.Bits(12, 4) != (split_exit ? 7U : native_predicate) ||
                branch.words[2] != 0x03800000)
                return false;
            const s64 target = s64{branch_index + 1} * 16 + static_cast<s32>(branch.words[1]);
            const s64 join = barrier_targets.front() - 16;
            const s64 inner_end = *std::max_element(barrier_targets.begin() + 1, barrier_targets.end());
            if (target < inner_end || target > join || target % 16 || join < 0 || join % 16 ||
                static_cast<u64>(join / 16) >= code.size() ||
                code[join / 16].words[0] != ((barrier_tokens.front() << 16) | 0x7941) ||
                code[join / 16].words[1] || code[join / 16].words[2] != 0x03800000 ||
                (barrier_tokens.size() == 2 && target != join && !split_exit))
                return false;
            for (s64 at = (split_exit ? target : inner_end) / 16; target != join && at < join / 16; ++at) {
                const auto& exit = code[at];
                const u32 op = exit.Bits(0, 12);
                if (op == 0x947) {
                    const u64 raw = exit.words[1] | (u64{exit.words[2] & 0x3ffff} << 32);
                    const s64 destination = (at + 1) * 16 + (static_cast<s64>(raw << 14) >> 14);
                    if (destination < inner_end || destination > join ||
                        (split_exit && (exit.Bits(12, 4) != 7 || destination <= at * 16)))
                        return false;
                    if (split_exit) at = destination / 16 - 1;
                } else if (op == 0x945 || op == 0x941 || op == 0x942 || op == 0x944 ||
                           op == 0x949 || op == 0x958 || op == 0x950 || op == 0x94d) {
                    return false;
                }
            }
                return true;
            };
            if (!prove_linear_exit() && !prove_exit_path())
                return fail(index, "native barrier cancellation exit path");
            for (u32 i = 0; i < count; ++i) validated_barrier_cancellations.push_back(index + i);
            // The outer SSY pop discards the canceled inner PBK tokens on this path.
            break;
        }
        case 0x947: case 0x945: case 0x941: { // BRA / BSSY B0 / BSYNC B0
            if (opcode == 0x947 && (insn.words[2] & ~0x3ffffU) == 0x0b800000U) {
                // BRA.DIV Ur: the warp-uniformity shortcut around a library shuffle (VOTEU, BRA.DIV, inline
                // SHFL or call). Lanes are emulated together, so the shortcut is always taken; the
                // fall-through call computes the same value.
                if (insn.words[0] != ((insn.Bits(24, 8) << 24) | 0x7947U) || insn.Bits(24, 8) >= 63 ||
                    (insn.words[1] & 3) != 3)
                    return fail(index, "native divergence branch mode");
                insn.words[0] = 0x7947;
                insn.words[1] &= ~3U;
                insn.words[2] = 0x03800000 | (insn.words[2] & 0x3ffffU);
            }
            u64 branch_predicate = predicate;
            if (opcode == 0x947 && insn.Bits(87, 4) != 7) {
                if (insn.Bits(12, 4) != 7) {
                    relocations.push_back({static_cast<u32>(result.instructions.size()), index + 1});
                    emit(0xe24000000000000fULL | (u64{insn.Bits(12, 4) ^ 8U} << 16));
                }
                branch_predicate = u64{insn.Bits(87, 4)} << 16;
            } else if (insn.Bits(87, 4) != 7) {
                return fail(index, "native extra branch condition");
            }
            if (opcode == 0x941) {
                if (barrier_tokens.empty() || barrier_tokens.back() != insn.Bits(16, 4) || insn.Bits(12, 4) != 7) return fail(index, "native reconvergence token");
                const bool inner = !barrier_sync_scopes.back();
                if (barrier_tokens.size() == 1) {
                    last_outer_sync_target = barrier_targets.back();
                    last_outer_sync_token = barrier_tokens.back();
                }
                barrier_targets.pop_back();
                barrier_tokens.pop_back();
                barrier_sync_scopes.pop_back();
                emit(inner ? 0xe34000000007000fULL : 0xf0f800000007000fULL);
                break;
            }
            if (opcode == 0x945) {
                if (barrier_tokens.size() >= 16U ||
                    std::find(barrier_tokens.begin(), barrier_tokens.end(), insn.Bits(16, 4)) != barrier_tokens.end() ||
                    insn.Bits(12, 4) != 7) return fail(index, "native nested reconvergence token");
                barrier_sync_scopes.push_back(!has_barrier_break || barrier_tokens.empty() ||
                    std::ranges::find(cancellation_parent_scopes, index) != cancellation_parent_scopes.end());
                barrier_tokens.push_back(insn.Bits(16, 4));
                barrier_targets.push_back(s64{index + 1} * 16 + static_cast<s32>(insn.words[1]));
            }
            s64 relative = static_cast<s32>(insn.words[1]);
            if (opcode == 0x947) {
                const u64 raw = insn.words[1] | (u64{insn.words[2] & 0x3ffff} << 32);
                relative = static_cast<s64>(raw << 14) >> 14;
            }
            const s64 target = s64{index} * 16 + 16 + relative;
            if (target < 0 || target % 16) return fail(index, "native branch destination alignment");
            relocations.push_back({static_cast<u32>(result.instructions.size()), target / 16});
            emit(opcode == 0x947 ? 0xe24000000000000fULL | branch_predicate :
                 !barrier_sync_scopes.back() ? 0xe2a000000007000fULL :
                 0xe29000000007000fULL);
            break;
        }
        case 0x94d: { // EXIT
            if (insn.Bits(84, 2)) return fail(index, "native special EXIT mode");
            const u32 exit_condition = insn.Bits(87, 4);
            if (exit_condition != 7 && native_predicate != 7) {
                relocations.push_back({static_cast<u32>(result.instructions.size()), index + 1});
                emit(0xe24000000000000fULL | (u64{native_predicate ^ 8U} << 16));
            }
            emit(0xe30000000000000fULL |
                 (exit_condition == 7 ? predicate : u64{exit_condition} << 16));
            if (native_predicate != 7 || exit_condition != 7) break;
            if (!barrier_tokens.empty()) return fail(index, "native unclosed reconvergence scope");
            if (call_targets.empty()) {
                const bool pending_branch = std::any_of(relocations.begin(), relocations.end(),
                    [&](const Relocation& relocation) { return relocation.target > index; });
                if (!pending_branch) {
                    // A captured BRX table can target blocks after this EXIT; the capture only extends past the
                    // first EXIT for such programs, so lower the rest instead of finishing here.
                    if (index + 1 < code.size() && has_indirect_branch) break;
                    return finish(index);
                }
                break;
            }
            const bool pending_main_branch = std::any_of(relocations.begin(), relocations.end(),
                [&](const Relocation& relocation) { return !relocation.call && relocation.target > index; });
            if (pending_main_branch) break;
            if (main_exited) return fail(index, "native library exits without returning");
            for (u32 target : call_targets) {
                if (target <= index) return fail(index, "native call target overlaps main program");
            }
            main_exited = true;
            break;
        }
        default:
            return fail(index, "unsupported native opcode");
        }
        if (!pressure.prefix.empty() && (original_opcode == 0x231 || original_opcode == 0x431 ||
            original_opcode == 0x631 || original_opcode == 0x831 || original_opcode == 0xa31)) {
            const u32 first = result.source_instruction_offsets[index] + static_cast<u32>(pressure.prefix.size());
            for (auto& relocation : relocations)
                if (!relocation.call && relocation.output_index >= first && relocation.target == index + 1)
                    relocation.output_target = static_cast<u32>(result.instructions.size());
        }
        result.instructions.insert(result.instructions.end(), pressure.suffix.begin(), pressure.suffix.end());
        if (!operand_error.empty()) return fail(index, operand_error);
        if (guarded_uniform) restore_predicate();
        if (uniform_predicate_bank) {
            emit(0x38e8000000070000ULL | 239 | (u64{239} << 8) | (u64{0x7f} << 20));
            emit(0x38f0000000070000ULL | (u64{238} << 8) | (u64{0x7f} << 20));
        }
    }
    return fail(static_cast<u32>(code.size()), main_exited ? "native program lacks captured library return" : "native program has no unconditional EXIT");
}

} // namespace Shader::Ampere
