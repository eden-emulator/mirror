// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// Instruction semantics adapted from Mesa mme_tu104_sim.c and mme_tu104.c:
// Copyright © 2022 Collabora Ltd. SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <span>
#include <string_view>

namespace Tegra {

// TU104/GA10x MME has two ALUs and two outputs per 96-bit instruction.
class TuringMacroInterpreter {
public:
    using Word = std::uint32_t;
    struct Result {
        bool success;
        Word pc;
        std::size_t consumed;
        std::string_view error;
    };

    template <typename Read, typename Emit>
    Result Execute(std::span<const Word> code, Word start,
                   std::span<const Word> parameters, Read read, Emit emit) {
        std::array<Word, 24> regs{};
        Word initialized{}, carry{}, method{}, increment{};
        Word pc = start, loop_count{}, loop_start{}, loop_end{};
        std::size_t consumed{};
        bool end_next{}, stopped{};
        std::string_view error;
        const auto fail = [&](std::string_view message) { if (error.empty()) error = message; };
        for (Word steps = 0; steps < 1000000; ++steps) {
            if (static_cast<std::size_t>(pc) * 3 + 2 >= code.size()) {
                fail("instruction outside uploaded RAM");
                break;
            }
            const std::array<Word, 3> bits{code[pc * 3 + 2], code[pc * 3 + 1], code[pc * 3]};
            const auto field = [&](Word low, Word count) {
                std::uint64_t value = bits[low / 32];
                if (low / 32 < 2) value |= std::uint64_t{bits[low / 32 + 1]} << 32;
                return Word((value >> (low % 32)) & ((std::uint64_t{1} << count) - 1));
            };
            const std::array<Word, 2> imm{field(30, 16), field(66, 16)};
            const std::array<Word, 2> op{field(10, 5), field(46, 5)};
            const std::array<Word, 2> dst{field(15, 5), field(51, 5)};
            const std::array<Word, 2> src_a{field(20, 5), field(56, 5)};
            const std::array<Word, 2> src_b{field(25, 5), field(61, 5)};
            const std::array<Word, 2> out_method{field(82, 3), field(89, 3)};
            const std::array<Word, 2> out_emit{field(85, 4), field(92, 4)};
            const Word pred_reg = field(5, 5), pred_mode = field(1, 4);
            std::array<Word, 2> load{}, alu{};
            for (Word i = 0; i < 2; ++i) {
                const Word reg = 28 + i, out = 3 + i;
                const bool needed = pred_reg == reg || src_a[0] == reg || src_a[1] == reg ||
                    src_b[0] == reg || src_b[1] == reg || out_method[0] == out ||
                    out_method[1] == out || out_emit[0] == out || out_emit[1] == out;
                if (needed) {
                    if (consumed >= parameters.size()) { fail("parameter FIFO underflow"); break; }
                    load[i] = parameters[consumed++];
                }
            }
            // Both ALUs read the register file before either destination is
            // committed. Carry chaining remains explicit between the lanes.
            const auto input_regs = regs;
            const Word input_initialized = initialized;
            const auto get = [&](Word reg, Word lane) -> Word {
                if (reg < 24) {
                    if (!(input_initialized & (1U << reg))) fail("uninitialized register read");
                    return input_regs[reg];
                }
                switch (reg) {
                case 24: return 0;
                case 25: return Word(std::int32_t(std::int16_t(imm[lane])));
                case 26: return Word(std::int32_t(std::int16_t(imm[1 - lane])));
                case 27: return (imm[0] << 16) | imm[1];
                case 28: return load[0];
                case 29: return load[1];
                default: fail("invalid source register"); return 0;
                }
            };
            constexpr std::array<std::string_view, 16> predicates{
                "UUUU", "TTTT", "FFFF", "TTUU", "FFUU", "TFUU", "TUUU", "FUUU",
                "UUTT", "UUTF", "UUTU", "UUFT", "UUFF", "UUFU", "UUUT", "UUUF"};
            Word mask = 15;
            if (pred_mode) {
                const char skipped = get(pred_reg, 0) ? 'T' : 'F';
                mask = 0;
                for (Word i = 0; i < 4; ++i)
                    if (predicates[pred_mode][i] != skipped) mask |= 1U << i;
            }
            Word next_pc = pc + 1;
            bool branch_cancels_end{};
            for (Word lane = 0; lane < 2 && error.empty(); ++lane) {
                if (!(mask & (1U << lane))) continue;
                const Word x = get(src_a[lane], lane), y = get(src_b[lane], lane);
                Word value{};
                const auto condition = [&](Word comparison) {
                    switch (comparison) {
                    case 17: case 25: return std::int32_t(x) < std::int32_t(y);
                    case 18: case 26: return x < y;
                    case 19: case 27: return std::int32_t(x) <= std::int32_t(y);
                    case 20: case 28: return x <= y;
                    default: return x == y;
                    }
                };
                switch (op[lane]) {
                case 0: value = x + y; carry = value < x; break;
                case 1: value = x + y + carry; break;
                case 2: value = x - y; carry = value > x; break;
                case 3: value = x - y - carry; break;
                case 4: case 6: {
                    const std::uint64_t a = op[lane] == 4 ? std::uint64_t(std::int64_t(std::int32_t(x))) : x;
                    const std::uint64_t b = op[lane] == 4 ? std::uint64_t(std::int64_t(std::int32_t(y))) : y;
                    const auto product = a * b;
                    value = Word(product); carry = Word(product >> 32); break;
                }
                case 5: value = carry; break;
                case 7: fail("unsupported extended operation"); break;
                case 8: value = Word(std::countl_zero(x)); break;
                case 9: value = x << (y & 31); break;
                case 10: value = x >> (y & 31); break;
                case 11: value = Word(std::int32_t(x) >> (y & 31)); break;
                case 12: value = x & y; break;
                case 13: value = ~(x & y); break;
                case 14: value = x | y; break;
                case 15: value = x ^ y; break;
                case 16: {
                    const Word dest = (imm[lane] >> 10) & 63, count = (imm[lane] >> 5) & 31;
                    const Word source = imm[lane] & 31;
                    if (dest >= 32) { fail("invalid merge destination"); break; }
                    const Word bits_mask = (1U << count) - 1;
                    value = (x & ~(bits_mask << dest)) | (((y >> source) & bits_mask) << dest);
                    break;
                }
                case 17: case 18: case 19: case 20: case 21:
                    value = condition(op[lane]) ? ~0U : 0; break;
                case 22: value = read((x + y) & 0xffff); break;
                case 23:
                    if (loop_count || imm[lane] < 2) { fail("invalid loop"); break; }
                    // The native SDK emits zero-count loops around FIFO reads.
                    // Unlike Mesa's test simulator, these must skip the body.
                    if (x == 0) { next_pc = pc + imm[lane]; break; }
                    loop_count = x - 1; loop_start = pc; loop_end = pc + imm[lane] - 1;
                    break;
                case 24: next_pc = pc + (imm[lane] & 0x7fff); break;
                case 25: case 26: case 27: case 28: case 29:
                    if (condition(op[lane]) == bool(imm[lane] & 0x8000)) {
                        const auto offset = std::int32_t(imm[lane] << 19) >> 19;
                        if (offset == -4096) stopped = true;
                        else {
                            next_pc = Word(std::int32_t(pc) + offset);
                            // Native back edges use bit 14 to suppress the
                            // fallthrough termination when the branch is taken.
                            branch_cancels_end = (imm[lane] & 0x4000) != 0;
                        }
                    }
                    break;
                case 30:
                    if (x >= dram.size()) { fail("data RAM read out of bounds"); break; }
                    value = dram[x]; break;
                case 31:
                    if (x >= dram.size()) { fail("data RAM write out of bounds"); break; }
                    dram[x] = y; break;
                }
                alu[lane] = value;
                if (dst[lane] < 24) { regs[dst[lane]] = value; initialized |= 1U << dst[lane]; }
                else if (dst[lane] != 24) fail("invalid destination register");
            }
            const auto output = [&](Word selector) -> Word {
                switch (selector) {
                case 1: return alu[0]; case 2: return alu[1];
                case 3: return load[0]; case 4: return load[1];
                case 5: return imm[0]; case 6: return imm[1];
                case 8: return imm[0] >> 12; case 9: return imm[1] >> 12;
                case 10: return (imm[0] << 16) | imm[1];
                default: fail("invalid output selector"); return 0;
                }
            };
            for (Word lane = 0; lane < 2 && error.empty(); ++lane) {
                if (!(mask & (4U << lane))) continue;
                if (out_method[lane]) {
                    const Word address = output(out_method[lane]);
                    method = address & 0xfff; increment = (address >> 12) & 15;
                }
                if (out_emit[lane]) {
                    const Word value = output(out_emit[lane]);
                    if (error.empty() && !emit(method, value)) fail("unsupported emitted method");
                    method += increment;
                }
            }
            if (!error.empty()) break;
            if (end_next || stopped) return {true, pc, consumed, {}};
            end_next = field(0, 1) != 0 && !branch_cancels_end;
            if (loop_count && pc == loop_end) { --loop_count; next_pc = loop_start + 1; }
            pc = next_pc;
        }
        if (error.empty()) error = "instruction limit exceeded";
        return {false, pc, consumed, error};
    }

    std::array<Word, 0xc00> dram{};
};

} // namespace Tegra
