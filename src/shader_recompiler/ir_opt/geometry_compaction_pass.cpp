// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <boost/container/small_vector.hpp>

#include "shader_recompiler/frontend/ir/ir_emitter.h"
#include "shader_recompiler/host_translate_info.h"
#include "shader_recompiler/ir_opt/passes.h"

namespace Shader::Optimization {
namespace {
constexpr int MAX_BALLOT_DEPTH = 8;

struct SlotAtomic {
    IR::Block* block;
    IR::Inst* atomic;
    IR::Inst* ballot;
};

IR::Inst* FindBallot(const IR::Value& value, int depth) {
    if (depth > MAX_BALLOT_DEPTH || value.IsImmediate()) {
        return nullptr;
    }
    IR::Inst* const inst{value.InstRecursive()};
    if (inst->GetOpcode() == IR::Opcode::SubgroupBallot) {
        return inst;
    }
    if (inst->GetOpcode() == IR::Opcode::Phi) {
        return nullptr;
    }
    for (size_t index = 0; index < inst->NumArgs(); ++index) {
        if (IR::Inst* const ballot{FindBallot(inst->Arg(index), depth + 1)}) {
            return ballot;
        }
    }
    return nullptr;
}

bool IsShuffle(IR::Opcode opcode) {
    switch (opcode) {
    case IR::Opcode::ShuffleIndex:
    case IR::Opcode::ShuffleUp:
    case IR::Opcode::ShuffleDown:
    case IR::Opcode::ShuffleButterfly:
        return true;
    default:
        return false;
    }
}

IR::U32 PrimitiveSlot(IR::IREmitter& ir, u32 invocations, const IR::U32& amount) {
    IR::U32 key{ir.GetAttributeU32(IR::Attribute::PrimitiveId)};
    if (invocations > 1) {
        key = ir.IAdd(ir.IMul(key, ir.Imm32(invocations)), ir.InvocationId());
    }
    return ir.IMul(key, amount);
}

bool MergesAtomic(const IR::Inst& phi, const IR::Block& block, const IR::Inst& atomic) {
    for (size_t index = 0; index < phi.NumArgs(); ++index) {
        const IR::Value arg{phi.Arg(index)};
        if (phi.PhiBlock(index) == &block && !arg.IsImmediate() &&
            arg.InstRecursive() == &atomic) {
            return true;
        }
    }
    return false;
}

void RewriteMergedSlots(IR::Block& block, IR::Inst& atomic, u32 invocations) {
    for (IR::Block* const successor : block.ImmSuccessors()) {
        for (IR::Inst& phi : successor->Instructions()) {
            if (phi.GetOpcode() != IR::Opcode::Phi || !MergesAtomic(phi, block, atomic)) {
                continue;
            }
            for (size_t index = 0; index < phi.NumArgs(); ++index) {
                const IR::Value arg{phi.Arg(index)};
                if (arg.IsImmediate() || !IsShuffle(arg.InstRecursive()->GetOpcode())) {
                    continue;
                }
                IR::IREmitter ir{*phi.PhiBlock(index)};
                const IR::U32 amount{arg.InstRecursive()->Arg(0)};
                phi.SetArg(index, PrimitiveSlot(ir, invocations, amount));
            }
        }
    }
}

void RewriteAtomic(IR::Block& block, IR::Inst& atomic, u32 invocations) {
    const auto insert_point{IR::Block::InstructionList::s_iterator_to(atomic)};
    IR::IREmitter ir{block, insert_point};
    const IR::U32 amount{atomic.Arg(2)};
    const IR::U32 slot{PrimitiveSlot(ir, invocations, amount)};
    block.PrependNewInst(insert_point, IR::Opcode::StorageAtomicUMax32,
                         {atomic.Arg(0), atomic.Arg(1), ir.IAdd(slot, amount)});
    atomic.ReplaceUsesWith(slot);
}

void NeutralizePredicate(IR::Inst& ballot) {
    const IR::Value pred{ballot.Arg(0)};
    if (!pred.IsImmediate()) {
        pred.InstRecursive()->ReplaceUsesWith(IR::Value{true});
    }
}
}

void GeometryCompactionPass(IR::Program& program, const HostTranslateInfo& host_info) {
    if (program.stage != Stage::Geometry || !host_info.single_lane_geometry_subgroups) {
        return;
    }
    boost::container::small_vector<SlotAtomic, 4> slot_atomics;
    for (IR::Block* const block : program.post_order_blocks) {
        for (IR::Inst& inst : block->Instructions()) {
            if (inst.GetOpcode() != IR::Opcode::StorageAtomicIAdd32) {
                continue;
            }
            if (IR::Inst* const ballot{FindBallot(inst.Arg(2), 0)}) {
                slot_atomics.push_back({block, &inst, ballot});
            }
        }
    }
    for (const SlotAtomic& slot_atomic : slot_atomics) {
        RewriteMergedSlots(*slot_atomic.block, *slot_atomic.atomic, program.invocations);
        RewriteAtomic(*slot_atomic.block, *slot_atomic.atomic, program.invocations);
        NeutralizePredicate(*slot_atomic.ballot);
    }
}

}
