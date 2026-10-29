// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include "shader_recompiler/ir_opt/conditional_phi.h"

namespace Shader::Optimization {
// A bounded, conservative proof for paths within one execution of a dominating block.
class ControlFlowReachability {
    struct Limit {};
    struct Node { u32 variable, low, high; };
    class Booleans {
    public:
        u32 Atom() {
            if (variables == 256) throw Limit{};
            return Make(variables++, 0, 1);
        }
        u32 Not(u32 value) {
            if (value < 2) return 1 - value;
            if (const auto found = negations.find(value); found != negations.end()) return found->second;
            const auto node = nodes[value];
            const u32 result = Make(node.variable, Not(node.low), Not(node.high));
            negations.emplace(value, result);
            return result;
        }
        u32 And(u32 a, u32 b) { return Apply(false, a, b); }
        u32 Or(u32 a, u32 b) { return Apply(true, a, b); }
    private:
        u32 Make(u32 variable, u32 low, u32 high) {
            if (low == high) return low;
            const auto key = std::tuple{variable, low, high};
            if (const auto found = unique.find(key); found != unique.end()) return found->second;
            if (nodes.size() == 32768) throw Limit{};
            const u32 result = static_cast<u32>(nodes.size());
            nodes.push_back({variable, low, high});
            unique.emplace(key, result);
            return result;
        }
        u32 Apply(bool disjunction, u32 a, u32 b) {
            if (a > b) std::swap(a, b);
            if (a == b) return a;
            if (disjunction) { if (a == 0) return b; if (a == 1) return 1; }
            else { if (a == 0) return 0; if (a == 1) return b; }
            const auto key = std::tuple{disjunction, a, b};
            if (const auto found = applications.find(key); found != applications.end()) return found->second;
            const auto left = nodes[a], right = nodes[b];
            const u32 variable = std::min(left.variable, right.variable);
            const u32 low = Apply(disjunction, left.variable == variable ? left.low : a,
                                  right.variable == variable ? right.low : b);
            const u32 high = Apply(disjunction, left.variable == variable ? left.high : a,
                                   right.variable == variable ? right.high : b);
            const u32 result = Make(variable, low, high);
            applications.emplace(key, result);
            return result;
        }
        u32 variables{};
        std::vector<Node> nodes{{~0U, 0, 0}, {~0U, 1, 1}};
        std::map<std::tuple<u32, u32, u32>, u32> unique;
        std::map<std::tuple<bool, u32, u32>, u32> applications;
        std::unordered_map<u32, u32> negations;
    } booleans;

public:
    using ConstantReader = std::function<std::optional<u32>(IR::Value)>;
    ControlFlowReachability(const IR::Program& program, const IR::Block* root_, const IR::Block* use_,
                            ConstantReader reader_ = {})
        : root{root_}, use{use_}, reader{std::move(reader_)} {
        try { Build(program); valid = true; } catch (const Limit&) {}
    }

    bool ExcludesEdge(const IR::Block* from, const IR::Block* to) {
        if (!valid) return false;
        const auto found = edges.find({from, to});
        if (found == edges.end()) return false;
        try {
            return booleans.And(constraints, booleans.And(found->second, reach.at(use))) == 0;
        } catch (const Limit&) { valid = false; return false; }
    }


    bool IsUnreachable() {
        if (!valid) return false;
        try { return booleans.And(constraints, reach.at(use)) == 0; }
        catch (const Limit&) { valid = false; return false; }
    }

