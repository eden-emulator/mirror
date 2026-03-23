// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <memory>
#include <string>
#include <ankerl/unordered_dense.h>
#include <utility>
#include <vector>

#include <fmt/ranges.h>

#include <ranges>
#include "shader_recompiler/shader_pool.h"
#include "shader_recompiler/environment.h"
#include "shader_recompiler/frontend/ir/basic_block.h"
#include "shader_recompiler/frontend/ir/ir_emitter.h"
#include "shader_recompiler/frontend/maxwell/structured_control_flow.h"
#include "shader_recompiler/frontend/maxwell/translate/translate.h"
#include "shader_recompiler/frontend/maxwell/translate_program.h"
#include "shader_recompiler/frontend/maxwell/control_flow.h"
#include "shader_recompiler/host_translate_info.h"

namespace Shader::Maxwell {
namespace {

std::string DumpExpr(const Statement* stmt) {
    switch (stmt->type) {
    case StatementType::Identity:
        return fmt::format("{}", stmt->guest_cond);
    case StatementType::Not:
        return fmt::format("!{}", DumpExpr(stmt->op));
    case StatementType::Or:
        return fmt::format("{} || {}", DumpExpr(stmt->op_a), DumpExpr(stmt->op_b));
    case StatementType::Variable:
        return fmt::format("goto_L{}", stmt->id);
    case StatementType::IndirectBranchCond:
        return fmt::format("(indirect_branch == {:x})", stmt->location);
    default:
        return "<invalid type>";
    }
}

[[maybe_unused]] std::string DumpTree(const Tree& tree, u32 indentation = 0) {
    std::string ret;
    std::string indent(indentation, ' ');
    for (auto stmt = tree.begin(); stmt != tree.end(); ++stmt) {
        switch (stmt->type) {
        case StatementType::Code:
            ret += fmt::format("{}    Block {:04x} -> {:04x} (0x{:016x});\n", indent,
                               stmt->block->begin.Offset(), stmt->block->end.Offset(),
                               reinterpret_cast<uintptr_t>(stmt->block));
            break;
        case StatementType::Goto:
            ret += fmt::format("{}    if ({}) goto L{};\n", indent, DumpExpr(stmt->cond),
                               stmt->label->id);
            break;
        case StatementType::Label:
            ret += fmt::format("{}L{}:\n", indent, stmt->id);
            break;
        case StatementType::If:
            ret += fmt::format("{}    if ({}) {{\n", indent, DumpExpr(stmt->cond));
            ret += DumpTree(stmt->children, indentation + 4);
            ret += fmt::format("{}    }}\n", indent);
            break;
        case StatementType::Loop:
            ret += fmt::format("{}    do {{\n", indent);
            ret += DumpTree(stmt->children, indentation + 4);
            ret += fmt::format("{}    }} while ({});\n", indent, DumpExpr(stmt->cond));
            break;
        case StatementType::Break:
            ret += fmt::format("{}    if ({}) break;\n", indent, DumpExpr(stmt->cond));
            break;
        case StatementType::Return:
            ret += fmt::format("{}    return;\n", indent);
            break;
        case StatementType::Kill:
            ret += fmt::format("{}    kill;\n", indent);
            break;
        case StatementType::Unreachable:
            ret += fmt::format("{}    unreachable;\n", indent);
            break;
        case StatementType::SetVariable:
            ret += fmt::format("{}    goto_L{} = {};\n", indent, stmt->id, DumpExpr(stmt->op));
            break;
        case StatementType::SetIndirectBranchVariable:
            ret += fmt::format("{}    indirect_branch = {} + {};\n", indent, stmt->branch_reg,
                               stmt->branch_offset);
            break;
        case StatementType::Function:
        case StatementType::Identity:
        case StatementType::Not:
        case StatementType::Or:
        case StatementType::Variable:
        case StatementType::IndirectBranchCond:
            throw LogicError("Statement can't be printed");
        }
    }
    return ret;
}

void SanitizeNoBreaks(const Tree& tree) {
    if (std::ranges::find(tree, StatementType::Break, &Statement::type) != tree.end()) {
        throw NotImplementedException("Capturing statement with break nodes");
    }
}

size_t Level(Node stmt) {
    size_t level{0};
    Statement* node{stmt->up};
    while (node) {
        ++level;
        node = node->up;
    }
    return level;
}

bool IsDirectlyRelated(Node goto_stmt, Node label_stmt) {
    const size_t goto_level{Level(goto_stmt)};
    const size_t label_level{Level(label_stmt)};
    size_t min_level;
    size_t max_level;
    Node min;
    Node max;
    if (label_level < goto_level) {
        min_level = label_level;
        max_level = goto_level;
        min = label_stmt;
        max = goto_stmt;
    } else { // goto_level < label_level
        min_level = goto_level;
        max_level = label_level;
        min = goto_stmt;
        max = label_stmt;
    }
    while (max_level > min_level) {
        --max_level;
        max = max->up;
    }
    return min->up == max->up;
}

bool IsIndirectlyRelated(Node goto_stmt, Node label_stmt) {
    return goto_stmt->up != label_stmt->up && !IsDirectlyRelated(goto_stmt, label_stmt);
}

[[maybe_unused]] bool AreSiblings(Node goto_stmt, Node label_stmt) noexcept {
    Node it{goto_stmt};
    do {
        if (it == label_stmt) {
            return true;
        }
        --it;
    } while (it != goto_stmt->up->children.begin());
    while (it != goto_stmt->up->children.end()) {
        if (it == label_stmt) {
            return true;
        }
        ++it;
    }
    return false;
}

Node SiblingFromNephew(Node uncle, Node nephew) noexcept {
    Statement* const parent{uncle->up};
    Statement* it{&*nephew};
    while (it->up != parent) {
        it = it->up;
    }
    return Tree::s_iterator_to(*it);
}

bool AreOrdered(Node left_sibling, Node right_sibling) noexcept {
    const Node end{right_sibling->up->children.end()};
    for (auto it = right_sibling; it != end; ++it) {
        if (it == left_sibling) {
            return false;
        }
    }
    return true;
}

bool NeedsLift(Node goto_stmt, Node label_stmt) noexcept {
    const Node sibling{SiblingFromNephew(goto_stmt, label_stmt)};
    return AreOrdered(sibling, goto_stmt);
}

class GotoPass {
public:
    explicit GotoPass(Flow::CFG& cfg, ShaderPools& pools_)
        : pools{pools_}
    {
        std::vector gotos{BuildTree(cfg)};
        const auto end{gotos.rend()};
        for (auto goto_stmt = gotos.rbegin(); goto_stmt != end; ++goto_stmt) {
            RemoveGoto(*goto_stmt);
        }
    }

