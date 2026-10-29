// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// Encoding reference: Mesa NAK sm70_encode.rs (MIT), Copyright 2022 Collabora, Ltd.

#include <algorithm>
#include <bit>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "shader_recompiler/environment.h"
#include "shader_recompiler/frontend/ampere/cfg_builder.h"
#include "shader_recompiler/frontend/ampere/classify.h"
#include "shader_recompiler/frontend/ampere/flow.h"
#include "shader_recompiler/frontend/ampere/isa.h"
#include "shader_recompiler/frontend/maxwell/control_flow.h"

namespace Shader::Ampere {
namespace {
using Maxwell::Location;
namespace Flow = Maxwell::Flow;

using BarrierStack = std::vector<std::pair<u32, u32>>; // (barrier id, reconvergence instruction)

// Barrier-register model: the state of B0..B15 as (id, target) pairs sorted by id. A target of
// CONFLICTING_TARGET marks a register that different paths set to different reconvergence points.
constexpr u32 CONFLICTING_TARGET = ~0U;

BarrierStack MergeBarrierRegisters(const BarrierStack& a, const BarrierStack& b) {
    BarrierStack merged;
    size_t i = 0;
    size_t j = 0;
    while (i < a.size() || j < b.size()) {
        if (j == b.size() || (i < a.size() && a[i].first < b[j].first)) {
            merged.push_back(a[i++]);
        } else if (i == a.size() || b[j].first < a[i].first) {
            merged.push_back(b[j++]);
        } else {
            merged.emplace_back(a[i].first, a[i].second == b[j].second ? a[i].second : CONFLICTING_TARGET);
            ++i;
            ++j;
        }
    }
    return merged;
}

struct Visit {
    u32 index;
    BarrierStack stack;
    u32 function{};
};

// Return address register set up for the CALL at `index`: a MOV of the address of the next
// instruction shortly before it.
std::optional<u32> CallLinkRegister(std::span<const Instruction> code, u32 index) {
    for (u32 back = 1; back <= 6 && back <= index; ++back) {
        const Instruction& insn = code[index - back];
        const u32 op = insn.Bits(0, 12);
        const OpKind kind = Classify(op).kind;
        if (kind != OpKind::Plain && kind != OpKind::Nop) {
            return std::nullopt;
        }
        if (op == 0x802 && insn.Bits(12, 4) == 7 && insn.Bits(32, 32) == (index + 1) * 16 &&
            insn.Bits(72, 4) == 0xf) {
            return insn.Bits(16, 8);
        }
    }
    return std::nullopt;
}

IR::Condition CondOf(Guard guard) {
    return IR::Condition(static_cast<IR::Pred>(guard.index), guard.negated);
}

Location At(u32 index) {
    return Location{LocationOf(index)};
}
} // Anonymous namespace

Guard GuardOf(const Instruction& instruction) {
    return {instruction.Bits(12, 3), instruction.Bits(15, 1) != 0};
}

Guard SecondOf(const Instruction& instruction) {
    return {instruction.Bits(87, 3), instruction.Bits(90, 1) != 0};
}

std::optional<u32> BranchTarget(const Instruction& instruction, u32 index) {
    const u32 op = instruction.Bits(0, 12);
    const s64 relative = op == 0x945 ? RelativeOffset30(instruction) : RelativeOffset48(instruction);
    const s64 target = s64{index} * 16 + 16 + relative;
    if (target < 0 || target % 16) {
        return std::nullopt;
    }
    return static_cast<u32>(target / 16);
}

namespace {
// A CALL with no return address, whose target is the instruction after the unconditional BRA that
// follows it, is a predicated jump over that BRA: `@P CALL.REL.NOINC L; BRA back; L:`. It never
// returns, so it is treated as a conditional branch to L.
bool IsSkipCall(std::span<const Instruction> code, u32 index) {
    if (code[index].Bits(0, 12) != 0x944 || index + 2 >= code.size()) {
        return false;
    }
    const Instruction& next = code[index + 1];
    if (next.Bits(0, 12) != 0x947 || !GuardOf(next).Always() || !SecondOf(next).Always()) {
        return false;
    }
    return BranchTarget(code[index], index) == index + 2 && !CallLinkRegister(code, index);
}

// A subroutine may return through a register other than the call-site link register when a
// `MOV reg, link` shortly before the RET copies it and nothing branches into the code in between.
bool ReturnRegisterCopiesLink(std::span<const Instruction> code, std::span<const u32> indirect_targets,
                              u32 ret_index, u32 reg, u32 link) {
    if (reg >= 250 || link >= 250) {
        return false;
    }
    const auto is_branch_target = [&](u32 index) {
        if (std::find(indirect_targets.begin(), indirect_targets.end(), index) != indirect_targets.end()) {
            return true;
        }
        for (u32 i = 0; i < code.size(); ++i) {
            const u32 op = code[i].Bits(0, 12);
            if (op == 0x947 || op == 0x945 || op == 0x944) {
                if (BranchTarget(code[i], i) == index) {
                    return true;
                }
            }
        }
        return false;
    };
    for (u32 back = 1; back <= 8 && back <= ret_index; ++back) {
        const u32 index = ret_index - back;
        const Instruction& insn = code[index];
        const u32 op = insn.Bits(0, 12);
        const OpKind kind = Classify(op).kind;
        if (kind != OpKind::Plain && kind != OpKind::Nop) {
            return false;
        }
        if (Classify(op).pure && !Classify(op).uniform && insn.Bits(16, 8) == reg) {
            // The last write to the return register must be the copy of the link register.
            if (op != 0x202 || !GuardOf(insn).Always() || insn.Bits(32, 8) != link ||
                insn.Bits(40, 24) != 0 || insn.Bits(72, 4) != 0xf) {
                return false;
            }
            for (u32 between = index + 1; between <= ret_index; ++between) {
                if (is_branch_target(between)) {
                    return false;
                }
            }
            return true;
        }
    }
    return false;
}

// Whether translating `insn` as the start of a block creates a block at the virtual location in
// front of it (a predicate test of its own).
bool NeedsVirtualBlock(const Instruction& insn) {
    const OpInfo info = Classify(insn.Bits(0, 12));
    const Guard guard = GuardOf(insn);
    const Guard second = SecondOf(insn);
    if (guard.Never()) {
        return false;
    }
    if ((info.kind == OpKind::Plain && !info.pure && !guard.Always()) || info.kind == OpKind::Kill ||
        (info.kind == OpKind::Call && !guard.Always())) {
        return true;
    }
    if (info.kind == OpKind::Exit) {
        return !(guard.Always() && second.Always());
    }
    return false;
}

FlowAnalysis AnalyzeFlowImpl(std::span<const Instruction> code, std::span<const u32> indirect_targets,
                             const IndirectTargetProvider* per_branch,
                             bool register_model) {
    const u32 count = static_cast<u32>(code.size());
    if (count == 0 || count > 65536) {
        throw UnsupportedError(0, "invalid native program size");
    }
    FlowAnalysis result;
    result.reachable.assign(count, 0);
    result.leader.assign(count, 0);
    std::vector<std::optional<BarrierStack>> seen(count);
    result.function_of.assign(count, 0);
    result.function_entry.push_back(0);
    std::vector<u32> link_register(1, ~0U);
    std::vector<std::vector<std::pair<u32, u32>>> returns(1); // per function: (instruction, register)
    std::vector<Visit> work;
    work.push_back({0, {}, 0});
    result.leader[0] = 1;
    const auto mark_leader = [&](u32 index, u32 from) {
        if (index >= count) {
            throw UnsupportedError(from, "native branch or call leaves captured program");
        }
        result.leader[index] = 1;
    };
    while (!work.empty()) {
        Visit visit = std::move(work.back());
        work.pop_back();
        u32 index = visit.index;
        BarrierStack stack = std::move(visit.stack);
        const u32 function = visit.function;
        while (true) {
            if (index >= count) {
                throw UnsupportedError(count - 1, "native program runs past its capture");
            }
            if (seen[index]) {
                if (result.function_of[index] != function) {
                    throw UnsupportedError(index, "native code shared between subroutines");
                }
                if (register_model) {
                    const BarrierStack merged = MergeBarrierRegisters(*seen[index], stack);
                    if (merged == *seen[index]) {
                        break;
                    }
                    seen[index] = merged; // revisit with the merged register state
                    stack = merged;
                } else if (*seen[index] != stack) {
                    // Paths that left a convergence region early arrive with a longer stack; the join
                    // is outside those regions, so the shorter stack (a common prefix) wins.
                    const auto is_prefix = [](const BarrierStack& shorter, const BarrierStack& longer) {
                        return shorter.size() <= longer.size() &&
                               std::equal(shorter.begin(), shorter.end(), longer.begin());
                    };
                    if (is_prefix(*seen[index], stack)) {
                        break;
                    }
                    if (!is_prefix(stack, *seen[index])) {
                        throw UnsupportedError(index, "native reconvergence stacks differ at a join");
                    }
                    seen[index] = stack; // revisit with the shorter stack
                } else {
                    break;
                }
            } else {
                seen[index] = stack;
            }
            result.reachable[index] = 1;
            result.function_of[index] = function;
            const Instruction& insn = code[index];
            const u32 op = insn.Bits(0, 12);
            const IsaRow* const row = LookupRow(op);
            if (!row) {
                throw UnsupportedError(index, fmt::format("unsupported native opcode {:#x}", op));
            }
            if (const int bit = FirstUnknownBit(insn, *row); bit >= 0) {
                throw UnsupportedError(index, fmt::format("unknown bit {} in op {:#x}", bit, op));
            }
            OpInfo info = Classify(op);
            if (info.kind == OpKind::Call && IsSkipCall(code, index)) {
                info.kind = OpKind::Bra;
            }
            const Guard guard = GuardOf(insn);
            const Guard second = SecondOf(insn);
            if (guard.Never()) {
                // Statically disabled instruction: no effect, and it ends no block.
                ++index;
                continue;
            }
            switch (info.kind) {
            case OpKind::Plain:
                if ((op == 0xb1d || op == 0x83b || op == 0x23a || op == 0x23c) && !guard.Always()) {
                    // Barriers and warp-wide matrix operations need every lane of the warp to execute
                    // them; a divergent conditional block would not.
                    throw UnsupportedError(index, "native predicated barrier or matrix operation");
                }
                if (!info.pure && !guard.Always()) {
                    // Guarded side effect: becomes its own conditional block.
                    result.leader[index] = 1;
                    mark_leader(index + 1, index);
                }
                ++index;
                continue;
            case OpKind::Nop:
                ++index;
                continue;
            case OpKind::Kill:
                if (!second.Always()) {
                    throw UnsupportedError(index, "native KILL with extra predicate");
                }
                result.leader[index] = 1;
                mark_leader(index + 1, index);
                ++index;
                continue;
            case OpKind::Exit:
                if (insn.Bits(84, 2)) {
                    throw UnsupportedError(index, "native conditional/special EXIT");
                }
                if (guard.Always() && second.Always()) {
                    break; // unconditional: the path ends
                }
                if (!guard.Always() && !second.Always() &&
                    (index + 1 >= count || NeedsVirtualBlock(code[index + 1]))) {
                    // The second predicate test needs the virtual location in front of the next
                    // instruction, which that instruction would also use.
                    throw UnsupportedError(index, "native EXIT with two predicates");
                }
                result.leader[index] = 1;
                mark_leader(index + 1, index);
                ++index;
                continue;
            case OpKind::Bra: {
                const auto target = BranchTarget(insn, index);
                if (!target) {
                    throw UnsupportedError(index, "native branch destination alignment");
                }
                if (insn.Bits(32, 1)) {
                    throw UnsupportedError(index, "native uniform branch");
                }
                mark_leader(*target, index);
                work.push_back({*target, stack, function});
                if (guard.Always() && second.Always()) {
                    break; // unconditional: the path ends
                }
                mark_leader(index + 1, index);
                ++index;
                continue;
            }
            case OpKind::Call: {
                if ((insn.words[2] & ~0x3ffffU) != 0x03c00000U) {
                    throw UnsupportedError(index, "native call mode");
                }
                if (!guard.Always()) {
                    result.leader[index] = 1; // the predicate test gets its own block
                }
                const auto target = BranchTarget(insn, index);
                const auto link = CallLinkRegister(code, index);
                if (!target || !link || *target >= count) {
                    throw UnsupportedError(index, "native call linkage");
                }
                u32 callee = ~0U;
                for (u32 f = 1; f < result.function_entry.size(); ++f) {
                    if (result.function_entry[f] == *target) callee = f;
                }
                if (callee == ~0U) {
                    callee = static_cast<u32>(result.function_entry.size());
                    result.function_entry.push_back(*target);
                    link_register.push_back(*link);
                    returns.emplace_back();
                    mark_leader(*target, index);
                    work.push_back({*target, stack, callee});
                } else if (link_register[callee] != *link) {
                    throw UnsupportedError(index, "native inconsistent call linkage");
                }
                if (callee == function) {
                    throw UnsupportedError(index, "native recursive call");
                }
                result.call_function[index] = callee;
                mark_leader(index + 1, index);
                ++index;
                continue;
            }
            case OpKind::Ret: {
                if (function == 0 || !second.Always() || (insn.words[2] & ~0x3ffffU) != 0x03c00000U ||
                    s64{index} * 16 + 16 + RelativeOffset48(insn) != 0) {
                    throw UnsupportedError(index, "native return linkage");
                }
                returns[function].emplace_back(index, insn.Bits(24, 8));
                if (guard.Always()) {
                    break;
                }
                mark_leader(index + 1, index);
                ++index;
                continue;
            }
            case OpKind::Brx: {
                // BRX Ra jumps to an absolute address read from a table; the destinations come from
                // the environment, which resolved the table when the program was captured.
                const bool uniform_source = insn.Bits(0, 12) == 0x958;
                if (!guard.Always() ||
                    (insn.words[2] & ~0x3ffffU) != (uniform_source ? 0x0b800000U : 0x03800000U) ||
                    s64{index} * 16 + 16 + RelativeOffset48(insn) != 0) {
                    throw UnsupportedError(index, "native computed branch mode");
                }
                std::span<const u32> targets = per_branch ? (*per_branch)(index) : std::span<const u32>{};
                if (targets.empty()) {
                    targets = indirect_targets;
                }
                if (targets.empty()) {
                    throw UnsupportedError(index, "native computed branch without a resolved table");
                }
                if (result.indirect_targets.empty()) {
                    result.indirect_targets.assign(targets.begin(), targets.end());
                }
                result.indirect_tables[index].assign(targets.begin(), targets.end());
                for (const u32 target : targets) {
                    mark_leader(target, index);
                    work.push_back({target, stack, function});
                }
                break;
            }
            case OpKind::Bssy: {
                const auto target = BranchTarget(insn, index);
                if (!target) {
                    throw UnsupportedError(index, "native branch destination alignment");
                }
                if (!guard.Always() || !second.Always()) {
                    throw UnsupportedError(index, "native predicated BSSY");
                }
                const u32 id = insn.Bits(16, 4);
                if (*target >= count) {
                    throw UnsupportedError(index, "native branch or call leaves captured program");
                }
                if (register_model) {
                    const auto slot = std::find_if(stack.begin(), stack.end(),
                                                   [id](const auto& entry) { return entry.first >= id; });
                    if (slot != stack.end() && slot->first == id) {
                        slot->second = *target;
                    } else {
                        stack.insert(slot, {id, *target});
                    }
                    ++index;
                    continue;
                }
                if (stack.size() >= 16 ||
                    std::ranges::any_of(stack, [&](const auto& entry) { return entry.first == id; })) {
                    throw UnsupportedError(index, "native nested reconvergence token");
                }
                if (*target >= count) {
                    throw UnsupportedError(index, "native branch or call leaves captured program");
                }
                stack.emplace_back(id, *target);
                ++index;
                continue;
            }
            case OpKind::Bsync: {
                if (!guard.Always() || !second.Always()) {
                    throw UnsupportedError(index, "native predicated BSYNC");
                }
                const u32 id = insn.Bits(16, 4);
                if (register_model) {
                    const auto slot = std::find_if(stack.begin(), stack.end(),
                                                   [id](const auto& entry) { return entry.first == id; });
                    if (slot == stack.end()) {
                        break; // not yet known on this path; a later merge may supply it
                    }
                    if (slot->second == CONFLICTING_TARGET) {
                        throw UnsupportedError(index, "native reconvergence target differs between paths");
                    }
                    const u32 target = slot->second;
                    stack.erase(slot); // the barrier is released
                    mark_leader(target, index);
                    if (const auto it = result.sync_target.find(index);
                        it != result.sync_target.end() && it->second != target) {
                        throw UnsupportedError(index, "native reconvergence target differs between paths");
                    }
                    result.sync_target[index] = target;
                    work.push_back({target, std::move(stack), function});
                    break;
                }
                // The barrier may sit below regions that were left early; those are abandoned.
                const auto entry = std::find_if(stack.rbegin(), stack.rend(),
                                                [id](const auto& e) { return e.first == id; });
                if (entry == stack.rend()) {
                    throw UnsupportedError(index, "native reconvergence token");
                }
                const u32 target = entry->second;
                stack.resize(static_cast<size_t>(std::distance(entry, stack.rend())) - 1);
                mark_leader(target, index);
                if (const auto it = result.sync_target.find(index);
                    it != result.sync_target.end() && it->second != target) {
                    throw UnsupportedError(index, "native reconvergence target differs between paths");
                }
                result.sync_target[index] = target;
                work.push_back({target, std::move(stack), function});
                break;
            }
            }
            break; // the path ended
        }
    }
    if (register_model) {
        for (u32 index = 0; index < count; ++index) {
            if (result.reachable[index] && code[index].Bits(0, 12) == 0x941 &&
                !GuardOf(code[index]).Never() && !result.sync_target.contains(index)) {
                throw UnsupportedError(index, "native reconvergence token");
            }
        }
    }
    std::vector<u32> all_indirect(indirect_targets.begin(), indirect_targets.end());
    for (const auto& [branch, table] : result.indirect_tables) {
        all_indirect.insert(all_indirect.end(), table.begin(), table.end());
    }
    for (u32 function = 1; function < result.function_entry.size(); ++function) {
        for (const auto& [ret_index, reg] : returns[function]) {
            if (reg != link_register[function] &&
                !ReturnRegisterCopiesLink(code, all_indirect, ret_index, reg, link_register[function])) {
                throw UnsupportedError(ret_index, "native return through a different register");
            }
        }
        // The return address must survive the subroutine: nothing in it writes the link register.
        for (u32 index = 0; index < count; ++index) {
            if (!result.reachable[index] || result.function_of[index] != function) continue;
            const u32 op = code[index].Bits(0, 12);
            if (Classify(op).pure && !Classify(op).uniform && code[index].Bits(16, 8) == link_register[function]) {
                throw UnsupportedError(index, "native subroutine overwrites its return address");
            }
        }
    }
    return result;
}
} // Anonymous namespace

FlowAnalysis AnalyzeFlowStack(std::span<const Instruction> code, std::span<const u32> indirect_targets) {
    return AnalyzeFlowImpl(code, indirect_targets, nullptr, false);
}

FlowAnalysis AnalyzeFlowRegisters(std::span<const Instruction> code,
                                  std::span<const u32> indirect_targets) {
    return AnalyzeFlowImpl(code, indirect_targets, nullptr, true);
}

FlowAnalysis AnalyzeFlow(std::span<const Instruction> code, std::span<const u32> indirect_targets,
                         const IndirectTargetProvider* per_branch) {
    // Programs whose convergence barriers nest as a stack keep their original analysis. Others are
    // retried with a barrier-register model, which only needs every BSYNC to name a single target.
    try {
        return AnalyzeFlowImpl(code, indirect_targets, per_branch, false);
    } catch (const UnsupportedError& error) {
        const std::string_view what{error.what()};
        if (what.find("reconvergence") == std::string_view::npos) {
            throw;
        }
    }
    return AnalyzeFlowImpl(code, indirect_targets, per_branch, true);
}

namespace {
struct Builder {
    ObjectPool<Flow::Block>& block_pool;
    std::span<const Instruction> code;
    const FlowAnalysis& analysis;
    std::vector<Flow::Function*> functions;
    Flow::Function* function{};
    u32 source_base;
    std::vector<Flow::Block*> entry_block; // block control enters when branching to instruction i