    bool ExcludesEquality(IR::Value value, u32 constant, bool not_equal = false) {
        if (!valid) return false;
        try {
            const u32 equal = Equality(value, constant);
            const u32 condition = not_equal ? booleans.Not(equal) : equal;
            return booleans.And(constraints, booleans.And(condition, reach.at(use))) == 0;
        } catch (const Limit&) { valid = false; return false; }
    }

private:
    u32 Equality(IR::Value value, u32 constant) {
        value = value.Resolve();
        if (value.Type() != IR::Type::U32) throw Limit{};
        if (value.IsImmediate()) return value.U32() == constant ? 1 : 0;
        if (reader) if (const auto known = reader(value)) return *known == constant ? 1 : 0;
        const auto* inst = value.TryInstRecursive();
        if (!inst) throw Limit{};
        auto& choices = equalities[inst];
        if (const auto found = choices.find(constant); found != choices.end()) return found->second;
        const auto key = std::pair{inst, constant};
        if (!active_equalities.insert(key).second || active_equalities.size() > 256) throw Limit{};
        u32 result = booleans.Atom();
        const auto owner = owners.find(inst);
        if (inst->GetOpcode() == IR::Opcode::Phi && owner != owners.end() &&
            owner->second != root && region.contains(owner->second)) {
            u32 merged{};
            bool complete = true;
            for (size_t i = 0; i < inst->NumArgs(); ++i) {
                const auto edge = edges.find({inst->PhiBlock(i), owner->second});
                if (edge == edges.end()) { complete = false; break; }
                merged = booleans.Or(merged, booleans.And(edge->second, Equality(inst->Arg(i), constant)));
            }
            if (complete) result = merged;
        }
        // Distinct integer constants are mutually exclusive for one SSA value.
        for (const auto& [other_constant, other] : choices) {
            (void)other_constant;
            constraints = booleans.And(constraints, booleans.Not(booleans.And(result, other)));
        }
        choices.emplace(constant, result);
        active_equalities.erase(key);
        return result;
    }

    u32 Atom(const IR::Inst* inst) {
        const auto found = expressions.find(inst);
        if (found != expressions.end()) return found->second;
        const u32 result = booleans.Atom();
        expressions.emplace(inst, result);
        return result;
    }

    u32 Expression(IR::Value value) {
        value = ConditionalPhi::Resolve(value);
        if (value.IsImmediate()) {
            if (value.Type() != IR::Type::U1) throw Limit{};
            return value.U1() ? 1 : 0;
        }
        const auto* inst = value.TryInstRecursive();
        if (!inst) throw Limit{};
        if (const auto found = expressions.find(inst); found != expressions.end()) return found->second;
        if (!active.insert(inst).second || active.size() > 256) throw Limit{};
        u32 result{};
        switch (inst->GetOpcode()) {
        case IR::Opcode::LogicalNot: result = booleans.Not(Expression(inst->Arg(0))); break;
        case IR::Opcode::LogicalAnd: result = booleans.And(Expression(inst->Arg(0)), Expression(inst->Arg(1))); break;
        case IR::Opcode::LogicalOr: result = booleans.Or(Expression(inst->Arg(0)), Expression(inst->Arg(1))); break;
        case IR::Opcode::LogicalXor: {
            const u32 a = Expression(inst->Arg(0)), b = Expression(inst->Arg(1));
            result = booleans.Or(booleans.And(a, booleans.Not(b)), booleans.And(booleans.Not(a), b));
            break;
        }
        case IR::Opcode::IEqual:
        case IR::Opcode::INotEqual: {
            IR::Value a = inst->Arg(0).Resolve(), b = inst->Arg(1).Resolve();
            if (a.IsImmediate()) std::swap(a, b);
            if (!a.IsImmediate() && b.IsImmediate() && b.Type() == IR::Type::U32) {
                result = Equality(a, b.U32());
                if (inst->GetOpcode() == IR::Opcode::INotEqual) result = booleans.Not(result);
            } else result = Atom(inst);
            break;
        }
        case IR::Opcode::Phi: {
            const auto found_owner = owners.find(inst);
            if (found_owner == owners.end() || found_owner->second == root || !region.contains(found_owner->second)) {
                result = Atom(inst);
                break;
            }
            result = 0;
            for (size_t i = 0; i < inst->NumArgs(); ++i) {
                const auto edge = edges.find({inst->PhiBlock(i), found_owner->second});
                if (edge == edges.end()) { result = Atom(inst); break; }
                result = booleans.Or(result, booleans.And(edge->second, Expression(inst->Arg(i))));
            }
            break;
        }
        default: result = Atom(inst); break;
        }
        active.erase(inst);
        expressions.emplace(inst, result);
        return result;
    }