    Statement& RootStatement() noexcept {
        return root_stmt;
    }

private:
    void RemoveGoto(Node goto_stmt) {
        // Force goto_stmt and label_stmt to be directly related
        const Node label_stmt{goto_stmt->label};
        if (IsIndirectlyRelated(goto_stmt, label_stmt)) {
            // Move goto_stmt out using outward-movement transformation until it becomes
            // directly related to label_stmt
            while (!IsDirectlyRelated(goto_stmt, label_stmt)) {
                goto_stmt = MoveOutward(goto_stmt);
            }
        }
        // Force goto_stmt and label_stmt to be siblings
        if (IsDirectlyRelated(goto_stmt, label_stmt)) {
            const size_t label_level{Level(label_stmt)};
            size_t goto_level{Level(goto_stmt)};
            if (goto_level > label_level) {
                // Move goto_stmt out of its level using outward-movement transformations
                while (goto_level > label_level) {
                    goto_stmt = MoveOutward(goto_stmt);
                    --goto_level;
                }
            } else { // Level(goto_stmt) < Level(label_stmt)
                if (NeedsLift(goto_stmt, label_stmt)) {
                    // Lift goto_stmt to above stmt containing label_stmt using goto-lifting
                    // transformations
                    goto_stmt = Lift(goto_stmt);
                }
                // Move goto_stmt into label_stmt's level using inward-movement transformation
                while (goto_level < label_level) {
                    goto_stmt = MoveInward(goto_stmt);
                    ++goto_level;
                }
            }
        }
        // Expensive operation:
        // if (!AreSiblings(goto_stmt, label_stmt)) {
        //     throw LogicError("Goto is not a sibling with the label");
        // }
        // goto_stmt and label_stmt are guaranteed to be siblings, eliminate
        if (std::next(goto_stmt) == label_stmt) {
            // Simply eliminate the goto if the label is next to it
            goto_stmt->up->children.erase(goto_stmt);
        } else if (AreOrdered(goto_stmt, label_stmt)) {
            // Eliminate goto_stmt with a conditional
            EliminateAsConditional(goto_stmt, label_stmt);
        } else {
            // Eliminate goto_stmt with a loop
            EliminateAsLoop(goto_stmt, label_stmt);
        }
    }

