// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "shader_recompiler/frontend/ir/program.h"

namespace Shader::Optimization {
namespace ConditionalPhi {
struct Branch {
    IR::Value condition;
    const IR::Block* yes;
    const IR::Block* no;
};
using Branches = std::unordered_map<const IR::Block*, Branch>;
using Facts = std::unordered_map<const IR::Inst*, bool>;

inline IR::Value Resolve(IR::Value value) {
    value = value.Resolve();
    while (const auto* inst = value.TryInstRecursive()) {
        if (inst->GetOpcode() != IR::Opcode::ConditionRef) break;
        value = inst->Arg(0).Resolve();
    }
    return value;
}

inline Branches FindBranches(const IR::Program& program) {
    std::unordered_map<const IR::Inst*, const IR::Block*> owners;
    for (const auto* block : program.blocks)
        for (const auto& inst : block->Instructions()) owners.emplace(&inst, block);
    Branches result;
    for (const auto& node : program.syntax_list) {
        using T = IR::AbstractSyntaxNode::Type;
        Branch branch;
        switch (node.type) {
        case T::If: branch = {node.data.if_node.cond, node.data.if_node.body, node.data.if_node.merge}; break;
        case T::Repeat: branch = {node.data.repeat.cond, node.data.repeat.loop_header, node.data.repeat.merge}; break;
        case T::Break: branch = {node.data.break_node.cond, node.data.break_node.merge, node.data.break_node.skip}; break;
        default: continue;
        }
        const auto found = owners.find(branch.condition.TryInstRecursive());
        if (found != owners.end() && branch.yes != branch.no &&
            found->second->ImmSuccessors().size() == 2 &&
            std::ranges::find(found->second->ImmSuccessors(), branch.yes) != found->second->ImmSuccessors().end() &&
            std::ranges::find(found->second->ImmSuccessors(), branch.no) != found->second->ImmSuccessors().end())
            result.emplace(found->second, branch);
    }
    return result;
}

inline Facts EdgeFacts(const Branch& branch, const IR::Block* destination) {
    if (destination != branch.yes && destination != branch.no) return {};
    std::vector<std::pair<IR::Value, bool>> pending{{branch.condition, destination == branch.yes}};
    Facts facts;
    while (!pending.empty()) {
        const auto [value, truth] = pending.back();
        pending.pop_back();
        const auto resolved = Resolve(value);
        if (resolved.IsImmediate()) {
            if (resolved.Type() == IR::Type::U1 && resolved.U1() != truth) return {};
            continue;
        }
        const auto* inst = resolved.TryInstRecursive();
        if (!inst) continue;
        const auto [found, inserted] = facts.emplace(inst, truth);
        if (!inserted) {
            if (found->second != truth) return {};
            continue;
        }
        if (inst->GetOpcode() == IR::Opcode::LogicalNot)
            pending.emplace_back(inst->Arg(0), !truth);
        if ((inst->GetOpcode() == IR::Opcode::LogicalAnd && truth) ||
            (inst->GetOpcode() == IR::Opcode::LogicalOr && !truth)) {
            pending.emplace_back(inst->Arg(0), truth);
            pending.emplace_back(inst->Arg(1), truth);
        }
    }
    return facts;
}

enum class State { Bottom, False, True, Varying };
inline State Join(State a, State b) {
    if (a == State::Bottom) return b;
    if (b == State::Bottom || a == b) return a;
    return State::Varying;
}

inline size_t Fold(IR::Program& program) {
    const auto branches = FindBranches(program);
    struct Input { IR::Value value; std::optional<bool> fact; };
    struct Phi { IR::Inst* inst; std::vector<Input> inputs; State state{State::Bottom}; };
    std::vector<Phi> phis;
    std::unordered_map<const IR::Inst*, size_t> indices;
    for (auto* block : program.blocks) for (auto& inst : block->Instructions()) {
        if (inst.GetOpcode() != IR::Opcode::Phi || inst.Type() != IR::Type::U1) continue;
        Phi phi{&inst, {}};
        for (size_t i = 0; i < inst.NumArgs(); ++i) {
            Input input{Resolve(inst.Arg(i)), {}};
            if (const auto branch = branches.find(inst.PhiBlock(i)); branch != branches.end()) {
                const auto facts = EdgeFacts(branch->second, block);
                if (const auto fact = facts.find(input.value.TryInstRecursive()); fact != facts.end())
                    input.fact = fact->second;
            }
            phi.inputs.push_back(input);
        }
        indices.emplace(&inst, phis.size());
        phis.push_back(std::move(phi));
    }
    const auto propagate = [&] {
        bool changed;
        do {
            changed = false;
            for (auto& phi : phis) {
                State joined = State::Bottom;
                for (const auto& input : phi.inputs) {
                    State value = State::Varying;
                    if (input.fact) value = *input.fact ? State::True : State::False;
                    else if (input.value.IsImmediate() && input.value.Type() == IR::Type::U1)
                        value = input.value.U1() ? State::True : State::False;
                    else if (const auto found = indices.find(input.value.TryInstRecursive()); found != indices.end())
                        value = phis[found->second].state;
                    joined = Join(joined, value);
                }
                joined = Join(phi.state, joined);
                if (joined != phi.state) { phi.state = joined; changed = true; }
            }
        } while (changed);
    };
    propagate();
    // A cycle without an incoming definition cannot establish a constant.
    for (auto& phi : phis) if (phi.state == State::Bottom) phi.state = State::Varying;
    propagate();
    size_t folded{};
    std::unordered_set<IR::Inst*> moved;
    for (auto& phi : phis) if (phi.state == State::True || phi.state == State::False) {
        phi.inst->ReplaceUsesWith(IR::Value{phi.state == State::True});
        moved.insert(phi.inst);
        ++folded;
    }
    // Phi instructions must remain before the identities replacing folded Phis.
    for (auto* block : program.blocks) {
        auto& list = block->Instructions();
        std::vector<IR::Inst*> identities;
        for (auto it = list.begin(); it != list.end();) {
            auto& inst = *it++;
            if (!moved.contains(&inst)) continue;
            identities.push_back(&inst);
            list.erase(IR::Block::InstructionList::s_iterator_to(inst));
        }
        const auto position = std::ranges::find_if_not(list, [](const auto& inst) {
            return inst.GetOpcode() == IR::Opcode::Phi;
        });
        for (auto* identity : identities) list.insert(position, *identity);
    }
    return folded;
}
} // namespace ConditionalPhi
} // namespace Shader::Optimization