    Flow::Block* NewBlock(Location begin, Location end, Flow::EndClass end_class, IR::Condition cond,
                          Flow::Block* branch_true, Flow::Block* branch_false) {
        Flow::Block* const block = block_pool.Create();
        *block = Flow::Block{};
        block->begin = begin;
        block->end = end;
        block->end_class = end_class;
        block->cond = cond;
        block->branch_true = branch_true;
        block->branch_false = branch_false;
        function->blocks.insert(*block);
        return block;
    }

    void Fill(Flow::Block* block, Location begin, Location end, Flow::EndClass end_class,
              IR::Condition cond, Flow::Block* branch_true, Flow::Block* branch_false) {
        *block = Flow::Block{};
        block->begin = begin;
        block->end = end;
        block->end_class = end_class;
        block->cond = cond;
        block->branch_true = branch_true;
        block->branch_false = branch_false;
        function->blocks.insert(*block);
    }

    void Build() {
        const u32 count = static_cast<u32>(code.size());
        entry_block.assign(count, nullptr);
        // Blocks are created first so branches can refer to them; contents are filled afterwards.
        for (u32 index = 0; index < count; ++index) {
            if (analysis.leader[index] && analysis.reachable[index]) {
                entry_block[index] = block_pool.Create();
            }
        }
        for (u32 leader = 0; leader < count; ++leader) {
            if (entry_block[leader]) {
                BuildFrom(leader);
            }
        }
    }

