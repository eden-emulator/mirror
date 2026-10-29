// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <optional>
#include <span>
#include <vector>

#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"

namespace Shader::Ampere {
class ProgramCaptureState {
public:
    enum class Result { Continue, Complete, InvalidTarget };
    static constexpr u32 MaximumInstructions = 65536;

    Result Observe(const Instruction& instruction, u32 index) {
        const u32 opcode = instruction.Bits(0, 12);
        if (opcode == 0x944 || opcode == 0x947 || opcode == 0x945) {
            const u64 raw = instruction.words[1] | (u64{instruction.words[2] & 0x3ffff} << 32);
            // BRA.DIV (bit 91 and a uniform register) carries two mode bits below its word offset.
            const bool divergence = opcode == 0x947 && (instruction.words[2] & ~0x3ffffU) == 0x0b800000U &&
                                    (instruction.words[1] & 3) == 3;
            const s64 relative = opcode == 0x945 ? static_cast<s32>(instruction.words[1])
                                                : (static_cast<s64>(raw << 14) >> 14) - (divergence ? 3 : 0);
            const s64 target = s64{index} * 16 + 16 + relative;
            if (target < 0 || target % 16 || target >= MaximumInstructions * 16)
                return Result::InvalidTarget;
            if (opcode == 0x944 && instruction.Bits(12, 4) == 7) {
                last_call_target = std::max(last_call_target, static_cast<u32>(target / 16));
            } else {
                last_forward_target = std::max(last_forward_target, static_cast<u32>(target / 16));
            }
        }
        if (opcode == 0x94d && instruction.Bits(12, 4) == 7 && instruction.Bits(87, 4) == 7 &&
            !instruction.Bits(84, 2) && index >= last_forward_target) {
            if (!last_call_target) return Result::Complete;
            main_exited = true;
        }
        if (opcode == 0x950 && instruction.Bits(12, 4) == 7 && main_exited &&
            index >= last_call_target && index >= last_forward_target)
            return Result::Complete;
        return Result::Continue;
    }

private:
    u32 last_call_target{};
    u32 last_forward_target{};
    bool main_exited{};
};

// A BRX selects among its blocks through a constant-buffer table, so the furthest block can lie past the first
// EXIT that ends the linear capture. The capture is extended through it and the per-BRX target lists are
// reported to the environment. The tables are neither validated nor rejected (entries that are not source
// addresses are ignored) and the translators still decide what is acceptable.
struct CaptureIndirectBranch {
    u32 index;       // instruction index of the BRX
    u32 cbuf_index;  // constant bank holding the table
    u32 cbuf_offset; // byte offset of the first entry
    u32 entries;
};

struct CaptureIndirectTable {
    u32 branch{};
    std::vector<u32> values;  // raw table words
    std::vector<u32> targets; // sorted distinct instruction indices inside the capture
};

struct CaptureIndirectResult {
    u32 base{};
    bool extended{};
    std::vector<CaptureIndirectTable> tables;
};

inline s64 CaptureRelativeOffset48(const Instruction& instruction) {
    const u64 raw = u64{instruction.words[1] & ~3U} | (u64{instruction.words[2] & 0x3ffffU} << 32);
    return static_cast<s64>(raw << 14) >> 14;
}

// Recognises, for every self-relative BRX, the selector sequence of the jump tables: a value clamped to
// [0, N-1] (IMNMX), scaled by 4 (SHF), loaded from a constant bank (LDC) and branched on. A BRX that does not
// match is simply not tracked.
inline std::vector<CaptureIndirectBranch> TrackCaptureIndirectBranches(std::span<const Instruction> code) {
    std::vector<CaptureIndirectBranch> result;
    for (u32 branch_index = 0; branch_index < code.size(); ++branch_index) {
        const auto& branch = code[branch_index];
        if (branch.Bits(0, 12) != 0x949)
            continue;
        if (branch.Bits(12, 4) != 7 || (branch.words[2] & ~0x3ffffU) != 0x03800000U ||
            s64{branch_index} * sizeof(Instruction) + sizeof(Instruction) + CaptureRelativeOffset48(branch) != 0)
            continue;
        const u32 branch_source = branch.Bits(24, 8);
        std::optional<u32> load_index;
        for (u32 index = branch_index; index-- > 0;) {
            const auto& instruction = code[index];
            if (instruction.Bits(0, 12) == 0xb82 && instruction.Bits(12, 4) == 7 &&
                instruction.Bits(16, 8) == branch_source && instruction.words[2] == 0x00000800 &&
                !(instruction.words[1] & ~((0x1fU << 22) | (0xffffU << 6))) && !((instruction.words[1] >> 6) & 3)) {
                load_index = index;
                break;
            }
        }
        if (!load_index)
            continue;
        const auto& load = code[*load_index];
        const u32 selector = load.Bits(24, 8);
        std::optional<u32> shift_index;
        for (u32 index = *load_index; index-- > 0;) {
            const auto& instruction = code[index];
            if (instruction.Bits(0, 12) == 0x819 && instruction.Bits(12, 4) == 7 &&
                instruction.Bits(16, 8) == selector && instruction.words[1] == 2 &&
                instruction.words[2] == 0x000006ff) {
                shift_index = index;
                break;
            }
        }
        if (!shift_index)
            continue;
        const u32 bounded_selector = code[*shift_index].Bits(24, 8);
        std::optional<u32> maximum;
        for (u32 index = *shift_index; index-- > 0;) {
            const auto& instruction = code[index];
            if (instruction.Bits(0, 12) == 0x817 && instruction.Bits(12, 4) == 7 &&
                instruction.Bits(16, 8) == bounded_selector && instruction.words[1] >= 1 &&
                instruction.words[1] <= 15 && instruction.words[2] == 0x03800000) {
                maximum = instruction.words[1];
                break;
            }
        }
        if (!maximum)
            continue;
        result.push_back({.index = branch_index,
                          .cbuf_index = (load.words[1] >> 22) & 0x1f,
                          .cbuf_offset = (load.words[1] >> 6) & 0xffff,
                          .entries = *maximum + 1});
    }
    return result;
}

// Reads the table of every tracked BRX in `native`, extends `native` (already captured through its first
// terminating EXIT) through the furthest in-range target and returns the per-BRX target indices.
// Table values are offsets from the program base: they include the `header_size` bytes before instruction 0.
// A non-zero entry below `header_size` can only be an offset from instruction 0, which selects that convention
// instead. When there is no table, every target is already inside the capture, or the tail cannot be read to a
// terminating EXIT, `native` and `capture` are left untouched, so shaders that capture correctly today are
// not affected.
template <typename ReadInstruction, typename ReadCbuf>
CaptureIndirectResult ResolveCaptureIndirectBranches(std::vector<Instruction>& native, ProgramCaptureState& capture,
                                                     u32 header_size, ReadInstruction&& read_instruction,
                                                     ReadCbuf&& read_cbuf) {
    const auto read_tables = [&] {
        std::vector<CaptureIndirectTable> tables;
        for (const auto& branch : TrackCaptureIndirectBranches(native)) {
            auto& table = tables.emplace_back();
            table.branch = branch.index;
            for (u32 entry = 0; entry < branch.entries; ++entry)
                table.values.push_back(read_cbuf(branch.cbuf_index, branch.cbuf_offset + entry * 4));
        }
        return tables;
    };
    CaptureIndirectResult result;
    result.tables = read_tables();
    if (result.tables.empty())
        return result;
    result.base = header_size;
    for (const auto& table : result.tables)
        for (const u32 value : table.values)
            if (value && value < header_size)
                result.base = 0;
    const auto entry_index = [&](u32 value) -> std::optional<u32> {
        if (!value || value < result.base || (value - result.base) % sizeof(Instruction))
            return {};
        const u32 index = (value - result.base) / sizeof(Instruction);
        if (index >= ProgramCaptureState::MaximumInstructions)
            return {};
        return index;
    };
    u32 furthest = 0;
    for (const auto& table : result.tables)
        for (const u32 value : table.values)
            if (const auto index = entry_index(value))
                furthest = std::max(furthest, *index);
    if (furthest >= native.size()) {
        const auto saved_native = native;
        const auto saved_capture = capture;
        bool complete = false;
        for (u32 index = static_cast<u32>(native.size()); index < ProgramCaptureState::MaximumInstructions;
             ++index) {
            const std::optional<Instruction> instruction = read_instruction(index);
            if (!instruction)
                break;
            native.push_back(*instruction);
            const auto status = capture.Observe(*instruction, index);
            if (status == ProgramCaptureState::Result::InvalidTarget)
                break;
            if (status == ProgramCaptureState::Result::Complete && native.size() > furthest) {
                complete = true;
                break;
            }
        }
        if (complete) {
            result.extended = true;
            result.tables = read_tables();
        } else {
            native = saved_native;
            capture = saved_capture;
        }
    }
    for (auto& table : result.tables) {
        for (const u32 value : table.values)
            if (const auto index = entry_index(value); index && *index < native.size())
                table.targets.push_back(*index);
        std::ranges::sort(table.targets);
        table.targets.erase(std::unique(table.targets.begin(), table.targets.end()), table.targets.end());
    }
    return result;
}
} // namespace Shader::Ampere