    std::vector<Node> BuildTree(Flow::CFG& cfg) {
        u32 label_id{0};
        std::vector<Node> gotos;
        Flow::Function& first_function{cfg.Functions().front()};
        BuildTree(cfg, first_function, label_id, gotos, root_stmt.children.end(), std::nullopt);
        return gotos;
    }

    void BuildTree(Flow::CFG& cfg, Flow::Function& function, u32& label_id, std::vector<Node>& gotos, Node function_insert_point, std::optional<Node> return_label) {
        Statement* const false_stmt{&pools.stmt.emplace_back(Identity{}, IR::Condition{false}, &root_stmt)};
        Tree& root{root_stmt.children};
        ankerl::unordered_dense::map<Flow::Block*, Node> local_labels;
        local_labels.reserve(function.blocks.size());

        for (Flow::Block& block : function.blocks) {
            Statement* const label{&pools.stmt.emplace_back(Label{}, label_id, &root_stmt)};
            const Node label_it{root.insert(function_insert_point, *label)};
            local_labels.emplace(&block, label_it);
            ++label_id;
        }
        for (Flow::Block& block : function.blocks) {
            const Node label{local_labels.at(&block)};
            // Insertion point
            const Node ip{std::next(label)};

            // Reset goto variables before the first block and after its respective label
            const auto make_reset_variable{[&]() -> Statement& {
                return pools.stmt.emplace_back(SetVariable{}, label->id, false_stmt, &root_stmt);
            }};
            root.push_front(make_reset_variable());
            root.insert(ip, make_reset_variable());
            root.insert(ip, pools.stmt.emplace_back(&block, &root_stmt));

            switch (block.end_class) {
            case Flow::EndClass::Branch: {
                Statement* const always_cond{&pools.stmt.emplace_back(Identity{}, IR::Condition{true}, &root_stmt)};
                if (block.cond == IR::Condition{true}) {
                    const Node true_label{local_labels.at(block.branch_true)};
                    gotos.push_back(root.insert(ip, pools.stmt.emplace_back(Goto{}, always_cond, true_label, &root_stmt)));
                } else if (block.cond == IR::Condition{false}) {
                    const Node false_label{local_labels.at(block.branch_false)};
                    gotos.push_back(root.insert(ip, pools.stmt.emplace_back(Goto{}, always_cond, false_label, &root_stmt)));
                } else {
                    const Node true_label{local_labels.at(block.branch_true)};
                    const Node false_label{local_labels.at(block.branch_false)};
                    Statement* const true_cond{&pools.stmt.emplace_back(Identity{}, block.cond, &root_stmt)};
                    gotos.push_back(root.insert(ip, pools.stmt.emplace_back(Goto{}, true_cond, true_label, &root_stmt)));
                    gotos.push_back(root.insert(ip, pools.stmt.emplace_back(Goto{}, always_cond, false_label, &root_stmt)));
                }
                break;
            }
            case Flow::EndClass::IndirectBranch:
                root.insert(ip, pools.stmt.emplace_back(SetIndirectBranchVariable{}, block.branch_reg, block.branch_offset, &root_stmt));
                for (const Flow::IndirectBranch& indirect : block.indirect_branches) {
                    const Node indirect_label{local_labels.at(indirect.block)};
                    Statement* cond{&pools.stmt.emplace_back(IndirectBranchCond{}, indirect.address, &root_stmt)};
                    Statement* goto_stmt{&pools.stmt.emplace_back(Goto{}, cond, indirect_label, &root_stmt)};
                    gotos.push_back(root.insert(ip, *goto_stmt));
                }
                root.insert(ip, pools.stmt.emplace_back(Unreachable{}, &root_stmt));
                break;
            case Flow::EndClass::Call: {
                Flow::Function& call{cfg.Functions()[block.function_call]};
                const Node call_return_label{local_labels.at(block.return_block)};
                BuildTree(cfg, call, label_id, gotos, ip, call_return_label);
                break;
            }
            case Flow::EndClass::Exit:
                root.insert(ip, pools.stmt.emplace_back(Return{}, &root_stmt));
                break;
            case Flow::EndClass::Return: {
                Statement* const always_cond{&pools.stmt.emplace_back(Identity{}, block.cond, &root_stmt)};
                auto goto_stmt{&pools.stmt.emplace_back(Goto{}, always_cond, return_label.value(), &root_stmt)};
                gotos.push_back(root.insert(ip, *goto_stmt));
                break;
            }
            case Flow::EndClass::Kill:
                root.insert(ip, pools.stmt.emplace_back(Kill{}, &root_stmt));
                break;
            }
        }
    }

