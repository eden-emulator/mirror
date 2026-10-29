// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <functional>
#include <optional>
#include <span>
#include "shader_recompiler/frontend/ampere/instruction.h"

namespace Shader::Ampere {
using TrapGuardReader = std::function<std::optional<u32>(u32, u32)>;

// Evaluate only a bounded, uniform prologue. Unknown inputs or reachable traps reject the dispatch.
inline bool VerifyTrapGuards(std::span<const Instruction> prefix, const TrapGuardReader& read) {
    if (prefix.empty()) return true;
    if (prefix.size() > 256 || !read) return false;
    std::array<std::optional<u32>, 256> regs{};
    std::array<std::optional<bool>, 8> predicates{};
    regs[255] = 0;
    predicates[7] = true;
    const auto pred = [&](u32 code) -> std::optional<bool> {
        const auto value = predicates[code & 7];
        if (!value) return std::nullopt;
        return *value != bool(code & 8);
    };
    for (u32 pc = 0, steps = 0; pc < prefix.size(); ++steps) {
        if (steps >= 256) return false;
        const auto& insn = prefix[pc];
        const u32 op = insn.Bits(0, 12), dest = insn.Bits(16, 8);
        const auto execute = pred(insn.Bits(12, 4));
        if (!execute) return false;
        if (!*execute) { ++pc; continue; }
        const auto cbuf = [&]() -> std::optional<u32> {
            if (insn.Bits(59, 5) || insn.Bits(38, 16) % 4) return std::nullopt;
            return read(insn.Bits(54, 5), insn.Bits(38, 16));
        };
        const auto operand = [&](u32 which) -> std::optional<u32> {
            if (which == 0) return regs[insn.Bits(24, 8)];
            const u32 form = insn.Bits(9, 3);
            if (form == 1) return regs[insn.Bits(32, 8)];
            if (form == 4) return insn.words[1];
            if (form == 5) return cbuf();
            return std::nullopt;
        };
        std::optional<u32> value;
        if (op == 0x95c) return false;
        if (op == 0x947) {
            if ((insn.words[0] & 0xffff0000U) || insn.words[2] != 0x03800000 || insn.words[1] % 16) return false;
            const s64 target = s64{pc} + 1 + static_cast<s32>(insn.words[1]) / 16;
            if (target <= pc) return false;
            if (target >= static_cast<s64>(prefix.size())) return true;
            pc = static_cast<u32>(target);
            continue;
        }
        if (op == 0x388) {
            if (insn.words[2] != 0) return false;
            ++pc;
            continue;
        }
        if (op == 0x624 || op == 0x424 || op == 0x224) {
            if (insn.Bits(24, 8) != 255 || insn.Bits(64, 8) != 255 || insn.words[2] != 0x078e00ff)
                return false;
            if (op == 0x624) value = cbuf();
            else if (op == 0x424) value = insn.words[1];
            else value = regs[insn.Bits(32, 8)];
        } else if (op == 0x207 || op == 0x807) {
            if (insn.words[2] & ~0x07800000U) return false;
            const auto select = pred(insn.Bits(87, 4));
            if (!select) return false;
            value = *select ? operand(0) : operand(1);
        } else if (op == 0x212 || op == 0x812 || op == 0xa12) {
            if ((insn.words[2] & ~0xffffU) != 0x078e0000 ||
                (op != 0x812 && insn.Bits(62, 2)) || (op == 0x212 && (insn.words[1] & ~0xffU))) return false;
            const auto a = operand(0), b = operand(1), c = regs[insn.Bits(64, 8)];
            if (!a || !b || !c) return false;
            u32 result{};
            const u32 lut = insn.Bits(72, 8);
            for (u32 bit = 0; bit < 32; ++bit) {
                const u32 index = (((*a >> bit) & 1) << 2) | (((*b >> bit) & 1) << 1) | ((*c >> bit) & 1);
                result |= ((lut >> index) & 1) << bit;
            }
            value = result;
        } else if (op == 0x210 || op == 0x810 || op == 0xa10) {
            if (insn.words[2] != 0x07ffe0ff || (op != 0x810 && insn.Bits(62, 2))) return false;
            const auto a = operand(0), b = operand(1);
            if (!a || !b) return false;
            value = *a + *b;
        } else if (op == 0x20c || op == 0x80c || op == 0xa0c) {
            if ((insn.words[2] & ~0x078e7e00U) != 0x00700070 || insn.Bits(74, 2) > 2 ||
                (op != 0x80c && insn.Bits(62, 2)) || (op == 0x20c && (insn.words[1] & ~0xffU))) return false;
            const auto a = operand(0), b = operand(1);
            const auto condition = pred(insn.Bits(87, 4));
            if (!a || !b || !condition) return false;
            const bool signed_compare = insn.Bits(73, 1);
            const bool less = signed_compare ? static_cast<s32>(*a) < static_cast<s32>(*b) : *a < *b;
            const bool equal = *a == *b;
            const std::array<bool, 8> comparisons{false, less, equal, less || equal, !less && !equal, !equal, !less, true};
            const bool comparison = comparisons[insn.Bits(76, 3)];
            const u32 combine = insn.Bits(74, 2), target = insn.Bits(81, 3);
            if (target != 7) predicates[target] = combine == 0 ? comparison && *condition :
                combine == 1 ? comparison || *condition : comparison != *condition;
            ++pc;
            continue;
        } else return false;
        if (!value) return false;
        if (dest != 255) regs[dest] = value;
        ++pc;
    }
    return true;
}
} // namespace Shader::Ampere
