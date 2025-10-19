// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cinttypes>
#include <memory>

#include "common/signal_chain.h"
#include "core/arm/nce/arm_nce.h"
#include "core/arm/nce/interpreter_visitor.h"
#include "core/arm/nce/patcher.h"
#include "core/core.h"
#include "core/memory.h"
#include "core/hle/kernel/k_process.h"

#include "dynarmic/common/context.h"

#include <signal.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace Core {

namespace {

struct sigaction g_orig_bus_action;
struct sigaction g_orig_segv_action;

// Verify assembly offsets.
using NativeExecutionParameters = Kernel::KThread::NativeExecutionParameters;
static_assert(offsetof(NativeExecutionParameters, native_context) == TpidrEl0NativeContext);
static_assert(offsetof(NativeExecutionParameters, lock) == TpidrEl0Lock);
static_assert(offsetof(NativeExecutionParameters, magic) == TpidrEl0TlsMagic);

using namespace Common::Literals;
constexpr u32 StackSize = 128_KiB;

} // namespace

void* ArmNce::RestoreGuestContext(void* raw_context) {
    CTX_DECLARE(raw_context);
    // Restore all guest state except tpidr_el0.
    // Thread-local parameters will be located in x9.
    auto* tpidr = reinterpret_cast<NativeExecutionParameters*>(CTX_X(9));
    auto* guest_ctx = static_cast<GuestContext*>(tpidr->native_context);
    // Save host callee-saved registers.
    std::memcpy(guest_ctx->host_ctx.host_saved_vregs.data(), &CTX_Q(8),
                sizeof(guest_ctx->host_ctx.host_saved_vregs));
    // Save stack pointer.
    guest_ctx->host_ctx.host_sp = CTX_SP;
    CTX_PC = guest_ctx->sp;
    CTX_SP = guest_ctx->pc;
    CTX_PSTATE = guest_ctx->pstate;
    CTX_FPCR = guest_ctx->fpcr;
    CTX_FPSR = guest_ctx->fpsr;
    std::memcpy(&CTX_X(0), guest_ctx->cpu_registers.data(), sizeof(guest_ctx->cpu_registers));
    std::memcpy(&CTX_Q(0), guest_ctx->vector_registers.data(), sizeof(guest_ctx->vector_registers));
    // Return the new thread-local storage pointer.
    return tpidr;
}

void ArmNce::SaveGuestContext(GuestContext* guest_ctx, void* raw_context) {
    CTX_DECLARE(raw_context);
    // Save all guest registers except tpidr_el0.
    std::memcpy(guest_ctx->cpu_registers.data(), &CTX_X(0), sizeof(guest_ctx->cpu_registers));
    std::memcpy(guest_ctx->vector_registers.data(), &CTX_Q(0), sizeof(guest_ctx->vector_registers));
    guest_ctx->fpsr = CTX_FPSR;
    guest_ctx->fpcr = CTX_FPCR;
    guest_ctx->pc = CTX_PC;
    guest_ctx->sp = CTX_SP;
    guest_ctx->pstate = u32(CTX_PSTATE);
    // Restore stack pointer.
    CTX_SP = guest_ctx->host_ctx.host_sp;

    // Restore host callee-saved registers.
    std::memcpy(&CTX_X(19), guest_ctx->host_ctx.host_saved_regs.data(),
                sizeof(guest_ctx->host_ctx.host_saved_regs));
    std::memcpy(&CTX_Q(8), guest_ctx->host_ctx.host_saved_vregs.data(),
                sizeof(guest_ctx->host_ctx.host_saved_vregs));
    // Return from the call on exit by setting pc to x30.
    CTX_PC = guest_ctx->host_ctx.host_saved_regs[11];
    // Clear esr_el1 and return it.
    CTX_X(0) = guest_ctx->esr_el1.exchange(0);
}

bool ArmNce::HandleFailedGuestFault(GuestContext* guest_ctx, void* raw_info, void* raw_context) {
    CTX_DECLARE(raw_context);
    auto* info = static_cast<siginfo_t*>(raw_info);

    // We can't handle the access, so determine why we crashed.
    auto const is_prefetch_abort = CTX_PC == reinterpret_cast<u64>(info->si_addr);
    // For data aborts, skip the instruction and return to guest code.
    // This will allow games to continue in many scenarios where they would otherwise crash.
    if (!is_prefetch_abort) {
        CTX_PC += 4;
        return true;
    }

    // This is a prefetch abort.
    guest_ctx->esr_el1.fetch_or(static_cast<u64>(HaltReason::PrefetchAbort));

    // Forcibly mark the context as locked. We are still running.
    // We may race with SignalInterrupt here:
    // - If we lose the race, then SignalInterrupt will send us a signal we are masking,
    //   and it will do nothing when it is unmasked, as we have already left guest code.
    // - If we win the race, then SignalInterrupt will wait for us to unlock first.
    auto& thread_params = guest_ctx->parent->m_running_thread->GetNativeExecutionParameters();
    thread_params.lock.store(SpinLockLocked);

    // Return to host.
    SaveGuestContext(guest_ctx, raw_context);
    return false;
}