    void UpdateTreeUp(Statement* tree) {
        for (Statement& stmt : tree->children) {
            stmt.up = tree;
        }
    }

    void EliminateAsConditional(Node goto_stmt, Node label_stmt) {
        Tree& body{goto_stmt->up->children};
        Tree if_body;
        if_body.splice(if_body.begin(), body, std::next(goto_stmt), label_stmt);
        Statement* const cond{&pools.stmt.emplace_back(Not{}, goto_stmt->cond, &root_stmt)};
        Statement* const if_stmt{&pools.stmt.emplace_back(If{}, cond, std::move(if_body), goto_stmt->up)};
        UpdateTreeUp(if_stmt);
        body.insert(goto_stmt, *if_stmt);
        body.erase(goto_stmt);
    }

    void EliminateAsLoop(Node goto_stmt, Node label_stmt) {
        Tree& body{goto_stmt->up->children};
        Tree loop_body;
        loop_body.splice(loop_body.begin(), body, label_stmt, goto_stmt);
        Statement* const cond{goto_stmt->cond};
        Statement* const loop{&pools.stmt.emplace_back(Loop{}, cond, std::move(loop_body), goto_stmt->up)};
        UpdateTreeUp(loop);
        body.insert(goto_stmt, *loop);
        body.erase(goto_stmt);
    }

    [[nodiscard]] Node MoveOutward(Node goto_stmt) {
        switch (goto_stmt->up->type) {
        case StatementType::If:
            return MoveOutwardIf(goto_stmt);
        case StatementType::Loop:
            return MoveOutwardLoop(goto_stmt);
        default:
            throw LogicError("Invalid outward movement");
        }
    }

    [[nodiscard]] Node MoveInward(Node goto_stmt) {
        Statement* const parent{goto_stmt->up};
        Tree& body{parent->children};
        const Node label{goto_stmt->label};
        const Node label_nested_stmt{SiblingFromNephew(goto_stmt, label)};
        const u32 label_id{label->id};

        Statement* const goto_cond{goto_stmt->cond};
        Statement* const set_var{&pools.stmt.emplace_back(SetVariable{}, label_id, goto_cond, parent)};
        body.insert(goto_stmt, *set_var);

        Tree if_body;
        if_body.splice(if_body.begin(), body, std::next(goto_stmt), label_nested_stmt);
        Statement* const variable{&pools.stmt.emplace_back(Variable{}, label_id, &root_stmt)};
        Statement* const neg_var{&pools.stmt.emplace_back(Not{}, variable, &root_stmt)};
        if (!if_body.empty()) {
            Statement* const if_stmt{&pools.stmt.emplace_back(If{}, neg_var, std::move(if_body), parent)};
            UpdateTreeUp(if_stmt);
            body.insert(goto_stmt, *if_stmt);
        }
        body.erase(goto_stmt);

        switch (label_nested_stmt->type) {
        case StatementType::If:
            // Update nested if condition
            label_nested_stmt->cond = &pools.stmt.emplace_back(Or{}, variable, label_nested_stmt->cond, &root_stmt);
            break;
        case StatementType::Loop:
            break;
        default:
            throw LogicError("Invalid inward movement");
        }
        Tree& nested_tree{label_nested_stmt->children};
        Statement* const new_goto{&pools.stmt.emplace_back(Goto{}, variable, label, &*label_nested_stmt)};
        return nested_tree.insert(nested_tree.begin(), *new_goto);
    }

    [[nodiscard]] Node Lift(Node goto_stmt) {
        Statement* const parent{goto_stmt->up};
        Tree& body{parent->children};
        const Node label{goto_stmt->label};
        const u32 label_id{label->id};
        const Node label_nested_stmt{SiblingFromNephew(goto_stmt, label)};

        Tree loop_body;
        loop_body.splice(loop_body.begin(), body, label_nested_stmt, goto_stmt);
        SanitizeNoBreaks(loop_body);
        Statement* const variable{&pools.stmt.emplace_back(Variable{}, label_id, &root_stmt)};
        Statement* const loop_stmt{&pools.stmt.emplace_back(Loop{}, variable, std::move(loop_body), parent)};
        UpdateTreeUp(loop_stmt);
        body.insert(goto_stmt, *loop_stmt);

        Statement* const new_goto{&pools.stmt.emplace_back(Goto{}, variable, label, loop_stmt)};
        loop_stmt->children.push_front(*new_goto);
        const Node new_goto_node{loop_stmt->children.begin()};

        Statement* const set_var{&pools.stmt.emplace_back(SetVariable{}, label_id, goto_stmt->cond, loop_stmt)};
        loop_stmt->children.push_back(*set_var);

        body.erase(goto_stmt);
        return new_goto_node;
    }

