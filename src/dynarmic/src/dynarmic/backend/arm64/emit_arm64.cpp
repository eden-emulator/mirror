// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2022 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include "dynarmic/backend/arm64/emit_arm64.h"

#include <oaknut/oaknut.hpp>

#include "dynarmic/backend/arm64/abi.h"
#include "dynarmic/backend/arm64/emit_context.h"
#include "dynarmic/backend/arm64/fpsr_manager.h"
#include "dynarmic/backend/arm64/reg_alloc.h"
#include "dynarmic/backend/arm64/verbose_debugging_output.h"
#include "dynarmic/ir/basic_block.h"
#include "dynarmic/ir/microinstruction.h"
#include "dynarmic/ir/opcodes.h"

namespace Dynarmic::Backend::Arm64 {

using namespace oaknut::util;

template<>
void EmitIR<IR::Opcode::Void>(oaknut::CodeGenerator&, EmitContext&, IR::Inst*) {}

template<>
void EmitIR<IR::Opcode::Identity>(oaknut::CodeGenerator&, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    ctx.reg_alloc.DefineAsExisting(inst, args[0]);
}

template<>
void EmitIR<IR::Opcode::Breakpoint>(oaknut::CodeGenerator& code, EmitContext&, IR::Inst*) {
    code.BRK(0);
}

template<>
void EmitIR<IR::Opcode::CallHostFunction>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    ctx.reg_alloc.PrepareForCall(args[1], args[2], args[3]);
    code.MOV(Xscratch0, args[0].GetImmediateU64());
    code.BLR(Xscratch0);
}

template<>
void EmitIR<IR::Opcode::PushRSB>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    if (!ctx.conf.HasOptimization(OptimizationFlag::ReturnStackBuffer)) {
        return;
    }

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ASSERT(args[0].IsImmediate());
    const IR::LocationDescriptor target{args[0].GetImmediateU64()};

    code.LDR(Wscratch2, SP, offsetof(StackLayout, rsb_ptr));
    code.ADD(Wscratch2, Wscratch2, sizeof(RSBEntry));
    code.AND(Wscratch2, Wscratch2, RSBIndexMask);
    code.STR(Wscratch2, SP, offsetof(StackLayout, rsb_ptr));
    code.ADD(Xscratch2, SP, Xscratch2);

    code.MOV(Xscratch0, target.Value());
    EmitBlockLinkRelocation(code, ctx, target, BlockRelocationType::MoveToScratch1);
    code.STP(Xscratch0, Xscratch1, Xscratch2, offsetof(StackLayout, rsb));
}

template<>
void EmitIR<IR::Opcode::GetCarryFromOp>(oaknut::CodeGenerator&, EmitContext& ctx, IR::Inst* inst) {
    [[maybe_unused]] auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ASSERT(ctx.reg_alloc.WasValueDefined(inst));
}

template<>
void EmitIR<IR::Opcode::GetOverflowFromOp>(oaknut::CodeGenerator&, EmitContext& ctx, IR::Inst* inst) {
    [[maybe_unused]] auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ASSERT(ctx.reg_alloc.WasValueDefined(inst));
}

template<>
void EmitIR<IR::Opcode::GetGEFromOp>(oaknut::CodeGenerator&, EmitContext& ctx, IR::Inst* inst) {
    [[maybe_unused]] auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ASSERT(ctx.reg_alloc.WasValueDefined(inst));
}

template<>
void EmitIR<IR::Opcode::GetNZCVFromOp>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    if (ctx.reg_alloc.WasValueDefined(inst)) {
        return;
    }

    switch (args[0].GetType()) {
    case IR::Type::U32: {
        auto Wvalue = ctx.reg_alloc.ReadW(args[0]);
        auto flags = ctx.reg_alloc.WriteFlags(inst);
        RegAlloc::Realize(Wvalue, flags);

        code.TST(*Wvalue, Wvalue);
        break;
    }
    case IR::Type::U64: {
        auto Xvalue = ctx.reg_alloc.ReadX(args[0]);
        auto flags = ctx.reg_alloc.WriteFlags(inst);
        RegAlloc::Realize(Xvalue, flags);

        code.TST(*Xvalue, Xvalue);
        break;
    }
    default:
        UNREACHABLE();
    }
}