bool ArmNce::HandleGuestAlignmentFault(GuestContext* guest_ctx, void* raw_info, void* raw_context) {
    CTX_DECLARE(raw_context);
    auto& memory = guest_ctx->system->ApplicationMemory();
    // Match and execute an instruction.
    if (auto next_pc = MatchAndExecuteOneInstruction(memory, raw_context); next_pc) {
        CTX_PC = *next_pc;
        return true;
    }
    // We couldn't handle the access.
    return HandleFailedGuestFault(guest_ctx, raw_info, raw_context);
}

bool ArmNce::HandleGuestAccessFault(GuestContext* guest_ctx, void* raw_info, void* raw_context) {
    auto* info = static_cast<siginfo_t*>(raw_info);

    // Try to handle an invalid access.
    // TODO: handle accesses which split a page?
    const Common::ProcessAddress addr =
        (reinterpret_cast<u64>(info->si_addr) & ~Memory::YUZU_PAGEMASK);
    if (guest_ctx->system->ApplicationMemory().InvalidateNCE(addr, Memory::YUZU_PAGESIZE)) {
        // We handled the access successfully and are returning to guest code.
        return true;
    }

    // We couldn't handle the access.
    return HandleFailedGuestFault(guest_ctx, raw_info, raw_context);
}

void ArmNce::HandleHostAlignmentFault(int sig, void* raw_info, void* raw_context) {
    return g_orig_bus_action.sa_sigaction(sig, static_cast<siginfo_t*>(raw_info), raw_context);
}

void ArmNce::HandleHostAccessFault(int sig, void* raw_info, void* raw_context) {
    return g_orig_segv_action.sa_sigaction(sig, static_cast<siginfo_t*>(raw_info), raw_context);
}

void ArmNce::LockThread(Kernel::KThread* thread) {
    auto* thread_params = &thread->GetNativeExecutionParameters();
    LockThreadParameters(thread_params);
}

void ArmNce::UnlockThread(Kernel::KThread* thread) {
    auto* thread_params = &thread->GetNativeExecutionParameters();
    m_guest_ctx.tpidr_el0 = thread_params->tpidr_el0;
    m_guest_ctx.tpidrro_el0 = thread_params->tpidrro_el0;
    thread_params->native_context = nullptr;
    UnlockThreadParameters(thread_params);
}

HaltReason ArmNce::RunThread(Kernel::KThread* thread) {
    // Check if we're already interrupted.
    // If we are, we can just return immediately.
    HaltReason hr = static_cast<HaltReason>(m_guest_ctx.esr_el1.exchange(0));
    if (True(hr)) {
        return hr;
    }

    // Pre-fetch thread context data to improve cache locality
    auto* thread_params = &thread->GetNativeExecutionParameters();
    auto* process = thread->GetOwnerProcess();

    // Move non-critical operations outside the locked section
    const u64 tpidr_el0_cache = m_guest_ctx.tpidr_el0;
    const u64 tpidrro_el0_cache = m_guest_ctx.tpidrro_el0;

    // Critical section begins - minimize operations here
    m_running_thread = thread;
    m_guest_ctx.parent = this;
    thread_params->native_context = &m_guest_ctx;
    thread_params->tpidr_el0 = tpidr_el0_cache;
    thread_params->tpidrro_el0 = tpidrro_el0_cache;

    // Memory barrier to ensure visibility of changes
    std::atomic_thread_fence(std::memory_order_release);
    thread_params->is_running = true;

    // TODO: finding and creating the post handler needs to be locked
    // to deal with dynamic loading of NROs.
    const auto& post_handlers = process->GetPostHandlers();
    if (auto it = post_handlers.find(m_guest_ctx.pc); it != post_handlers.end()) {
        hr = ReturnToRunCodeByTrampoline(thread_params, &m_guest_ctx, it->second);
    } else {
        hr = ReturnToRunCodeByExceptionLevelChange(m_thread_id, thread_params);  // Android: Use "process handle SIGUSR2 -n true -p true -s false" (and SIGURG) in LLDB when debugging
    }

    // Critical section for thread cleanup
    std::atomic_thread_fence(std::memory_order_acquire);

    // Cache values before releasing thread
    const u64 final_tpidr_el0 = thread_params->tpidr_el0;

    // Minimize critical section
    thread_params->is_running = false;
    thread_params->native_context = nullptr;
    m_running_thread = nullptr;

    // Non-critical updates can happen after releasing the thread
    m_guest_ctx.tpidr_el0 = final_tpidr_el0;

    // Return the halt reason.
    return hr;
}

