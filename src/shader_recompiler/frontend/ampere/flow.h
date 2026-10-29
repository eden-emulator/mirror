// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <span>
#include <vector>

#include "common/common_types.h"
#include "shader_recompiler/frontend/ampere/native.h"

namespace Shader::Ampere {

// Guard predicate field of an instruction: P0..P6, or PT (index 7), optionally negated.
struct Guard {
    u32 index;
    bool negated;
    [[nodiscard]] bool Always() const {
        return index == 7 && !negated;
    }
    [[nodiscard]] bool Never() const {
        return index == 7 && negated;
    }
};

[[nodiscard]] Guard GuardOf(const Instruction& instruction);
// Second predicate operand (87..90) of EXIT/BRA/BSSY/BSYNC/KILL.
[[nodiscard]] Guard SecondOf(const Instruction& instruction);
// Absolute instruction index targeted by a native BRA or BSSY.
[[nodiscard]] std::optional<u32> BranchTarget(const Instruction& instruction, u32 index);

struct FlowAnalysis {
    std::vector<u8> reachable;
    std::vector<u8> leader;
    std::map<u32, u32> sync_target; // BSYNC instruction -> reconvergence instruction
    std::vector<u32> indirect_targets; // destinations of the first BRX table (kept for callers)
    std::map<u32, std::vector<u32>> indirect_tables; // BRX instruction -> its destinations
    // Subroutines: function 0 is the program, others are CALL targets (inlined by the structurizer).
    std::vector<u32> function_of;       // function owning each reachable instruction
    std::vector<u32> function_entry;    // entry instruction of each function
    std::map<u32, u32> call_function;   // CALL instruction -> callee function
};

// Destinations of the BRX at an instruction index; an empty result falls back to the single table.
using IndirectTargetProvider = std::function<std::span<const u32>(u32)>;

// Walks the program from its entry, tracking convergence barriers. Throws UnsupportedError for
// anything whose control flow is not modelled exactly.
[[nodiscard]] FlowAnalysis AnalyzeFlow(std::span<const Instruction> code,
                                       std::span<const u32> indirect_targets = {},
                                       const IndirectTargetProvider* per_branch = nullptr);

// The two barrier models behind AnalyzeFlow, exposed so tests can check that they agree: a strict
// BSSY/BSYNC stack, and a barrier-register model that only needs each BSYNC to name one target.
[[nodiscard]] FlowAnalysis AnalyzeFlowStack(std::span<const Instruction> code,
                                            std::span<const u32> indirect_targets = {});
[[nodiscard]] FlowAnalysis AnalyzeFlowRegisters(std::span<const Instruction> code,
                                                std::span<const u32> indirect_targets = {});

} // namespace Shader::Ampere