    Node MoveOutwardIf(Node goto_stmt) {
        const Node parent{Tree::s_iterator_to(*goto_stmt->up)};
        Tree& body{parent->children};
        const u32 label_id{goto_stmt->label->id};
        Statement* const goto_cond{goto_stmt->cond};
        Statement* const set_goto_var{&pools.stmt.emplace_back(SetVariable{}, label_id, goto_cond, &*parent)};
        body.insert(goto_stmt, *set_goto_var);

        Tree if_body;
        if_body.splice(if_body.begin(), body, std::next(goto_stmt), body.end());
        if_body.pop_front();
        Statement* const cond{&pools.stmt.emplace_back(Variable{}, label_id, &root_stmt)};
        Statement* const neg_cond{&pools.stmt.emplace_back(Not{}, cond, &root_stmt)};
        Statement* const if_stmt{&pools.stmt.emplace_back(If{}, neg_cond, std::move(if_body), &*parent)};
        UpdateTreeUp(if_stmt);
        body.insert(goto_stmt, *if_stmt);

        body.erase(goto_stmt);

        Statement* const new_cond{&pools.stmt.emplace_back(Variable{}, label_id, &root_stmt)};
        Statement* const new_goto{&pools.stmt.emplace_back(Goto{}, new_cond, goto_stmt->label, parent->up)};
        Tree& parent_tree{parent->up->children};
        return parent_tree.insert(std::next(parent), *new_goto);
    }

    Node MoveOutwardLoop(Node goto_stmt) {
        Statement* const parent{goto_stmt->up};
        Tree& body{parent->children};
        const u32 label_id{goto_stmt->label->id};
        Statement* const goto_cond{goto_stmt->cond};
        Statement* const set_goto_var{&pools.stmt.emplace_back(SetVariable{}, label_id, goto_cond, parent)};
        Statement* const cond{&pools.stmt.emplace_back(Variable{}, label_id, &root_stmt)};
        Statement* const break_stmt{&pools.stmt.emplace_back(Break{}, cond, parent)};
        body.insert(goto_stmt, *set_goto_var);
        body.insert(goto_stmt, *break_stmt);
        body.erase(goto_stmt);

        const Node loop{Tree::s_iterator_to(*goto_stmt->up)};
        Statement* const new_goto_cond{&pools.stmt.emplace_back(Variable{}, label_id, &root_stmt)};
        Statement* const new_goto{&pools.stmt.emplace_back(Goto{}, new_goto_cond, goto_stmt->label, loop->up)};
        Tree& parent_tree{loop->up->children};
        return parent_tree.insert(std::next(loop), *new_goto);
    }