HaltReason ArmNce::StepThread(Kernel::KThread* thread) {
    return HaltReason::StepThread;
}

u32 ArmNce::GetSvcNumber() const {
    return m_guest_ctx.svc;
}

void ArmNce::GetSvcArguments(std::span<uint64_t, 8> args) const {
    for (size_t i = 0; i < 8; i++) {
        args[i] = m_guest_ctx.cpu_registers[i];
    }
}

void ArmNce::SetSvcArguments(std::span<const uint64_t, 8> args) {
    for (size_t i = 0; i < 8; i++) {
        m_guest_ctx.cpu_registers[i] = args[i];
    }
}

ArmNce::ArmNce(System& system, bool uses_wall_clock, std::size_t core_index)
    : ArmInterface{uses_wall_clock}, m_system{system}, m_core_index{core_index} {
    m_guest_ctx.system = &m_system;
}

ArmNce::~ArmNce() = default;

// Borrowed from libusb
static unsigned int posix_gettid(void) {
    static _Thread_local unsigned int tl_tid;
    int tid;
    if (tl_tid)
        return tl_tid;
#if defined(__ANDROID__)
    tid = gettid();
#elif defined(__APPLE__)
#ifdef HAVE_PTHREAD_THREADID_NP
    uint64_t thread_id;
    if (pthread_threadid_np(NULL, &thread_id) == 0)
        tid = (int)thread_id;
    else
        tid = -1;
#else
    tid = (int)pthread_mach_thread_np(pthread_self());
#endif
#elif defined(__HAIKU__)
    tid = get_pthread_thread_id(pthread_self());
#elif defined(__linux__)
    tid = (int)syscall(SYS_gettid);
#elif defined(__NetBSD__)
    tid = _lwp_self();
#elif defined(__OpenBSD__)
    /* The following only works with OpenBSD > 5.1 as it requires
    * real thread support. For 5.1 and earlier, -1 is returned. */
    tid = syscall(SYS_getthrid);
#elif defined(__sun__)
    tid = _lwp_self();
#else
    tid = -1;
#endif
    if (tid == -1) {
        /* If we don't have a thread ID, at least return a unique
        * value that can be used to distinguish individual
        * threads. */
        tid = (int)(intptr_t)pthread_self();
    }
    return tl_tid = (unsigned int)tid;
}

void ArmNce::Initialize() {
    if (m_thread_id == -1) {
        m_thread_id = posix_gettid();
    }

    // Configure signal stack.
    if (!m_stack) {
        m_stack = std::make_unique<u8[]>(StackSize);

        stack_t ss{};
        ss.ss_sp = m_stack.get();
        ss.ss_size = StackSize;
        sigaltstack(&ss, nullptr);
    }

    // Set up signals.
    static std::once_flag flag;
    std::call_once(flag, [] {
        using HandlerType = decltype(sigaction::sa_sigaction);

        sigset_t signal_mask;
        sigemptyset(&signal_mask);
        sigaddset(&signal_mask, ReturnToRunCodeByExceptionLevelChangeSignal);
        sigaddset(&signal_mask, BreakFromRunCodeSignal);
        sigaddset(&signal_mask, GuestAlignmentFaultSignal);
        sigaddset(&signal_mask, GuestAccessFaultSignal);

        struct sigaction return_to_run_code_action {};
        return_to_run_code_action.sa_flags = SA_SIGINFO | SA_ONSTACK;
        return_to_run_code_action.sa_sigaction = reinterpret_cast<HandlerType>(
            &ArmNce::ReturnToRunCodeByExceptionLevelChangeSignalHandler);
        return_to_run_code_action.sa_mask = signal_mask;
        Common::SigAction(ReturnToRunCodeByExceptionLevelChangeSignal, &return_to_run_code_action,
                        nullptr);

        struct sigaction break_from_run_code_action {};
        break_from_run_code_action.sa_flags = SA_SIGINFO | SA_ONSTACK;
        break_from_run_code_action.sa_sigaction =
            reinterpret_cast<HandlerType>(&ArmNce::BreakFromRunCodeSignalHandler);
        break_from_run_code_action.sa_mask = signal_mask;
        Common::SigAction(BreakFromRunCodeSignal, &break_from_run_code_action, nullptr);

        struct sigaction alignment_fault_action {};
        alignment_fault_action.sa_flags = SA_SIGINFO | SA_ONSTACK;
        alignment_fault_action.sa_sigaction =
            reinterpret_cast<HandlerType>(&ArmNce::GuestAlignmentFaultSignalHandler);
        alignment_fault_action.sa_mask = signal_mask;
        Common::SigAction(GuestAlignmentFaultSignal, &alignment_fault_action, nullptr);

        struct sigaction access_fault_action {};
        access_fault_action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESTART;
        access_fault_action.sa_sigaction =
            reinterpret_cast<HandlerType>(&ArmNce::GuestAccessFaultSignalHandler);
        access_fault_action.sa_mask = signal_mask;
        Common::SigAction(GuestAccessFaultSignal, &access_fault_action, &g_orig_segv_action);
    });
}

