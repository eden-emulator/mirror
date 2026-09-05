// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shader_recompiler/frontend/ir/ir_emitter.h"
#include "shader_recompiler/frontend/ir/program.h"
#include "shader_recompiler/host_translate_info.h"
#include "shader_recompiler/ir_opt/passes.h"

namespace Shader::Optimization {
namespace {

constexpr int MAX_BALLOT_SEARCH_DEPTH = 8;

bool DependsOnSubgroupBallot(const IR::Value& value, int depth) {
    if (depth > MAX_BALLOT_SEARCH_DEPTH || value.IsImmediate()) {
        return false;
    }
    IR::Inst* const producer{value.Inst()};
    if (producer->GetOpcode() == IR::Opcode::SubgroupBallot) {
        return true;
    }
    if (producer->GetOpcode() == IR::Opcode::Phi) {
        return false;
    }
    const size_t num_args{producer->NumArgs()};
    for (size_t index = 0; index < num_args; ++index) {
        if (DependsOnSubgroupBallot(producer->Arg(index), depth + 1)) {
            return true;
        }
    }
    return false;
}

IR::Inst* FindSubgroupBallot(const IR::Value& value, int depth) {
    if (depth > MAX_BALLOT_SEARCH_DEPTH || value.IsImmediate()) {
        return nullptr;
    }
    IR::Inst* const producer{value.Inst()};
    if (producer->GetOpcode() == IR::Opcode::SubgroupBallot) {
        return producer;
    }
    if (producer->GetOpcode() == IR::Opcode::Phi) {
        return nullptr;
    }
    const size_t num_args{producer->NumArgs()};
    for (size_t index = 0; index < num_args; ++index) {
        if (IR::Inst* const found{FindSubgroupBallot(producer->Arg(index), depth + 1)}) {
            return found;
        }
    }
    return nullptr;
}

IR::Value UnwrapIdentity(IR::Value value) {
    for (; !value.IsImmediate() && value.Inst()->GetOpcode() == IR::Opcode::Identity;
         value = value.Inst()->Arg(0))
        ;
    return value;
}

bool IsShuffleFamily(IR::Opcode op) {
    switch (op) {
    case IR::Opcode::ShuffleIndex:
    case IR::Opcode::ShuffleUp:
    case IR::Opcode::ShuffleDown:
    case IR::Opcode::ShuffleButterfly:
        return true;
    default:
        return false;
    }
}

void FixNonElectedPath(IR::Block& elected_block, IR::Inst& atomic) {
    for (IR::Block* const successor : elected_block.ImmSuccessors()) {
        for (IR::Inst& phi : successor->Instructions()) {
            if (phi.GetOpcode() != IR::Opcode::Phi) {
                continue;
            }
            const size_t num_args{phi.NumArgs()};
            size_t elected_index = static_cast<size_t>(-1);
            for (size_t i = 0; i < num_args; ++i) {
                if (phi.PhiBlock(i) == &elected_block) {
                    elected_index = i;
                    break;
                }
            }
            if (elected_index == size_t(-1)) {
                continue;
            }
            const IR::Value elected_value{UnwrapIdentity(phi.Arg(elected_index))};
            if (elected_value.IsImmediate() || elected_value.Inst() != &atomic) {
                continue;
            }
            for (size_t i = 0; i < num_args; ++i) {
                IR::Block* const pred{phi.PhiBlock(i)};
                if (pred == &elected_block) {
                    continue;
                }
                const IR::Value shuffle_value{UnwrapIdentity(phi.Arg(i))};
                if (shuffle_value.IsImmediate()) {
                    continue;
                }
                IR::Inst* const producer{shuffle_value.Inst()};
                if (!IsShuffleFamily(producer->GetOpcode())) {
                    continue;
                }
                const IR::U32 local_amount{UnwrapIdentity(producer->Arg(0))};
                const auto insert_point{IR::Block::InstructionList::s_iterator_to(*producer)};
                IR::IREmitter ir{*pred, insert_point};

                const IR::U32 primitive_id{ir.GetAttributeU32(IR::Attribute::PrimitiveId)};
                const IR::U32 new_index{ir.IMul(primitive_id, local_amount)};

                producer->ReplaceUsesWith(new_index);
            }
        }
    }
}

void NeutralizeCullPredicate(IR::Inst& ballot) {
    const IR::Value pred{UnwrapIdentity(ballot.Arg(0))};
    if (pred.IsImmediate()) {
        return;
    }
    pred.Inst()->ReplaceUsesWith(IR::Value{true});
}

void RewriteAtomic(IR::Block& block, IR::Inst& inst) {
    const auto insert_point{IR::Block::InstructionList::s_iterator_to(inst)};
    IR::IREmitter ir{block, insert_point};

    const IR::U32 amount{inst.Arg(2)};
    const IR::U32 primitive_id{ir.GetAttributeU32(IR::Attribute::PrimitiveId)};
    const IR::U32 new_index{ir.IMul(primitive_id, amount)};
    const IR::U32 new_atomic_value{ir.IAdd(new_index, amount)};

    const IR::Value umax_result{&*block.PrependNewInst(
        insert_point, IR::Opcode::StorageAtomicUMax32,
        {inst.Arg(0), inst.Arg(1), new_atomic_value})};
    static_cast<void>(umax_result);

    inst.ReplaceUsesWith(new_index);
}

} // Anonymous namespace

void CompactionFallbackPass(IR::Program& program, const HostTranslateInfo& host_info) {
    if (program.stage != Stage::Geometry) {
        return;
    }
    if (host_info.support_subgroup_in_geometry_stage) {
        return;
    }
    for (IR::Block* const block : program.post_order_blocks) {
        for (IR::Inst& inst : block->Instructions()) {
            if (inst.GetOpcode() != IR::Opcode::StorageAtomicIAdd32) {
                continue;
            }
            if (!DependsOnSubgroupBallot(inst.Arg(2), 0)) {
                continue;
            }
            FixNonElectedPath(*block, inst);
            IR::Inst* const ballot{FindSubgroupBallot(inst.Arg(2), 0)};
            RewriteAtomic(*block, inst);
            if (ballot) {
                NeutralizeCullPredicate(*ballot);
            }
        }
    }
}

} // namespace Shader::Optimization