    void Build(const IR::Program& program) {
        if (program.blocks.empty() || !root || !use) throw Limit{};
        // If an entry-to-use path bypasses root, its values cannot be specialized here.
        std::vector<const IR::Block*> pending{program.blocks.front()};
        std::unordered_set<const IR::Block*> visited;
        while (!pending.empty()) {
            const auto* block = pending.back(); pending.pop_back();
            if (block == root || !visited.insert(block).second) continue;
            if (block == use) throw Limit{};
            for (const auto* next : block->ImmSuccessors()) pending.push_back(next);
        }
        pending = {use};
        std::unordered_set<const IR::Block*> ancestors;
        while (!pending.empty()) {
            const auto* block = pending.back(); pending.pop_back();
            if (!ancestors.insert(block).second || block == root) continue;
            for (const auto* previous : block->ImmPredecessors()) pending.push_back(previous);
        }
        pending = {root};
        while (!pending.empty()) {
            const auto* block = pending.back(); pending.pop_back();
            if (!ancestors.contains(block) || !region.insert(block).second || block == use) continue;
            for (const auto* next : block->ImmSuccessors()) if (next != root) pending.push_back(next);
        }
        if (!region.contains(use) || region.size() > 1024) throw Limit{};
        pending.assign(use->ImmSuccessors().begin(), use->ImmSuccessors().end());
        visited.clear();
        while (!pending.empty()) {
            const auto* block = pending.back(); pending.pop_back();
            if (block == root || !visited.insert(block).second) continue;
            if (region.contains(block)) throw Limit{};
            for (const auto* next : block->ImmSuccessors()) pending.push_back(next);
        }
        std::unordered_map<const IR::Block*, size_t> degree;
        for (const auto* block : region) {
            degree[block] = 0;
            if (block != root) for (const auto* previous : block->ImmPredecessors())
                if (region.contains(previous) && previous != use) ++degree[block];
        }
        std::vector<const IR::Block*> order;
        for (const auto& [block, count] : degree) if (!count) order.push_back(block);
        for (size_t i = 0; i < order.size(); ++i) {
            const auto* block = order[i];
            if (block == use) continue;
            for (const auto* next : block->ImmSuccessors())
                if (next != root && region.contains(next) && --degree[next] == 0) order.push_back(next);
        }
        if (order.size() != region.size()) throw Limit{};
        for (const auto* block : program.blocks)
            for (const auto& inst : block->Instructions()) owners.emplace(&inst, block);
        const auto branches = ConditionalPhi::FindBranches(program);
        for (const auto* block : order) {
            u32 reached = block == root ? 1 : 0;
            if (block != root) for (const auto* previous : block->ImmPredecessors())
                if (const auto found = edges.find({previous, block}); found != edges.end()) reached = booleans.Or(reached, found->second);
            reach.emplace(block, reached);
            if (block == use) continue;
            const auto successors = block->ImmSuccessors();
            if (successors.size() > 2) throw Limit{};
            u32 condition = 1;
            const auto branch = branches.find(block);
            if (successors.size() == 2) condition = branch != branches.end() ? Expression(branch->second.condition) : booleans.Atom();
            for (size_t i = 0; i < successors.size(); ++i) {
                const auto* next = successors[i];
                if (next == root || !region.contains(next)) continue;
                const bool yes = branch != branches.end() ? next == branch->second.yes : i == 0;
                const u32 edge = successors.size() == 1 || yes ? condition : booleans.Not(condition);
                edges.emplace(std::pair{block, next}, booleans.And(reached, edge));
            }
        }
    }

    const IR::Block* root;
    const IR::Block* use;
    bool valid{};
    ConstantReader reader;
    u32 constraints{1};
    std::unordered_set<const IR::Block*> region;
    std::unordered_set<const IR::Inst*> active;
    std::unordered_map<const IR::Inst*, const IR::Block*> owners;
    std::unordered_map<const IR::Inst*, u32> expressions;
    std::unordered_map<const IR::Block*, u32> reach;
    std::map<std::pair<const IR::Block*, const IR::Block*>, u32> edges;
    std::map<const IR::Inst*, std::map<u32, u32>> equalities;
    std::set<std::pair<const IR::Inst*, u32>> active_equalities;
};
} // namespace Shader::Optimization