void ArmNce::SetTpidrroEl0(u64 value) {
    m_guest_ctx.tpidrro_el0 = value;
}

void ArmNce::GetContext(Kernel::Svc::ThreadContext& ctx) const {
    for (size_t i = 0; i < 29; i++) {
        ctx.r[i] = m_guest_ctx.cpu_registers[i];
    }
    ctx.fp = m_guest_ctx.cpu_registers[29];
    ctx.lr = m_guest_ctx.cpu_registers[30];
    ctx.sp = m_guest_ctx.sp;
    ctx.pc = m_guest_ctx.pc;
    ctx.pstate = m_guest_ctx.pstate;
    ctx.v = m_guest_ctx.vector_registers;
    ctx.fpcr = m_guest_ctx.fpcr;
    ctx.fpsr = m_guest_ctx.fpsr;
    ctx.tpidr = m_guest_ctx.tpidr_el0;
}

void ArmNce::SetContext(const Kernel::Svc::ThreadContext& ctx) {
    for (size_t i = 0; i < 29; i++) {
        m_guest_ctx.cpu_registers[i] = ctx.r[i];
    }
    m_guest_ctx.cpu_registers[29] = ctx.fp;
    m_guest_ctx.cpu_registers[30] = ctx.lr;
    m_guest_ctx.sp = ctx.sp;
    m_guest_ctx.pc = ctx.pc;
    m_guest_ctx.pstate = ctx.pstate;
    m_guest_ctx.vector_registers = ctx.v;
    m_guest_ctx.fpcr = ctx.fpcr;
    m_guest_ctx.fpsr = ctx.fpsr;
    m_guest_ctx.tpidr_el0 = ctx.tpidr;
}

void ArmNce::SignalInterrupt(Kernel::KThread* thread) {
    // Add break loop condition.
    m_guest_ctx.esr_el1.fetch_or(static_cast<u64>(HaltReason::BreakLoop));

    // Lock the thread context.
    auto* params = &thread->GetNativeExecutionParameters();
    LockThreadParameters(params);

    if (params->is_running) {
        // We should signal to the running thread.
        // The running thread will unlock the thread context.
#ifdef __linux__
        syscall(SYS_tkill, m_thread_id, BreakFromRunCodeSignal);
#else
        pthread_kill(m_thread_id, BreakFromRunCodeSignal);
#endif
    } else {
        // If the thread is no longer running, we have nothing to do.
        UnlockThreadParameters(params);
    }
}

const std::size_t CACHE_PAGE_SIZE = 4096;

void ArmNce::ClearInstructionCache() {
#if defined(__GNUC__) || defined(__clang__)
    void* start = (void*)((uintptr_t)__builtin_return_address(0) & ~(CACHE_PAGE_SIZE - 1));
    void* end =
        (void*)((uintptr_t)start + CACHE_PAGE_SIZE * 2); // Clear two pages for better coverage
    // Prefetch next likely pages
    __builtin_prefetch((void*)((uintptr_t)end), 1, 3);
    __builtin___clear_cache(static_cast<char*>(start), static_cast<char*>(end));
#endif
#ifdef __aarch64__
    // Ensure all previous memory operations complete
    asm volatile("dmb ish" ::: "memory");
    asm volatile("dsb ish" ::: "memory");
    asm volatile("isb" ::: "memory");
#endif
}

void ArmNce::InvalidateCacheRange(u64 addr, std::size_t size) {
    #if defined(__GNUC__) || defined(__clang__)
        // Align the start address to cache line boundary for better performance
        const size_t CACHE_LINE_SIZE = 64;
        addr &= ~(CACHE_LINE_SIZE - 1);

        // Round up size to nearest cache line
        size = (size + CACHE_LINE_SIZE - 1) & ~(CACHE_LINE_SIZE - 1);

        // Prefetch the range to be invalidated
        for (size_t offset = 0; offset < size; offset += CACHE_LINE_SIZE) {
            __builtin_prefetch((void*)(addr + offset), 1, 3);
        }
    #endif

    this->ClearInstructionCache();
}

} // namespace Core
