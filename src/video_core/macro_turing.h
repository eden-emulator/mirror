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
#include <functional>

namespace Tegra {

// TU104/GA10x MME has two ALUs and two outputs per 96-bit instruction.
class TuringMacroInterpreter {
public:
    using Word = std::uint32_t;
    using MemoryRead = std::function<bool(std::uint64_t, std::span<Word>)>;
    struct Result {
        bool success;
        Word pc;
        std::size_t consumed;
        std::string_view error;
    };

    struct Instruction {
        std::array<Word, 2> imm{};
        std::array<std::uint8_t, 2> op{}, dst{}, src_a{}, src_b{}, out_method{}, out_emit{};
        std::array<std::uint8_t, 2> predicate_masks{15, 15};
        std::uint8_t pred_reg{}, load_mask{};
        bool predicated{}, end_next{};
    };

    static constexpr Instruction Decode(std::span<const Word, 3> code) {
        const std::array<Word, 3> bits{code[2], code[1], code[0]};
        const auto field = [&](Word low, Word count) {
            std::uint64_t value = bits[low / 32];
            if (low / 32 < 2) value |= std::uint64_t{bits[low / 32 + 1]} << 32;
            return Word((value >> (low % 32)) & ((std::uint64_t{1} << count) - 1));
        };
        Instruction instruction{};
        for (Word lane = 0; lane < 2; ++lane) {
            instruction.imm[lane] = field(30 + lane * 36, 16);
            instruction.op[lane] = static_cast<std::uint8_t>(field(10 + lane * 36, 5));
            instruction.dst[lane] = static_cast<std::uint8_t>(field(15 + lane * 36, 5));
            instruction.src_a[lane] = static_cast<std::uint8_t>(field(20 + lane * 36, 5));
            instruction.src_b[lane] = static_cast<std::uint8_t>(field(25 + lane * 36, 5));
            instruction.out_method[lane] = static_cast<std::uint8_t>(field(82 + lane * 7, 3));
            instruction.out_emit[lane] = static_cast<std::uint8_t>(field(85 + lane * 7, 4));
        }
        instruction.pred_reg = static_cast<std::uint8_t>(field(5, 5));
        const Word pred_mode = field(1, 4);
        instruction.predicated = pred_mode != 0;
        instruction.end_next = field(0, 1) != 0;
        constexpr std::array<std::string_view, 16> predicates{
            "UUUU", "TTTT", "FFFF", "TTUU", "FFUU", "TFUU", "TUUU", "FUUU",
            "UUTT", "UUTF", "UUTU", "UUFT", "UUFF", "UUFU", "UUUT", "UUUF"};
        for (Word truth = 0; truth < 2; ++truth) {
            Word mask{};
            for (Word lane = 0; lane < 4; ++lane) {
                if (predicates[pred_mode][lane] != (truth ? 'T' : 'F')) mask |= 1U << lane;
            }
            instruction.predicate_masks[truth] = static_cast<std::uint8_t>(mask);
        }
        for (Word lane = 0; lane < 2; ++lane) {
            const Word reg = 28 + lane, out = 3 + lane;
            if (instruction.pred_reg == reg || instruction.src_a[0] == reg ||
                instruction.src_a[1] == reg || instruction.src_b[0] == reg ||
                instruction.src_b[1] == reg || instruction.out_method[0] == out ||
                instruction.out_method[1] == out || instruction.out_emit[0] == out ||
                instruction.out_emit[1] == out) {
                instruction.load_mask |= static_cast<std::uint8_t>(1U << lane);
            }
        }
        return instruction;
    }

    template <typename Read, typename Emit>
    Result Execute(std::span<const Word> code, Word start,
                   std::span<const Word> parameters, Read read, Emit emit, MemoryRead memory_read = {}) {
        return ExecuteProgram(code.size() / 3, start, parameters, read, emit,
            [&](Word pc) { return Decode(std::span<const Word, 3>{code.data() + pc * 3, 3}); }, memory_read);
    }

