// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <boost/container/stable_vector.hpp>
#include <boost/intrusive/list.hpp>
#include "shader_recompiler/frontend/maxwell/control_flow.h"

namespace Shader::Maxwell {

struct Statement;

// Use normal_link because we are not guaranteed to destroy the tree in order
using ListBaseHook = boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>>;
using Tree = boost::intrusive::list<Statement,
    // Allow using Statement without a definition
    boost::intrusive::base_hook<ListBaseHook>,
    // Avoid linear complexity on splice, size is never called
    boost::intrusive::constant_time_size<false>>;
using Node = Tree::iterator;

enum class StatementType {
    Code,
    Goto,
    Label,
    If,
    Loop,
    Break,
    Return,
    Kill,
    Unreachable,
    Function,
    Identity,
    Not,
    Or,
    SetVariable,
    SetIndirectBranchVariable,
    Variable,
    IndirectBranchCond,
};

[[nodiscard]] inline bool HasChildren(StatementType type) {
    switch (type) {
    case StatementType::If:
    case StatementType::Loop:
    case StatementType::Function:
        return true;
    default:
        return false;
    }
}

struct Goto {};
struct Label {};
struct If {};
struct Loop {};
struct Break {};
struct Return {};
struct Kill {};
struct Unreachable {};
struct FunctionTag {};
struct Identity {};
struct Not {};
struct Or {};
struct SetVariable {};
struct SetIndirectBranchVariable {};
struct Variable {};
struct IndirectBranchCond {};

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 26495) // Always initialize a member variable, expected in Statement
#endif
struct Statement : ListBaseHook {
    Statement(const Flow::Block* block_, Statement* up_) : block{block_}, up{up_}, type{StatementType::Code} {}
    Statement(Goto, Statement* cond_, Node label_, Statement* up_) : label{label_}, cond{cond_}, up{up_}, type{StatementType::Goto} {}
    Statement(Label, u32 id_, Statement* up_) : id{id_}, up{up_}, type{StatementType::Label} {}
    Statement(If, Statement* cond_, Tree&& children_, Statement* up_) : children{std::move(children_)}, cond{cond_}, up{up_}, type{StatementType::If} {}
    Statement(Loop, Statement* cond_, Tree&& children_, Statement* up_) : children{std::move(children_)}, cond{cond_}, up{up_}, type{StatementType::Loop} {}
    Statement(Break, Statement* cond_, Statement* up_) : cond{cond_}, up{up_}, type{StatementType::Break} {}
    Statement(Return, Statement* up_) : up{up_}, type{StatementType::Return} {}
    Statement(Kill, Statement* up_) : up{up_}, type{StatementType::Kill} {}
    Statement(Unreachable, Statement* up_) : up{up_}, type{StatementType::Unreachable} {}
    Statement(FunctionTag) : children{}, type{StatementType::Function} {}
    Statement(Identity, IR::Condition cond_, Statement* up_) : guest_cond{cond_}, up{up_}, type{StatementType::Identity} {}
    Statement(Not, Statement* op_, Statement* up_) : op{op_}, up{up_}, type{StatementType::Not} {}
    Statement(Or, Statement* op_a_, Statement* op_b_, Statement* up_) : op_a{op_a_}, op_b{op_b_}, up{up_}, type{StatementType::Or} {}
    Statement(SetVariable, u32 id_, Statement* op_, Statement* up_) : op{op_}, id{id_}, up{up_}, type{StatementType::SetVariable} {}
    Statement(SetIndirectBranchVariable, IR::Reg branch_reg_, s32 branch_offset_, Statement* up_) : branch_offset{branch_offset_}, branch_reg{branch_reg_}, up{up_}, type{StatementType::SetIndirectBranchVariable} {}
    Statement(Variable, u32 id_, Statement* up_) : id{id_}, up{up_}, type{StatementType::Variable} {}
    Statement(IndirectBranchCond, u32 location_, Statement* up_) : location{location_}, up{up_}, type{StatementType::IndirectBranchCond} {}
    ~Statement() {
        if (HasChildren(type)) {
            std::destroy_at(&children);
        }
    }
    union {
        const Flow::Block* block;
        Node label;
        Tree children;
        IR::Condition guest_cond;
        Statement* op;
        Statement* op_a;
        u32 location;
        s32 branch_offset;
    };
    union {
        Statement* cond;
        Statement* op_b;
        u32 id;
        IR::Reg branch_reg;
    };
    Statement* up{};
    StatementType type;
};
#ifdef _MSC_VER
#pragma warning(pop)
#endif

struct ShaderPools {
    void clear() {
        flow_block.clear();
        block.clear();
        inst.clear();
        stmt.clear();
    }
    boost::container::stable_vector<Shader::IR::Inst> inst{};
    boost::container::stable_vector<Shader::IR::Block> block{};
    boost::container::stable_vector<Shader::Maxwell::Flow::Block> flow_block{};
    boost::container::stable_vector<Shader::Maxwell::Statement> stmt;
};

}