    ShaderPools& pools;
    Statement root_stmt{FunctionTag{}};
};

[[nodiscard]] Statement* TryFindForwardBlock(Statement& stmt) {
    Tree& tree{stmt.up->children};
    const Node end{tree.end()};
    Node forward_node{std::next(Tree::s_iterator_to(stmt))};
    while (forward_node != end && !HasChildren(forward_node->type)) {
        if (forward_node->type == StatementType::Code) {
            return &*forward_node;
        }
        ++forward_node;
    }
    return nullptr;
}

[[nodiscard]] IR::U1 VisitExpr(IR::IREmitter& ir, const Statement& stmt) {
    switch (stmt.type) {
    case StatementType::Identity:
        return ir.Condition(stmt.guest_cond);
    case StatementType::Not:
        return ir.LogicalNot(IR::U1{VisitExpr(ir, *stmt.op)});
    case StatementType::Or:
        return ir.LogicalOr(VisitExpr(ir, *stmt.op_a), VisitExpr(ir, *stmt.op_b));
    case StatementType::Variable:
        return ir.GetGotoVariable(stmt.id);
    case StatementType::IndirectBranchCond:
        return ir.IEqual(ir.GetIndirectBranchVariable(), ir.Imm32(stmt.location));
    default:
        throw NotImplementedException("Statement type {}", stmt.type);
    }
}

class TranslatePass {
public:
    TranslatePass(ShaderPools& pools_, Environment& env_, Statement& root_stmt, IR::AbstractSyntaxList& syntax_list_, const HostTranslateInfo& host_info)
        : pools{pools_}
        , env{env_}
        , syntax_list{syntax_list_}
    {
        Visit(root_stmt, nullptr, nullptr);

        IR::Block& first_block{*syntax_list.front().data.block};
        IR::IREmitter ir(first_block, first_block.begin());
        ir.Prologue();
        if (uses_demote_to_helper && host_info.needs_demote_reorder) {
            DemoteCombinationPass();
        }
    }

private:
    void Visit(Statement& parent, IR::Block* break_block, IR::Block* fallthrough_block) {
        IR::Block* current_block{};
        const auto ensure_block{[&] {
            if (current_block) {
                return;
            }
            current_block = &pools.block.emplace_back(pools.inst);
            auto& node{syntax_list.emplace_back()};
            node.type = IR::AbstractSyntaxNode::Type::Block;
            node.data.block = current_block;
        }};
        Tree& tree{parent.children};
        for (auto it = tree.begin(); it != tree.end(); ++it) {
            Statement& stmt{*it};
            switch (stmt.type) {
            case StatementType::Label:
                // Labels can be ignored
                break;
            case StatementType::Code: {
                ensure_block();
                Translate(env, current_block, stmt.block->begin.Offset(), stmt.block->end.Offset());
                break;
            }
            case StatementType::SetVariable: {
                ensure_block();
                IR::IREmitter ir{*current_block};
                ir.SetGotoVariable(stmt.id, VisitExpr(ir, *stmt.op));
                break;
            }
            case StatementType::SetIndirectBranchVariable: {
                ensure_block();
                IR::IREmitter ir{*current_block};
                IR::U32 address{ir.IAdd(ir.GetReg(stmt.branch_reg), ir.Imm32(stmt.branch_offset))};
                ir.SetIndirectBranchVariable(address);
                break;
            }
            case StatementType::If: {
                ensure_block();
                IR::Block* const merge_block{MergeBlock(parent, stmt)};

                // Implement if header block
                IR::IREmitter ir{*current_block};
                const IR::U1 cond{ir.ConditionRef(VisitExpr(ir, *stmt.cond))};

                const size_t if_node_index{syntax_list.size()};
                syntax_list.emplace_back();

                // Visit children
                const size_t then_block_index{syntax_list.size()};
                Visit(stmt, break_block, merge_block);

                IR::Block* const then_block{syntax_list.at(then_block_index).data.block};
                current_block->AddBranch(then_block);
                current_block->AddBranch(merge_block);
                current_block = merge_block;

                auto& if_node{syntax_list[if_node_index]};
                if_node.type = IR::AbstractSyntaxNode::Type::If;
                if_node.data.if_node.cond = cond;
                if_node.data.if_node.body = then_block;
                if_node.data.if_node.merge = merge_block;

                auto& endif_node{syntax_list.emplace_back()};
                endif_node.type = IR::AbstractSyntaxNode::Type::EndIf;
                endif_node.data.end_if.merge = merge_block;

                auto& merge{syntax_list.emplace_back()};
                merge.type = IR::AbstractSyntaxNode::Type::Block;
                merge.data.block = merge_block;
                break;
            }
            case StatementType::Loop: {
                IR::Block* const loop_header_block{&pools.block.emplace_back(pools.inst)};
                if (current_block) {
                    current_block->AddBranch(loop_header_block);
                }
                auto& header_node{syntax_list.emplace_back()};
                header_node.type = IR::AbstractSyntaxNode::Type::Block;
                header_node.data.block = loop_header_block;

                IR::Block* const continue_block{&pools.block.emplace_back(pools.inst)};
                IR::Block* const merge_block{MergeBlock(parent, stmt)};

                const size_t loop_node_index{syntax_list.size()};
                syntax_list.emplace_back();

                // Visit children
                const size_t body_block_index{syntax_list.size()};
                Visit(stmt, merge_block, continue_block);

                // The continue block is located at the end of the loop
                IR::IREmitter ir{*continue_block};
                const IR::U1 cond{ir.ConditionRef(VisitExpr(ir, *stmt.cond))};

                IR::Block* const body_block{syntax_list.at(body_block_index).data.block};
                loop_header_block->AddBranch(body_block);

                continue_block->AddBranch(loop_header_block);
                continue_block->AddBranch(merge_block);

                current_block = merge_block;

                auto& loop{syntax_list[loop_node_index]};
                loop.type = IR::AbstractSyntaxNode::Type::Loop;
                loop.data.loop.body = body_block;
                loop.data.loop.continue_block = continue_block;
                loop.data.loop.merge = merge_block;

                auto& continue_block_node{syntax_list.emplace_back()};
                continue_block_node.type = IR::AbstractSyntaxNode::Type::Block;
                continue_block_node.data.block = continue_block;

                auto& repeat{syntax_list.emplace_back()};
                repeat.type = IR::AbstractSyntaxNode::Type::Repeat;
                repeat.data.repeat.cond = cond;
                repeat.data.repeat.loop_header = loop_header_block;
                repeat.data.repeat.merge = merge_block;

                auto& merge{syntax_list.emplace_back()};
                merge.type = IR::AbstractSyntaxNode::Type::Block;
                merge.data.block = merge_block;
                break;
            }
            case StatementType::Break: {
                ensure_block();
                IR::Block* const skip_block{MergeBlock(parent, stmt)};

                IR::IREmitter ir{*current_block};
                const IR::U1 cond{ir.ConditionRef(VisitExpr(ir, *stmt.cond))};
                current_block->AddBranch(break_block);
                current_block->AddBranch(skip_block);
                current_block = skip_block;

                auto& break_node{syntax_list.emplace_back()};
                break_node.type = IR::AbstractSyntaxNode::Type::Break;
                break_node.data.break_node.cond = cond;
                break_node.data.break_node.merge = break_block;
                break_node.data.break_node.skip = skip_block;

                auto& merge{syntax_list.emplace_back()};
                merge.type = IR::AbstractSyntaxNode::Type::Block;
                merge.data.block = skip_block;
                break;
            }
            case StatementType::Return: {
                ensure_block();
                IR::Block* return_block{&pools.block.emplace_back(pools.inst)};
                IR::IREmitter{*return_block}.Epilogue();
                current_block->AddBranch(return_block);

                auto& merge{syntax_list.emplace_back()};
                merge.type = IR::AbstractSyntaxNode::Type::Block;
                merge.data.block = return_block;

                current_block = nullptr;
                syntax_list.emplace_back().type = IR::AbstractSyntaxNode::Type::Return;
                break;
            }
            case StatementType::Kill: {
                ensure_block();
                IR::Block* demote_block{MergeBlock(parent, stmt)};
                IR::IREmitter{*current_block}.DemoteToHelperInvocation();
                current_block->AddBranch(demote_block);
                current_block = demote_block;

                auto& merge{syntax_list.emplace_back()};
                merge.type = IR::AbstractSyntaxNode::Type::Block;
                merge.data.block = demote_block;
                uses_demote_to_helper = true;
                break;
            }
            case StatementType::Unreachable: {
                ensure_block();
                current_block = nullptr;
                syntax_list.emplace_back().type = IR::AbstractSyntaxNode::Type::Unreachable;
                break;
            }
            default:
                throw NotImplementedException("Statement type {}", stmt.type);
            }
        }
        if (current_block) {
            if (fallthrough_block) {
                current_block->AddBranch(fallthrough_block);
            } else {
                syntax_list.emplace_back().type = IR::AbstractSyntaxNode::Type::Unreachable;
            }
        }
    }