    template <typename Read, typename Emit>
    Result ExecuteDecoded(std::span<const Instruction> code, Word start,
                          std::span<const Word> parameters, Read read, Emit emit, MemoryRead memory_read = {}) {
        return ExecuteProgram(code.size(), start, parameters, read, emit,
            [&](Word pc) -> const Instruction& { return code[pc]; }, memory_read);
    }

    std::array<Word, 0xc00> dram{};

private:
    template <typename Read, typename Emit, typename Fetch>
    Result ExecuteProgram(std::size_t instruction_count, Word start,
                          std::span<const Word> parameters, Read read, Emit emit, Fetch fetch, const MemoryRead& memory_read) {
        std::array<Word, 24> regs{};
        Word initialized{}, carry{}, method{}, increment{};
        Word pc = start, loop_count{}, loop_start{}, loop_end{};
        std::size_t consumed{}, parameter_cursor{};
        std::array<Word, 1024> dma_fifo{};
        Word dma_count{}, address_high{}, address_low{};
        bool end_next{}, stopped{};
        std::string_view error;
        const auto fail = [&](std::string_view message) { if (error.empty()) error = message; };
        for (Word steps = 0; steps < 1000000; ++steps) {
            if (pc >= instruction_count) {
                fail("instruction outside uploaded RAM");
                break;
            }
            const auto instruction = fetch(pc);
            const auto& imm = instruction.imm;
            const auto& op = instruction.op;
            const auto& dst = instruction.dst;
            const auto& src_a = instruction.src_a;
            const auto& src_b = instruction.src_b;
            const auto& out_method = instruction.out_method;
            const auto& out_emit = instruction.out_emit;
            std::array<Word, 2> load{}, alu{};
            for (Word lane = 0; lane < 2; ++lane) {
                if (instruction.load_mask & (1U << lane)) {
                    if (parameter_cursor >= parameters.size()) { fail("parameter FIFO underflow"); break; }
                    load[lane] = parameters[parameter_cursor++];
                    ++consumed;
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
            const Word mask = instruction.predicated
                ? instruction.predicate_masks[get(instruction.pred_reg, 0) != 0] : 15;
            Word next_pc = pc + 1;
            bool branch_cancels_end{};
            Word executed_mask{};
            for (Word lane = 0; lane < 2 && error.empty(); ++lane) {
                if (!(mask & (1U << lane))) continue;
                executed_mask |= 1U << lane;
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
                case 7:
                    if (x != 0x1000 || y != 1) {
                        fail("unsupported extended operation");
                    } else if (dma_count != 0) {
                        const std::uint64_t address = (std::uint64_t{address_high} << 32) | address_low;
                        if (!memory_read || !memory_read(address, std::span{dma_fifo}.first(dma_count))) {
                            fail("FIFO DMA memory read failed");
                        } else {
                            parameters = std::span{dma_fifo}.first(dma_count);
                            parameter_cursor = 0;
                            dma_count = 0;
                        }
                    }
                    break;
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
                if (dst[lane] > 24) fail("invalid destination register");
            }
            for (Word lane = 0; lane < 2; ++lane) {
                if ((executed_mask & (1U << lane)) && dst[lane] < 24) {
                    regs[dst[lane]] = alu[lane];
                    initialized |= 1U << dst[lane];
                }
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
                    if (error.empty()) {
                        switch (method) {
                        case 0x154: address_high = value & 0xff; break;
                        case 0x155: address_low = value; break;
                        case 0x158:
                            if (value > dma_fifo.size()) fail("FIFO DMA length out of bounds");
                            else dma_count = value;
                            break;
                        case 0x156: case 0x157: case 0x159: case 0x15a:
                            fail("unsupported DMA method"); break;
                        default:
                            if (!emit(method, value)) fail("unsupported emitted method");
                            break;
                        }
                    }
                    method += increment;
                }
            }
            if (!error.empty()) break;
            if (end_next || stopped) return {true, pc, consumed, {}};
            end_next = instruction.end_next && !branch_cancels_end;
            if (loop_count && pc == loop_end) { --loop_count; next_pc = loop_start + 1; }
            pc = next_pc;
        }
        if (error.empty()) error = "instruction limit exceeded";
        return {false, pc, consumed, error};
    }

};

} // namespace Tegra