    void BuildFrom(u32 leader) {
        const u32 count = static_cast<u32>(code.size());
        Flow::Block* const block = entry_block[leader];
        function = functions[analysis.function_of[leader]];
        u32 index = leader;
        while (true) {
            if (index >= count) {
                throw UnsupportedError(count - 1, "native program has no terminating EXIT");
            }
            if (index != leader && analysis.leader[index]) {
                // Fall through into the next block.
                Fill(block, At(leader), At(index), Flow::EndClass::Branch, IR::Condition{true},
                     entry_block[index], nullptr);
                return;
            }
            const Instruction& insn = code[index];
            OpInfo info = Classify(insn.Bits(0, 12));
            if (info.kind == OpKind::Call && IsSkipCall(code, index)) {
                info.kind = OpKind::Bra;
            }
            const Guard guard = GuardOf(insn);
            const Guard second = SecondOf(insn);
            if (guard.Never()) {
                ++index;
                continue;
            }
            const bool conditional_inst =
                (info.kind == OpKind::Plain && !info.pure && !guard.Always()) ||
                info.kind == OpKind::Kill ||
                (info.kind == OpKind::Call && !guard.Always()) ||
                (info.kind == OpKind::Exit && !(guard.Always() && second.Always()));
            if (conditional_inst) {
                // index == leader here. A virtual block tests the predicate, the conditional block
                // holds the instruction, and both continue at the next instruction.
                if (index + 1 >= count) {
                    throw UnsupportedError(index, "native program has no terminating EXIT");
                }
                Flow::Block* const next = entry_block[index + 1];
                Flow::EndClass end_class = Flow::EndClass::Branch;
                if (info.kind == OpKind::Kill) end_class = Flow::EndClass::Kill;
                if (info.kind == OpKind::Exit) end_class = Flow::EndClass::Exit;
                if (info.kind == OpKind::Call) end_class = Flow::EndClass::Call;
                Flow::Block* const conditional = NewBlock(
                    At(index), info.kind == OpKind::Call ? At(index) : At(index + 1), end_class,
                    IR::Condition{true}, end_class == Flow::EndClass::Exit ? nullptr : next, nullptr);
                if (info.kind == OpKind::Call) {
                    conditional->branch_true = nullptr;
                    conditional->function_call = analysis.call_function.at(index);
                    conditional->return_block = next;
                }
                if (info.kind == OpKind::Exit && !guard.Always() && !second.Always()) {
                    // Exit only when both predicates hold: test the guard, then the second predicate in
                    // a block placed at the virtual location of the next instruction.
                    const Location second_begin = At(index + 1).Virtual();
                    Flow::Block* const second_test = NewBlock(second_begin, second_begin, Flow::EndClass::Branch,
                                                              CondOf(second), conditional, next);
                    const Location first_begin = At(index).Virtual();
                    Fill(block, first_begin, first_begin, Flow::EndClass::Branch, CondOf(guard),
                         second_test, next);
                    return;
                }
                IR::Condition cond{true};
                if (info.kind == OpKind::Exit) {
                    cond = CondOf(guard.Always() ? second : guard);
                } else if (!guard.Always()) {
                    cond = CondOf(guard);
                }
                const Location virtual_begin = At(index).Virtual();
                Fill(block, virtual_begin, virtual_begin, Flow::EndClass::Branch, cond, conditional,
                     cond == IR::Condition{true} ? nullptr : next);
                return;
            }
            switch (info.kind) {
            case OpKind::Exit:
                Fill(block, At(leader), At(index + 1), Flow::EndClass::Exit, IR::Condition{true},
                     nullptr, nullptr);
                return;
            case OpKind::Bra: {
                Flow::Block* const target = entry_block[*BranchTarget(insn, index)];
                if (guard.Always() && second.Always()) {
                    Fill(block, At(leader), At(index), Flow::EndClass::Branch, IR::Condition{true},
                         target, nullptr);
                } else if (second.Always()) {
                    Fill(block, At(leader), At(index), Flow::EndClass::Branch, CondOf(guard), target,
                         entry_block[index + 1]);
                } else if (guard.Always()) {
                    Fill(block, At(leader), At(index), Flow::EndClass::Branch, CondOf(second), target,
                         entry_block[index + 1]);
                } else {
                    // guard AND second: test the guard first, then the extra predicate in a virtual block.
                    Flow::Block* const next = entry_block[index + 1];
                    const Location virtual_begin = At(index).Virtual();
                    Flow::Block* const extra = NewBlock(virtual_begin, virtual_begin,
                                                        Flow::EndClass::Branch, CondOf(second), target, next);
                    Fill(block, At(leader), At(index), Flow::EndClass::Branch, CondOf(guard), extra, next);
                }
                return;
            }
            case OpKind::Bsync:
                Fill(block, At(leader), At(index), Flow::EndClass::Branch, IR::Condition{true},
                     entry_block[analysis.sync_target.at(index)], nullptr);
                return;
            case OpKind::Call:
                Fill(block, At(leader), At(index), Flow::EndClass::Call, IR::Condition{true}, nullptr,
                     nullptr);
                block->function_call = analysis.call_function.at(index);
                block->return_block = entry_block[index + 1];
                return;
            case OpKind::Ret:
                if (guard.Always()) {
                    Fill(block, At(leader), At(index), Flow::EndClass::Return, IR::Condition{true},
                         nullptr, nullptr);
                } else {
                    Fill(block, At(leader), At(index), Flow::EndClass::Return, CondOf(guard), nullptr,
                         entry_block[index + 1]);
                }
                return;
            case OpKind::Brx: {
                Fill(block, At(leader), At(index + 1), Flow::EndClass::IndirectBranch,
                     IR::Condition{true}, nullptr, nullptr);
                if (insn.Bits(0, 12) == 0x958) {
                    const u32 ur = insn.Bits(24, 6);
                    if (ur >= 63) throw UnsupportedError(index, "native computed branch through URZ");
                    const UniformLayout layout = LayoutOf(code);
                    block->branch_reg = static_cast<IR::Reg>(layout.scattered ? layout.slots[ur]
                                                                              : layout.register_base + ur);
                } else {
                    block->branch_reg = static_cast<IR::Reg>(insn.Bits(24, 8));
                }
                block->branch_offset = 0;
                for (const u32 target : analysis.indirect_tables.at(index)) {
                    block->indirect_branches.push_back(
                        {.block = entry_block[target], .address = source_base + target * 16});
                }
                return;
            }
            default:
                ++index;
                break;
            }
        }
    }
};
} // Anonymous namespace

void CfgBuilder::Build(Flow::CFG& cfg) {
    Environment& env = cfg.env;
    if (cfg.exits_to_dispatcher) {
        throw UnsupportedError(0, "native program in a dispatcher context");
    }
    if (cfg.program_start.Offset() != LocationOf(0)) {
        throw UnsupportedError(0, "native program start address");
    }
    const std::span<const Instruction> code = env.NativeProgram();
    const IndirectTargetProvider per_branch = [&env](u32 index) { return env.NativeIndirectTargetsAt(index); };
    const FlowAnalysis analysis = AnalyzeFlow(code, env.NativeIndirectTargets(), &per_branch);
    cfg.functions.emplace_back(cfg.block_pool, cfg.program_start);
    for (size_t f = 1; f < analysis.function_entry.size(); ++f) {
        cfg.functions.emplace_back(cfg.block_pool, At(analysis.function_entry[f]));
    }
    std::vector<Flow::Function*> function_ptrs;
    for (auto& fn : cfg.functions) {
        fn.labels.clear();
        function_ptrs.push_back(&fn);
    }
    Builder builder{cfg.block_pool, code, analysis, function_ptrs, nullptr, env.NativeSourceBase(), {}};
    builder.Build();
}

} // namespace Shader::Ampere