template<>
void EmitIR<IR::Opcode::GetNZFromOp>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    if (ctx.reg_alloc.WasValueDefined(inst)) {
        return;
    }

    switch (args[0].GetType()) {
    case IR::Type::U32: {
        auto Wvalue = ctx.reg_alloc.ReadW(args[0]);
        auto flags = ctx.reg_alloc.WriteFlags(inst);
        RegAlloc::Realize(Wvalue, flags);

        code.TST(*Wvalue, *Wvalue);
        break;
    }
    case IR::Type::U64: {
        auto Xvalue = ctx.reg_alloc.ReadX(args[0]);
        auto flags = ctx.reg_alloc.WriteFlags(inst);
        RegAlloc::Realize(Xvalue, flags);

        code.TST(*Xvalue, *Xvalue);
        break;
    }
    default:
        UNREACHABLE();
    }
}

template<>
void EmitIR<IR::Opcode::GetUpperFromOp>(oaknut::CodeGenerator&, EmitContext& ctx, IR::Inst* inst) {
    [[maybe_unused]] auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ASSERT(ctx.reg_alloc.WasValueDefined(inst));
}

template<>
void EmitIR<IR::Opcode::GetLowerFromOp>(oaknut::CodeGenerator&, EmitContext& ctx, IR::Inst* inst) {
    [[maybe_unused]] auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ASSERT(ctx.reg_alloc.WasValueDefined(inst));
}

template<>
void EmitIR<IR::Opcode::GetCFlagFromNZCV>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    auto Wc = ctx.reg_alloc.WriteW(inst);
    auto Wnzcv = ctx.reg_alloc.ReadW(args[0]);
    RegAlloc::Realize(Wc, Wnzcv);

    code.AND(Wc, Wnzcv, 1 << 29);
}

template<>
void EmitIR<IR::Opcode::NZCVFromPackedFlags>(oaknut::CodeGenerator&, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    ctx.reg_alloc.DefineAsExisting(inst, args[0]);
}

static void EmitAddCycles(oaknut::CodeGenerator& code, EmitContext& ctx, std::size_t cycles_to_add) {
    if (!ctx.conf.enable_cycle_counting) {
        return;
    }
    if (cycles_to_add == 0) {
        return;
    }

    if (oaknut::AddSubImm::is_valid(cycles_to_add)) {
        code.SUB(Xticks, Xticks, cycles_to_add);
    } else {
        code.MOV(Xscratch1, cycles_to_add);
        code.SUB(Xticks, Xticks, Xscratch1);
    }
}