    IR::Block* MergeBlock(Statement& parent, Statement& stmt) {
        Statement* merge_stmt{TryFindForwardBlock(stmt)};
        if (!merge_stmt) {
            // Create a merge block we can visit later
            merge_stmt = &pools.stmt.emplace_back(&dummy_flow_block, &parent);
            parent.children.insert(std::next(Tree::s_iterator_to(stmt)), *merge_stmt);
        }
        return &pools.block.emplace_back(pools.inst);
    }

    void DemoteCombinationPass() {
        using Type = IR::AbstractSyntaxNode::Type;
        std::vector<IR::Block*> demote_blocks;
        std::vector<IR::U1> demote_conds;
        u32 num_epilogues{};
        u32 branch_depth{};
        for (const IR::AbstractSyntaxNode& node : syntax_list) {
            if (node.type == Type::If) {
                ++branch_depth;
            }
            if (node.type == Type::EndIf) {
                --branch_depth;
            }
            if (node.type != Type::Block) {
                continue;
            }
            if (branch_depth > 1) {
                // Skip reordering nested demote branches.
                continue;
            }
            for (const IR::Inst& inst : node.data.block->Instructions()) {
                const IR::Opcode op{inst.GetOpcode()};
                if (op == IR::Opcode::DemoteToHelperInvocation) {
                    demote_blocks.push_back(node.data.block);
                    break;
                }
                if (op == IR::Opcode::Epilogue) {
                    ++num_epilogues;
                }
            }
        }
        if (demote_blocks.size() == 0) {
            return;
        }
        if (num_epilogues > 1) {
            LOG_DEBUG(Shader, "Combining demotes with more than one return is not implemented.");
            return;
        }
        s64 last_iterator_offset{};
        auto& asl{syntax_list};
        for (const IR::Block* demote_block : demote_blocks) {
            const auto start_it{asl.begin() + last_iterator_offset};
            auto asl_it{std::find_if(start_it, asl.end(), [&](const IR::AbstractSyntaxNode& asn) {
                return asn.type == Type::If && asn.data.if_node.body == demote_block;
            })};
            if (asl_it == asl.end()) {
                // Demote without a conditional branch.
                // No need to proceed since all fragment instances will be demoted regardless.
                return;
            }
            const IR::Block* const end_if = asl_it->data.if_node.merge;
            demote_conds.push_back(asl_it->data.if_node.cond);
            last_iterator_offset = std::distance(asl.begin(), asl_it);

            asl_it = asl.erase(asl_it);
            asl_it = std::find_if(asl_it, asl.end(), [&](const IR::AbstractSyntaxNode& asn) {
                return asn.type == Type::Block && asn.data.block == demote_block;
            });

            asl_it = asl.erase(asl_it);
            asl_it = std::find_if(asl_it, asl.end(), [&](const IR::AbstractSyntaxNode& asn) {
                return asn.type == Type::EndIf && asn.data.end_if.merge == end_if;
            });
            asl_it = asl.erase(asl_it);
        }
        const auto epilogue_func{[](const IR::AbstractSyntaxNode& asn) {
            if (asn.type != Type::Block) {
                return false;
            }
            for (const auto& inst : asn.data.block->Instructions()) {
                if (inst.GetOpcode() == IR::Opcode::Epilogue) {
                    return true;
                }
            }
            return false;
        }};
        const auto reverse_it{std::find_if(asl.rbegin(), asl.rend(), epilogue_func)};
        const auto return_block_it{(reverse_it + 1).base()};

        IR::IREmitter ir{*(return_block_it - 1)->data.block};
        IR::U1 cond(IR::Value(false));
        for (const auto& demote_cond : demote_conds) {
            cond = ir.LogicalOr(cond, demote_cond);
        }
        cond.Inst()->DestructiveAddUsage(1);

        IR::AbstractSyntaxNode demote_if_node{};
        demote_if_node.type = Type::If;
        demote_if_node.data.if_node.cond = cond;
        demote_if_node.data.if_node.body = demote_blocks[0];
        demote_if_node.data.if_node.merge = return_block_it->data.block;

        IR::AbstractSyntaxNode demote_node{};
        demote_node.type = Type::Block;
        demote_node.data.block = demote_blocks[0];

        IR::AbstractSyntaxNode demote_endif_node{};
        demote_endif_node.type = Type::EndIf;
        demote_endif_node.data.end_if.merge = return_block_it->data.block;

        const auto next_it_1 = asl.insert(return_block_it, demote_endif_node);
        const auto next_it_2 = asl.insert(next_it_1, demote_node);
        asl.insert(next_it_2, demote_if_node);
    }

    ShaderPools& pools;
    Environment& env;
    IR::AbstractSyntaxList& syntax_list;
    bool uses_demote_to_helper{};
    const Flow::Block dummy_flow_block{};
};
} // Anonymous namespace

IR::AbstractSyntaxList BuildASL(ShaderPools& pools, Environment& env, Flow::CFG& cfg, const HostTranslateInfo& host_info) {
    GotoPass goto_pass{cfg, pools};
    Statement& root{goto_pass.RootStatement()};
    IR::AbstractSyntaxList syntax_list;
    TranslatePass pass{pools, env, root, syntax_list, host_info};
    pools.stmt.clear();
    return syntax_list;
}

} // namespace Shader::Maxwell
