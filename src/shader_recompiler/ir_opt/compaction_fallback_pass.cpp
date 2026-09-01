// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shader_recompiler/frontend/ir/ir_emitter.h"
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

void RewriteAtomic(IR::Block& block, IR::Inst& inst) {
    const auto insert_point{IR::Block::InstructionList::s_iterator_to(inst)};
    IR::IREmitter ir{block, insert_point};

    const IR::U32 amount{inst.Arg(2)};
    const IR::U32 primitive_id{ir.GetAttributeU32(IR::Attribute::PrimitiveId)};
    const IR::U1 reserves_slot{ir.INotEqual(amount, ir.Imm32(0u))};
    const IR::U32 slot_end{ir.IAdd(primitive_id, ir.Imm32(1u))};
    const IR::U32 high_water{IR::U32{ir.Select(reserves_slot, slot_end, ir.Imm32(0u))}};

    block.PrependNewInst(insert_point, IR::Opcode::StorageAtomicUMax32,
                         {inst.Arg(0), inst.Arg(1), high_water});

    inst.ReplaceUsesWith(primitive_id);
}

} // Anonymous namespace

void CompactionFallbackPass(IR::Program& program, const HostTranslateInfo& host_info) {
    if (program.stage != Stage::Geometry || host_info.support_subgroup_in_geometry_stage) {
        return;
    }
    for (IR::Block* const block : program.post_order_blocks) {
        for (IR::Inst& inst : block->Instructions()) {
            if (inst.GetOpcode() != IR::Opcode::StorageAtomicIAdd32) {
                continue;
            }
            if (!inst.HasUses()) {
                continue;
            }
            if (!DependsOnSubgroupBallot(inst.Arg(2), 0)) {
                continue;
            }
            RewriteAtomic(*block, inst);
        }
    }
}

} // namespace Shader::Optimization