namespace {

// Out-of-line paths queued in deferred_emits are reached via B.cond/CBZ/CBNZ, which only have a
// +/-1 MiB range. Flush them inline (behind a branch) once the pending window grows past this.
constexpr std::ptrdiff_t deferred_emit_flush_threshold = 64 * 1024;
// Upper bound for what is emitted after the last IR instruction (terminal and pending deferred code).
constexpr std::size_t block_tail_reserve = 512 * 1024;

void FlushDeferredEmits(oaknut::CodeGenerator& code, EmitContext& ctx) {
    oaknut::Label skip;
    code.B(skip);
    for (const auto& deferred_emit : ctx.deferred_emits) {
        deferred_emit();
    }
    ctx.deferred_emits.clear();
    code.l(skip);
}

EmittedBlockInfo EmitArm64Impl(oaknut::CodeGenerator& code, IR::Block& block, const EmitConfig& conf, FastmemManager& fastmem_manager, std::size_t max_size, IR::Inst*& current_inst) {
    if (conf.very_verbose_debugging_output) {
        std::puts(IR::DumpBlock(block).c_str());
    }

    EmittedBlockInfo ebi;

    FpsrManager fpsr_manager{code, conf.state_fpsr_offset};
    RegAlloc reg_alloc{code, fpsr_manager, GPR_ORDER, FPR_ORDER};
    EmitContext ctx{block, reg_alloc, conf, ebi, fpsr_manager, fastmem_manager, {}};

    ebi.entry_point = code.xptr<CodePtr>();
    const std::ptrdiff_t start_offset = code.offset();

    if (ctx.block.GetCondition() == IR::Cond::AL) {
        ASSERT(!ctx.block.HasConditionFailedLocation());
    } else {
        ASSERT(ctx.block.HasConditionFailedLocation());
        oaknut::Label pass;

        pass = conf.emit_cond(code, ctx, ctx.block.GetCondition());
        EmitAddCycles(code, ctx, ctx.block.ConditionFailedCycleCount());
        conf.emit_condition_failed_terminal(code, ctx);

        code.l(pass);
    }

    std::ptrdiff_t deferred_window_start = code.offset();

    for (auto iter = block.instructions.begin(); iter != block.instructions.end(); ++iter) {
        IR::Inst* inst = &*iter;
        current_inst = inst;

        switch (inst->GetOpcode()) {
#define OPCODE(name, type, ...)                    \
    case IR::Opcode::name:                         \
        EmitIR<IR::Opcode::name>(code, ctx, inst); \
        break;
#define A32OPC(name, type, ...)                         \
    case IR::Opcode::A32##name:                         \
        EmitIR<IR::Opcode::A32##name>(code, ctx, inst); \
        break;
#define A64OPC(name, type, ...)                         \
    case IR::Opcode::A64##name:                         \
        EmitIR<IR::Opcode::A64##name>(code, ctx, inst); \
        break;
#include "dynarmic/ir/opcodes.inc"
#undef OPCODE
#undef A32OPC
#undef A64OPC
        default:
            UNREACHABLE();
        }

        reg_alloc.UpdateAllUses();
        reg_alloc.AssertAllUnlocked();

        if (conf.very_verbose_debugging_output) {
            EmitVerboseDebuggingOutput(code, ctx);
        }

        if (ctx.deferred_emits.empty()) {
            deferred_window_start = code.offset();
        } else if (code.offset() - deferred_window_start >= deferred_emit_flush_threshold) {
            FlushDeferredEmits(code, ctx);
            deferred_window_start = code.offset();
        }

        if (static_cast<std::size_t>(code.offset() - start_offset) + block_tail_reserve > max_size) {
            throw CodeBufferExhausted{};
        }
    }
    current_inst = nullptr;

    fpsr_manager.Spill();

    reg_alloc.AssertNoMoreUses();

    EmitAddCycles(code, ctx, block.CycleCount());
    conf.emit_terminal(code, ctx);
    code.BRK(0);

    for (const auto& deferred_emit : ctx.deferred_emits) {
        deferred_emit();
    }
    code.BRK(0);

    ebi.size = code.xptr<CodePtr>() - ebi.entry_point;
    ASSERT(ebi.size <= max_size);
    return ebi;
}

}  // namespace

EmittedBlockInfo EmitArm64(oaknut::CodeGenerator& code, IR::Block block, const EmitConfig& conf, FastmemManager& fastmem_manager, std::size_t max_size) {
    IR::Inst* current_inst = nullptr;
    const std::ptrdiff_t start_offset = code.offset();
    try {
        return EmitArm64Impl(code, block, conf, fastmem_manager, max_size, current_inst);
    } catch (const oaknut::OaknutException& e) {
        // Unwinding through JIT frames is not possible, so report as much as we can here.
        UNREACHABLE_MSG("dynarmic: failed to emit host code ({}) for opcode {} after {} bytes\n{}",
                        e.what(), current_inst ? IR::GetNameOf(current_inst->GetOpcode()) : std::string_view{"<terminal/deferred>"},
                        code.offset() - start_offset, IR::DumpBlock(block));
    }
}

void EmitRelocation(oaknut::CodeGenerator& code, EmitContext& ctx, LinkTarget link_target) {
    ctx.ebi.relocations.emplace_back(Relocation{code.xptr<CodePtr>() - ctx.ebi.entry_point, link_target});
    code.NOP();
}

void EmitBlockLinkRelocation(oaknut::CodeGenerator& code, EmitContext& ctx, const IR::LocationDescriptor& descriptor, BlockRelocationType type) {
    ctx.ebi.block_relocations[descriptor].emplace_back(BlockRelocation{code.xptr<CodePtr>() - ctx.ebi.entry_point, type});
    switch (type) {
    case BlockRelocationType::Branch:
        code.NOP();
        break;
    case BlockRelocationType::MoveToScratch1:
        code.BRK(0);
        code.NOP();
        break;
    default:
        UNREACHABLE();
    }
}

}  // namespace Dynarmic::Backend::Arm64
