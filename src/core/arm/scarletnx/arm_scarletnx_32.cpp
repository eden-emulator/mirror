// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstring>
#include <utility>
#include <atomic>

#include "common/logging.h"
#include "core/arm/scarletnx/arm_scarletnx_32.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/hle/kernel/k_process.h"

namespace Core {

namespace {

// The exit SignalInterrupt sets, which no block sets itself.
constexpr u32 InterruptExit = 0x1000;
constexpr u32 UserMode = 0x10;
constexpr u32 ModeMask = 0x1F;

// FPSCR splits into FPSR and FPCR as in ArmDynarmic32.
std::pair<u32, u32> FpscrToFpsrFpcr(u32 fpscr) {
    const u32 fpsr = (fpscr & 0xf8000000) | (fpscr & 0x80) | (fpscr & 0x1f);
    const u32 fpcr = (fpscr & 0x7ff8000) | (fpscr & 0x1f00);
    return {fpsr, fpcr};
}

u32 FpsrFpcrToFpscr(u64 fpsr, u64 fpcr) {
    const auto [s, c] = FpscrToFpsrFpcr(u32(fpsr | fpcr));
    return s | c;
}

} // namespace

ArmScarletNx32::ArmScarletNx32(std::shared_ptr<ScarletNxProcess> process, std::size_t core_index)
    : ArmInterface{true}, m_process{std::move(process)}, m_core_index{core_index} {
    m_rt.coproc = &Coprocessor;
    m_cpu.parent = this;
    m_cpu.cpu.mem = m_process->m_arena;
    m_cpu.cpu.mem_size = 1ULL << 32;
    m_cpu.cpu.rt = &m_rt;

    m_fault_scope.memory = &m_process->m_process.GetMemory();
    m_fault_scope.arena = m_process->m_arena;
    m_fault_scope.arena_size = ScarletNxProcess::ArenaSize;
}

ArmScarletNx32::~ArmScarletNx32() {
    snx_recompiled32_free(m_module);
}

int ArmScarletNx32::Svc(void* context, u32 imm, u64 number, const u64 arguments[6]) {
    auto& self = *static_cast<ArmScarletNx32*>(context);
    self.m_svc = imm;
    self.m_in_svc = true;
    return 1;
}

void ArmScarletNx32::Coprocessor(SnxArmCPU* cpu, u32 word) {
    auto& self = *reinterpret_cast<Cpu32*>(cpu)->parent;
    const u32 coproc = (word >> 8) & 0xF;
    const u32 crm = word & 0xF;

    if (coproc == 15 && (word & 0x0F100010) == 0x0E000010) {
        // MCR p15
        const u32 opc1 = (word >> 21) & 7;
        const u32 crn = (word >> 16) & 0xF;
        const u32 opc2 = (word >> 5) & 7;
        if (opc1 == 0 && crn == 7 && crm == 5 && opc2 == 4) {
            // CP15_FLUSH_PREFETCH_BUFFER
            return;
        }
        if (opc1 == 0 && crn == 7 && crm == 10 && (opc2 == 4 || opc2 == 5)) {
            // CP15_DATA_SYNC_BARRIER, CP15_DATA_MEMORY_BARRIER
            std::atomic_thread_fence(std::memory_order_seq_cst);
            return;
        }
    } else if (coproc == 15 && (word & 0x0FF00000) == 0x0C500000) {
        // MRRC p15
        const u32 opc1 = (word >> 4) & 0xF;
        const u32 rt = (word >> 12) & 0xF;
        const u32 rt2 = (word >> 16) & 0xF;
        if (opc1 == 0 && crm == 14) {
            // CNTPCT
            const u64 ticks = self.m_process->m_system.CoreTiming().GetClockTicks();
            cpu->r[rt] = u32(ticks);
            cpu->r[rt2] = u32(ticks >> 32);
            return;
        }
    }

    // TPIDRURW and TPIDRURO, and undefined for anything else.
    snx_arm_coproc_user(cpu, word);
    ASSERT(cpu->exit == SNX_ARM_OK && "unhandled coproc");
}

HaltReason ArmScarletNx32::RunThread(Kernel::KThread* thread) {
    if (m_interrupt.exchange(false)) {
        return HaltReason::BreakLoop;
    }

    // The block cache only covers the code present when it was made.
    const u64 generation = m_process->CodeGeneration();
    if (!m_module || generation != m_code_generation) {
        snx_recompiled32_free(m_module);
        m_module = m_process->MakeModule32();
        m_code_generation = generation;
    }

    LoadCpu();
    m_in_svc = false;
    m_fault_scope.last_fault = ~u64{};
    SetScarletNxFaultScope(&m_fault_scope);
    if (sigsetjmp(m_fault_scope.env, 1) != 0) {
        SetScarletNxFaultScope(nullptr);
        LOG_CRITICAL(Core_ARM, "invalid memory access at {:#x}",
                     m_fault_scope.fault_address);
        SaveCpu();
        return HaltReason::PrefetchAbort;
    }
    const int exit =
        m_module ? snx_recompiled32_run(m_module, &m_cpu.cpu, &Svc, this) : SNX_ARM_BAD_ENTRY;
    SetScarletNxFaultScope(nullptr);

    SaveCpu();
    return Halt(exit);
}

HaltReason ArmScarletNx32::Halt(int exit) {
    if (m_in_svc) {
        return HaltReason::SupervisorCall;
    }
    u32 const pc = m_cpu.cpu.r[15];
    u32 const syndrome = m_cpu.cpu.syndrome;
    switch (u32(exit)) {
    case InterruptExit:
        m_interrupt.store(false);
        return HaltReason::BreakLoop;
    case SNX_ARM_BREAKPOINT:
        // As Dynarmic does without a debugger: report it and go on.
        LOG_CRITICAL(Core_ARM, "BKPT #{:#x} at {:#x}", syndrome, pc);
        return HaltReason::BreakLoop;
    default:
        LOG_CRITICAL(Core_ARM, "A32 exit {} at {:#x} (syndrome {:#x})", exit, pc, syndrome);
        return HaltReason::PrefetchAbort;
    }
}

HaltReason ArmScarletNx32::StepThread(Kernel::KThread* thread) {
    // Never reached: the debugger selects Dynarmic.
    LOG_ERROR(Core_ARM, "ScarletNX cannot single step");
    return HaltReason::BreakLoop;
}

void ArmScarletNx32::LoadCpu() {
    auto& cpu = m_cpu.cpu;
    static_assert(sizeof(cpu.d) <= sizeof(m_ctx.v));
    for (std::size_t i = 0; i < 16; i++)
        cpu.r[i] = u32(m_ctx.r[i]);
    cpu.cpsr = (m_ctx.pstate & ~ModeMask) | UserMode;
    std::memcpy(cpu.d, m_ctx.v.data(), sizeof(cpu.d));
    cpu.fpscr = FpsrFpcrToFpscr(m_ctx.fpsr, m_ctx.fpcr);
    cpu.tpidrurw = u32(m_ctx.tpidr);
    cpu.tpidruro = m_tpidrro;
    cpu.exclusive_valid = 0;
}

void ArmScarletNx32::SaveCpu() {
    const auto& cpu = m_cpu.cpu;
    for (std::size_t i = 0; i < 16; i++)
        m_ctx.r[i] = cpu.r[i];
    m_ctx.fp = cpu.r[11];
    m_ctx.sp = cpu.r[13];
    m_ctx.lr = cpu.r[14];
    m_ctx.pc = cpu.r[15];
    m_ctx.pstate = cpu.cpsr;
    std::memcpy(m_ctx.v.data(), cpu.d, sizeof(cpu.d));
    const auto [fpsr, fpcr] = FpscrToFpsrFpcr(cpu.fpscr);
    m_ctx.fpsr = fpsr;
    m_ctx.fpcr = fpcr;
    m_ctx.tpidr = cpu.tpidrurw;
}

void ArmScarletNx32::GetContext(Kernel::Svc::ThreadContext& ctx) const {
    ctx = m_ctx;
}

void ArmScarletNx32::SetContext(const Kernel::Svc::ThreadContext& ctx) {
    m_ctx = ctx;
}

void ArmScarletNx32::SetTpidrroEl0(u64 value) {
    m_tpidrro = u32(value);
}

void ArmScarletNx32::GetSvcArguments(std::span<uint64_t, 8> args) const {
    for (std::size_t i = 0; i < args.size(); i++)
        args[i] = u32(m_ctx.r[i]);
}

void ArmScarletNx32::SetSvcArguments(std::span<const uint64_t, 8> args) {
    for (std::size_t i = 0; i < args.size(); i++)
        m_ctx.r[i] = u32(args[i]);
}

u32 ArmScarletNx32::GetSvcNumber() const {
    return m_svc;
}

void ArmScarletNx32::SignalInterrupt(Kernel::KThread* thread) {
    // The run loop checks the exit between blocks. A block's own exit takes precedence.
    m_interrupt.store(true);
    u32 expected = SNX_ARM_OK;
    __atomic_compare_exchange_n(&m_cpu.cpu.exit, &expected, InterruptExit, false, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED);
}

void ArmScarletNx32::ClearInstructionCache() {
    m_process->ClearInstructionCache();
}

void ArmScarletNx32::InvalidateCacheRange(u64 addr, std::size_t size) {
    m_process->InvalidateCacheRange(addr, size);
}

} // namespace Core
