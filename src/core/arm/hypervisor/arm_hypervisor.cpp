// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <mach/mach_time.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <format>
#include <mutex>
#include <shared_mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "common/alignment.h"
#include "common/logging.h"
#include "common/settings.h"
#include "core/arm/hypervisor/arm_hypervisor.h"
#include "core/arm/hypervisor/hv_machine.h"
#include "core/arm/hypervisor/hv_page_table.h"
#include "core/arm/nce/interpreter_visitor.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/device_memory.h"
#include "core/hle/kernel/board/nintendo/nx/k_system_control.h"
#include "core/memory.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/k_thread.h"
#include "core/hle/kernel/kernel.h"

namespace Core {
namespace {
enum class ExceptionClass : u32 {
    TrappedWfiWfe = 0b000001,
    TrappedSveFpSimd = 0b000111,
    SvcAarch64 = 0b010101,
    HvcAarch64 = 0b010110,
    TrappedMsrMrsSystem = 0b011000,
    InstructionAbortLowerEl = 0b100000,
    DataAbortLowerEl = 0b100100,
    SoftwareStepLowerEl = 0b110010,
    BreakpointLowerEl = 0b110000,
    WatchpointLowerEl = 0b110100,
    BrkAarch64 = 0b111100,
};

[[nodiscard]] constexpr ExceptionClass GetExceptionClass(u64 esr) {
    return static_cast<ExceptionClass>((esr >> 26) & 0x3F);
}

enum class VectorEntry : u32 {
    CurrentElSp0Sync = 0,
    CurrentElSp0Irq = 1,
    CurrentElSp0Fiq = 2,
    CurrentElSp0SError = 3,
    CurrentElSpxSync = 4,
    CurrentElSpxIrq = 5,
    CurrentElSpxFiq = 6,
    CurrentElSpxSError = 7,
    LowerEl64Sync = 8,
    LowerEl64Irq = 9,
    LowerEl64Fiq = 10,
    LowerEl64SError = 11,
    LowerEl32Sync = 12,
    LowerEl32Irq = 13,
    LowerEl32Fiq = 14,
    LowerEl32SError = 15,
};

[[nodiscard]] constexpr bool IsCurrentEl(VectorEntry entry) {
    return static_cast<u32>(entry) < static_cast<u32>(VectorEntry::LowerEl64Sync);
}

[[nodiscard]] constexpr const char* VectorEntryName(VectorEntry entry) {
    switch (entry) {
    case VectorEntry::CurrentElSp0Sync: return "current EL SP0 synchronous";
    case VectorEntry::CurrentElSp0Irq: return "current EL SP0 IRQ";
    case VectorEntry::CurrentElSp0Fiq: return "current EL SP0 FIQ";
    case VectorEntry::CurrentElSp0SError: return "current EL SP0 SError";
    case VectorEntry::CurrentElSpxSync: return "current EL SPx synchronous";
    case VectorEntry::CurrentElSpxIrq: return "current EL SPx IRQ";
    case VectorEntry::CurrentElSpxFiq: return "current EL SPx FIQ";
    case VectorEntry::CurrentElSpxSError: return "current EL SPx SError";
    case VectorEntry::LowerEl64Sync: return "lower EL AArch64 synchronous";
    case VectorEntry::LowerEl64Irq: return "lower EL AArch64 IRQ";
    case VectorEntry::LowerEl64Fiq: return "lower EL AArch64 FIQ";
    case VectorEntry::LowerEl64SError: return "lower EL AArch64 SError";
    case VectorEntry::LowerEl32Sync: return "lower EL AArch32 synchronous";
    case VectorEntry::LowerEl32Irq: return "lower EL AArch32 IRQ";
    case VectorEntry::LowerEl32Fiq: return "lower EL AArch32 FIQ";
    case VectorEntry::LowerEl32SError: return "lower EL AArch32 SError";
    }
    return "unknown";
}

constexpr u64 PSTATE_SS = 1ull << 21;
constexpr u64 MDSCR_EL1_SS = 1ull << 0;
constexpr u64 CPSR_EL1H = 0x3C5;
constexpr u64 SPSR_MODE_MASK = 0x1F;
constexpr u64 MAIR_EL1_VALUE = 0xFF;

// Guards guest execution against page table mutation. Every vCPU holds this shared
// while inside hv_vcpu_run; anything that changes a descriptor takes it exclusive
// through GuestPause first. Doing it the other way round loses stores, a core still
// on the old translation writes into the old page and nothing replays those once
// the shootdown lands. Bounded because the vtimer is armed, see ArmVirtualTimer.
std::shared_mutex guest_execution_lock;

// Only let one vCPU be inside hv_vcpu_run at a time.
//
// SMM2 and ACNH corrupt an NVN command buffer when two guest threads record into
// it at once. EndRecording finds the buffer not recording, hands back a null
// handle, and the game replays it a few frames later and dies on address 0. An
// instrumented run showed zero bad handles with this on, every run without it had
// them.
//
// TODO(Maufeat): Workaround. Something in our SMP handling lets the two threads
// overlap in a way real hardware does not. Kernel side looks clean so far
// (scheduler, arbiter, condvar, exclusive monitors all match yuzu).
constexpr bool SERIALISE_GUEST_EXECUTION = true;

constexpr u64 CTR_EL0_VALUE = 0x8444C004ull;
// SCTLR_EL1, taken from Hydra's hypervisor backend.
constexpr u64 SCTLR_EL1_UCT = 1ull << 15;
constexpr u64 SCTLR_EL1_VALUE = 0x34D5D925ull & ~SCTLR_EL1_UCT;
constexpr u64 CPACR_EL1_FPEN = 3ull << 20;
// TCR_EL1, same source as SCTLR_EL1.
constexpr u64 TCR_EL1_VALUE = 0x12B5193519ull;
constexpr u64 TRAMPOLINE_VA =
    HvMachine::KERNEL_REGION_VA + HvMachine::EXCEPTION_TRAMPOLINE_OFFSET;

// How long a core may stay in the guest before the vtimer preempts it. Only a
// perf knob, serialisation still works at any slice.
//
// Serialised, a core spinning on a lock the others cannot release burns its whole
// slice. At 1ms that cost us ~20x (FFT ran at 2-3 FPS). Short slices are not free
// either, the exit rate goes up 6x from 25us to 4us and the host pays it. On the
// fanless Air the P-cluster already sits at 828-1002 MHz out of 3204 with 0% idle,
// so that comes out of a quarter clock. 4-12us gave 42-48 FPS where 25us held 60.
//
// A long slice is free when the guest exits on its own, the timer just never fires,
// so use the long one when nothing is queued and the short one when something is.
//
// Do not go under 3us. At 2us the slice is about as long as the hv_vcpu_run round
// trip, so the deadline is already gone when we handle the exit, the vCPU exits
// again right away and nothing progresses. Needs a SIGKILL to get out of.
constexpr u64 VTIMER_PERIOD_NS = 1000 * 1000;
constexpr u64 VTIMER_PERIOD_UNCONTENDED_NS = 1000 * 1000;
constexpr u64 VTIMER_PERIOD_CONTENDED_NS = 25 * 1000;

std::atomic<u32> guest_lock_waiters{0};

// Maps GPU-visible guest pages read-only and upgrades them on the faulting store.
constexpr bool TRACK_GPU_WRITES = true;

[[nodiscard]] constexpr bool IsPermissionFault(u64 esr) {
    return (esr & 0x3C) == 0x0C;
}

[[nodiscard]] constexpr bool IsTranslationFault(u64 esr) {
    return (esr & 0x3C) == 0x04;
}

[[nodiscard]] constexpr const char* DescribeFaultStatus(u64 esr) {
    switch (esr & 0x3F) {
    case 0x00: case 0x01: case 0x02: case 0x03:
        return "address size fault";
    case 0x04: case 0x05: case 0x06: case 0x07:
        return "translation fault";
    case 0x08: case 0x09: case 0x0A: case 0x0B:
        return "access flag fault";
    case 0x0C: case 0x0D: case 0x0E: case 0x0F:
        return "permission fault";
    case 0x10:
        return "synchronous external abort";
    case 0x14: case 0x15: case 0x16: case 0x17:
        return "synchronous external abort on translation table walk";
    case 0x18:
        return "synchronous parity or ECC error";
    case 0x21:
        return "alignment fault";
    case 0x30:
        return "TLB conflict abort";
    case 0x31:
        return "unsupported atomic hardware update";
    default:
        return "unknown fault status";
    }
}

[[nodiscard]] constexpr bool IsWriteFault(u64 esr) {
    return (esr & (1ull << 6)) != 0;
}

// The same return, preceded by TLB and instruction cache maintenance.
constexpr u64 FLUSH_TRAMPOLINE_VA =
    HvMachine::KERNEL_REGION_VA + HvMachine::FLUSH_TRAMPOLINE_OFFSET;

// Rate limiter for faults that repeat at VM-exit rates.
class Throttle {
public:
    [[nodiscard]] u64 Tick() noexcept {
        const u64 n = m_count.fetch_add(1, std::memory_order_relaxed) + 1;
        return (n <= FIRST || n % EVERY == 0) ? n : 0;
    }

private:
    static constexpr u64 FIRST = 8;
    static constexpr u64 EVERY = 100'000;
    std::atomic<u64> m_count{0};
};

std::mutex backends_mutex;
std::vector<ArmHypervisor*> backends;

// GPU-cached pages a fault left accessible. Loads and stores to them are not seen, so the GPU
// writing the range, or reading it back, takes the mapping away again, see OnGpuAccess.
std::mutex watched_mutex;
std::unordered_set<u64> watched_pages;

void ForgetWatched(u64 begin, u64 end) {
    std::scoped_lock lock{watched_mutex};
    if (watched_pages.empty()) {
        return;
    }
    s64 removed = 0;
    for (u64 va = begin; va < end; va += HvPageTable::PAGE_SIZE) {
        removed += static_cast<s64>(watched_pages.erase(va));
    }
    if (removed != 0) {
        ArmInterface::AddWatchedPages(-removed);
    }
}

} // Anonymous namespace

ArmHypervisor::ArmHypervisor(System& system, bool uses_wall_clock, Kernel::KProcess* process,
                             std::size_t core_index)
    : ArmInterface{uses_wall_clock}, m_system{system}, m_process{process},
      m_core_index{core_index} {
    RegisterBackend();
    ArmInterface::SetWantsMappingNotifications();
    if constexpr (TRACK_GPU_WRITES) {
        ArmInterface::SetWantsRasterizerTracking();
    }
}

void ArmHypervisor::RegisterBackend() {
    std::scoped_lock lock{backends_mutex};
    backends.push_back(this);
}

void ArmHypervisor::UnregisterBackend() {
    std::scoped_lock lock{backends_mutex};
    backends.erase(std::remove(backends.begin(), backends.end(), this), backends.end());
}

void DetachAllPageTables() {
    std::scoped_lock lock{backends_mutex};
    for (ArmHypervisor* const backend : backends) {
        backend->DetachPageTable();
    }
}

ArmHypervisor::GuestPause::GuestPause() {
    // hv_vcpus_exit is only a nudge, Apple documents no latching or completion
    // semantics for it (one sentence in total), so the lock is what gives us the
    // barrier. The vtimer bounds the wait for a core that ignores the nudge.
    {
        std::scoped_lock backends_lock{backends_mutex};
        for (ArmHypervisor* const backend : backends) {
            if (backend->m_vcpu_created.load(std::memory_order::acquire)) {
                hv_vcpus_exit(std::addressof(backend->m_vcpu), 1);
            }
        }
    }
    lock = std::unique_lock<std::shared_mutex>{guest_execution_lock};
}

void ArmHypervisor::MarkFlushPendingEverywhere() {
    // Call with a GuestPause held. No core is inside hv_vcpu_run, so none can
    // consume its flag and enter the guest between the store here and the
    // descriptor change.
    const HvPageTable* const table = m_page_table.load(std::memory_order::acquire);
    std::scoped_lock lock{backends_mutex};
    for (ArmHypervisor* const backend : backends) {
        if (backend->m_page_table.load(std::memory_order::acquire) != table) {
            continue;
        }
        backend->m_flush_pending.store(true, std::memory_order::seq_cst);
    }
}

ArmHypervisor::~ArmHypervisor() {
    UnregisterBackend();
    if (!m_vcpu_created.load(std::memory_order::acquire)) {
        return;
    }
    // hv_vcpu_destroy is as thread-bound as creation. Leaking it from the wrong
    // thread is the lesser problem, the VM goes with the process anyway.
    if (std::this_thread::get_id() == m_owner_thread) {
        hv_vcpu_destroy(m_vcpu);
    } else {
        LOG_WARNING(Core_ARM, "vCPU for core {} outlived its thread; leaking it", m_core_index);
    }
}

bool ArmHypervisor::IsAvailable() {
    const HvMachine* const machine = HvMachine::Instance();
    return machine != nullptr && machine->HasKernelRegion();
}

void ArmHypervisor::Initialize() { }

bool ArmHypervisor::EnsureVcpu() {
    if (m_vcpu_created.load(std::memory_order::acquire)) {
        return true;
    }
    if (m_vcpu_failed) {
        return false;
    }

    HvMachine* const machine = HvMachine::Instance();
    if (machine == nullptr) {
        LOG_ERROR(Core_ARM, "No hypervisor VM; the backend should not have been selected");
        m_vcpu_failed = true;
        return false;
    }

    const hv_return_t result = hv_vcpu_create(&m_vcpu, &m_exit, nullptr);
    if (result != HV_SUCCESS) {
        LOG_ERROR(Core_ARM, "hv_vcpu_create failed for core {}: {:#x}", m_core_index,
                  static_cast<u32>(result));
        m_vcpu_failed = true;
        return false;
    }
    m_owner_thread = std::this_thread::get_id();
    m_vcpu_created.store(true, std::memory_order::release);

    SetSysReg(HV_SYS_REG_MAIR_EL1, MAIR_EL1_VALUE);
    SetSysReg(HV_SYS_REG_TCR_EL1, TCR_EL1_VALUE);
    SetSysReg(HV_SYS_REG_SCTLR_EL1, SCTLR_EL1_VALUE);
    SetSysReg(HV_SYS_REG_CPACR_EL1, CPACR_EL1_FPEN);
    SetSysReg(HV_SYS_REG_CNTKCTL_EL1, 0x3);

    if (const hv_return_t vtimer_set = hv_vcpu_set_vtimer_offset(m_vcpu, 0);
        vtimer_set != HV_SUCCESS) {
        LOG_ERROR(Core_ARM, "hv_vcpu_set_vtimer_offset failed on core {}: {:#x}", m_core_index,
                  static_cast<u32>(vtimer_set));
    } else {
        // Read back: the accessor supports it, unlike the system register.
        u64 offset = ~u64{0};
        if (const hv_return_t vtimer_get = hv_vcpu_get_vtimer_offset(m_vcpu, &offset);
            vtimer_get != HV_SUCCESS) {
            LOG_WARNING(Core_ARM, "hv_vcpu_get_vtimer_offset failed on core {}: {:#x}",
                        m_core_index, static_cast<u32>(vtimer_get));
        } else if (offset != 0) {
            LOG_ERROR(Core_ARM,
                      "Virtual timer offset is {:#x} on core {} after being set to zero; "
                      "CNTVCT does not match the timebase the virtual timer is programmed "
                      "against and preemption will not fire",
                      offset, m_core_index);
        }
    }
    SetSysReg(HV_SYS_REG_VBAR_EL1, HvMachine::KERNEL_REGION_VA);
    SetSysReg(HV_SYS_REG_TTBR1_EL1, machine->KernelPageTable().BaseRegister());
    auto& device_memory = m_system.DeviceMemory();
    m_dram_backing = device_memory.buffer.BackingBasePointer();
    m_dram_size = Kernel::Board::Nintendo::Nx::KSystemControl::Init::GetIntendedMemorySize();
    LOG_INFO(Core_ARM, "Mapping {} MiB of guest DRAM at IPA {:#x} (backing {})",
             m_dram_size / (1024 * 1024), DramMemoryMap::Base,
             static_cast<const void*>(m_dram_backing));
    if (!machine->EnsureDramMapped(device_memory.buffer.BackingBasePointer(), DramMemoryMap::Base,
                                   m_dram_size)) {
        m_vcpu_failed = true;
        return false;
    }

    // The processes own translation. Without it TTBR0 reads zero and the first
    // instruction fetch faults with S1PTW set.
    HvPageTable* const page_table = machine->ProcessPageTable(m_process);
    m_page_table.store(page_table, std::memory_order::release);
    if (page_table == nullptr) {
        LOG_ERROR(Core_ARM, "Could not create a page table for the process");
        m_vcpu_failed = true;
        return false;
    }
    SetSysReg(HV_SYS_REG_TTBR0_EL1, page_table->BaseRegister());

    if (const hv_return_t trap_debug = hv_vcpu_set_trap_debug_exceptions(m_vcpu, true);
        trap_debug != HV_SUCCESS) {
        LOG_ERROR(Core_ARM,
                  "hv_vcpu_set_trap_debug_exceptions failed on core {}: {:#x}; breakpoints and "
                  "stepping will not reach the host",
                  m_core_index, static_cast<u32>(trap_debug));
    }

    ArmVirtualTimer();
    LoadContextToVcpu(m_context);
    SetSysReg(HV_SYS_REG_TPIDRRO_EL0, m_tpidrro_el0);

    SetReg(HV_REG_PC, TRAMPOLINE_VA);

    LOG_INFO(Core_ARM, "Created a vCPU for core {}", m_core_index);

    return true;
}

void ArmHypervisor::ArmVirtualTimer() {
    // Without CNTV_CTL_EL0.ENABLE set here, HV_EXIT_REASON_VTIMER_ACTIVATED can
    // never fire and the guest cannot arm it either (CNTKCTL_EL1 = 0x3 leaves
    // EL0VTEN clear), leaving nothing to bound time in the guest.
    static const auto ticks_for = [](u64 period_ns) {
        mach_timebase_info_data_t info{};
        mach_timebase_info(&info);
        return ((period_ns * info.denom) + (info.numer - 1)) / info.numer;
    };
    static const u64 delta_ticks = ticks_for(VTIMER_PERIOD_NS);
    static const u64 uncontended_ticks = ticks_for(VTIMER_PERIOD_UNCONTENDED_NS);
    static const u64 contended_ticks = ticks_for(VTIMER_PERIOD_CONTENDED_NS);

    u64 period = delta_ticks;
    if constexpr (SERIALISE_GUEST_EXECUTION) {
        period = guest_lock_waiters.load(std::memory_order::relaxed) != 0 ? contended_ticks
                                                                         : uncontended_ticks;
    }
    SetSysReg(HV_SYS_REG_CNTV_CVAL_EL0, mach_absolute_time() + period);
    SetSysReg(HV_SYS_REG_CNTV_CTL_EL0, 1);
}

void ArmHypervisor::ShortenRunningSlice() {
    // A core that armed the long slice while nothing was queued would sit on the
    // guest for up to 1ms once someone starts waiting. Kick it so it re-arms short.
    std::scoped_lock backends_lock{backends_mutex};
    for (ArmHypervisor* const backend : backends) {
        if (backend->m_in_guest.load(std::memory_order::acquire) &&
            backend->m_vcpu_created.load(std::memory_order::acquire)) {
            hv_vcpus_exit(std::addressof(backend->m_vcpu), 1);
        }
    }
}

void ArmHypervisor::EnsureContextFresh() {
    if (m_context_fresh) {
        return;
    }
    std::scoped_lock context_lock{m_context_mutex};
    StoreContextFromVcpu(m_context);
    m_context_fresh = true;
}

bool ArmHypervisor::OnOwnerThread() const {
    return m_vcpu_created.load(std::memory_order::acquire) &&
           std::this_thread::get_id() == m_owner_thread;
}

void ArmHypervisor::ReportIfFailed(const char* call, u32 reg, u32 result) const {
    if (result == HV_SUCCESS) {
        return;
    }
    static Throttle throttle;
    if (const u64 n = throttle.Tick()) {
        LOG_ERROR(Core_ARM, "{}({:#x}) failed on core {}: {:#x}{} ({} so far)", call, reg,
                  m_core_index, result, OnOwnerThread() ? "" : ", from a foreign thread", n);
    }
}

u64 ArmHypervisor::GetReg(hv_reg_t reg) const {
    u64 value{};
    ReportIfFailed("hv_vcpu_get_reg", static_cast<u32>(reg),
                   hv_vcpu_get_reg(m_vcpu, reg, &value));
    return value;
}

void ArmHypervisor::SetReg(hv_reg_t reg, u64 value) {
    ReportIfFailed("hv_vcpu_set_reg", static_cast<u32>(reg), hv_vcpu_set_reg(m_vcpu, reg, value));
}

u64 ArmHypervisor::GetSysReg(hv_sys_reg_t reg) const {
    u64 value{};
    ReportIfFailed("hv_vcpu_get_sys_reg", static_cast<u32>(reg),
                   hv_vcpu_get_sys_reg(m_vcpu, reg, &value));
    return value;
}

void ArmHypervisor::SetSysReg(hv_sys_reg_t reg, u64 value) {
    ReportIfFailed("hv_vcpu_set_sys_reg", static_cast<u32>(reg),
                   hv_vcpu_set_sys_reg(m_vcpu, reg, value));
}

HaltReason ArmHypervisor::RunThread(Kernel::KThread* thread) {
    return Run(false);
}

HaltReason ArmHypervisor::StepThread(Kernel::KThread* thread) {
    return Run(true);
}

HaltReason ArmHypervisor::Run(bool stepping) {
    if (!EnsureVcpu()) {
        return HaltReason::BreakLoop;
    }

    if (std::this_thread::get_id() != m_owner_thread) {
        LOG_ERROR(Core_ARM, "Core {} vCPU driven from a foreign thread", m_core_index);
        return HaltReason::BreakLoop;
    }

    // Consume a pending interrupt instead of destroying it. physical_core publishes
    // m_arm_interface under its guard and releases it before RunThread, so an
    // Interrupt() can land in that gap. Throwing it away wedges a thread that only
    // produces internally-resolved exits (demand faults, CNTVCT reads), since the
    // loop below has no other escape. dynarmic and NCE behave the same way.
    if (m_interrupted.exchange(false, std::memory_order::acq_rel)) {
        return HaltReason::BreakLoop;
    }

    u64 mdscr = GetSysReg(HV_SYS_REG_MDSCR_EL1);
    if (stepping) {
        mdscr |= MDSCR_EL1_SS;
    } else {
        mdscr &= ~MDSCR_EL1_SS;
    }
    SetSysReg(HV_SYS_REG_MDSCR_EL1, mdscr);

    {
        // Foreign threads write m_context (SetContext, SetSvcArguments) while we own
        // it, so the guest can otherwise resume on a half-updated context.
        std::scoped_lock context_lock{m_context_mutex};
        if (stepping) {
            SetSysReg(HV_SYS_REG_SPSR_EL1, GetSysReg(HV_SYS_REG_SPSR_EL1) | PSTATE_SS);
        } else {
            m_context.pstate &= ~static_cast<u32>(PSTATE_SS);
        }
        if (m_context_dirty.exchange(false, std::memory_order::acquire)) {
            LoadContextToVcpu(m_context);
            SetSysReg(HV_SYS_REG_TPIDRRO_EL0, m_tpidrro_el0);
        }
        m_context_fresh = true;
    }

    while (true) {
        if (std::this_thread::get_id() != m_owner_thread) {
            LOG_ERROR(Core_ARM, "Core {} vCPU driven from a foreign thread", m_core_index);
            return HaltReason::BreakLoop;
        }

        // Only the entry is scoped, the exit handling below runs unlocked so a fault
        // handler on this thread can take the exclusive side without deadlocking
        // against itself. Keep the flush-flag exchange inside the lock: outside it a
        // core could exchange its flag to false, let a mutator set it again and
        // finish the mutation, then enter the guest with a stale TLB and no flush.
        hv_return_t result{};
        {
            std::shared_lock<std::shared_mutex> shared;
            std::unique_lock<std::shared_mutex> exclusive;
            if constexpr (SERIALISE_GUEST_EXECUTION) {
                // Say we are waiting before we block, so whoever is in the guest
                // can see it and cut its slice short.
                if (guest_lock_waiters.fetch_add(1, std::memory_order::relaxed) == 0) {
                    ShortenRunningSlice();
                }
                exclusive = std::unique_lock<std::shared_mutex>{guest_execution_lock};
                guest_lock_waiters.fetch_sub(1, std::memory_order::relaxed);
            } else {
                shared = std::shared_lock<std::shared_mutex>{guest_execution_lock};
            }

            m_in_guest.store(true, std::memory_order::seq_cst);

            const bool pending = m_flush_pending.exchange(false, std::memory_order::seq_cst);
            if (pending) {
                SetReg(HV_REG_PC, FLUSH_TRAMPOLINE_VA);
            }
            m_flushing_this_entry = pending;

            result = hv_vcpu_run(m_vcpu);
            m_in_guest.store(false, std::memory_order::seq_cst);
        }
        const bool flushing = m_flushing_this_entry;

        if (result != HV_SUCCESS) {
            LOG_ERROR(Core_ARM, "hv_vcpu_run failed on core {}: {:#x}", m_core_index,
                      static_cast<u32>(result));
            return HaltReason::BreakLoop;
        }

        const bool entered_guest = ParkAtTrampoline();
        if (flushing && !entered_guest) {
            m_flush_pending.store(true, std::memory_order::seq_cst);
        }

        m_context_fresh = false;
        if (const std::optional<HaltReason> reason = HandleExit()) {
            EnsureContextFresh();
            // Fold in a pending interrupt instead of leaving it for a later Run to
            // find. HaltReason is a flag enum so it composes, as NCE also relies on.
            if (m_interrupted.exchange(false, std::memory_order::acq_rel)) {
                return *reason | HaltReason::BreakLoop;
            }
            return *reason;
        }

        if (m_interrupted.exchange(false, std::memory_order::acq_rel)) {
            EnsureContextFresh();
            return HaltReason::BreakLoop;
        }
    }
}

bool ArmHypervisor::ParkAtTrampoline() {
    const bool through_vectors =
        m_exit->reason == HV_EXIT_REASON_EXCEPTION &&
        GetExceptionClass(m_exit->exception.syndrome) == ExceptionClass::HvcAarch64;

    const bool in_guest = !through_vectors && (GetReg(HV_REG_CPSR) & SPSR_MODE_MASK) == 0;

    if (in_guest) {
        SetSysReg(HV_SYS_REG_ELR_EL1, GetReg(HV_REG_PC));
        SetSysReg(HV_SYS_REG_SPSR_EL1, GetReg(HV_REG_CPSR));
        SetReg(HV_REG_CPSR, CPSR_EL1H);
    } else if (!through_vectors) {
        const u64 pc = GetReg(HV_REG_PC);
        if (pc >= HvMachine::KERNEL_REGION_VA &&
            pc < HvMachine::KERNEL_REGION_VA + HvMachine::EXCEPTION_TRAMPOLINE_OFFSET) {
            m_pending_vector = static_cast<u32>((pc - HvMachine::KERNEL_REGION_VA) / 0x80);
            m_pending_vector_valid = true;
        }
    }

    SetReg(HV_REG_PC, TRAMPOLINE_VA);
    return through_vectors || in_guest;
}

HaltReason ArmHypervisor::ConsumePendingVectorOr(HaltReason fallback) {
    if (!m_pending_vector_valid) {
        return fallback;
    }
    m_pending_vector_valid = false;

    switch (static_cast<VectorEntry>(m_pending_vector)) {
    case VectorEntry::LowerEl64Sync:
        if (const std::optional<HaltReason> reason = HandleGuestSynchronous()) {
            return *reason;
        }
        return fallback;
    case VectorEntry::LowerEl64Irq:
    case VectorEntry::LowerEl64Fiq:
        return fallback;
    default:
        LOG_ERROR(Core_ARM, "Interrupted exception through unexpected vector entry {}",
                  m_pending_vector);
        return HaltReason::BreakLoop;
    }
}

std::optional<HaltReason> ArmHypervisor::HandleExit() {
    switch (m_exit->reason) {
    case HV_EXIT_REASON_EXCEPTION:
        break;
    case HV_EXIT_REASON_VTIMER_ACTIVATED:
        ArmVirtualTimer();
        if (const hv_return_t unmask = hv_vcpu_set_vtimer_mask(m_vcpu, false);
            unmask != HV_SUCCESS) {
            LOG_ERROR(Core_ARM,
                      "hv_vcpu_set_vtimer_mask(false) failed on core {}: {:#x}; preemption is "
                      "now disabled on this core",
                      m_core_index, static_cast<u32>(unmask));
        }
        return ConsumePendingVectorOr(HaltReason::BreakLoop);
    case HV_EXIT_REASON_CANCELED:
        return ConsumePendingVectorOr(HaltReason::BreakLoop);
    default:
        LOG_ERROR(Core_ARM, "Unexpected VM exit reason {}", static_cast<u32>(m_exit->reason));
        return HaltReason::BreakLoop;
    }

    const u64 syndrome = m_exit->exception.syndrome;

    if (GetExceptionClass(syndrome) == ExceptionClass::TrappedMsrMrsSystem) {
        HandleSystemRegisterTrap(static_cast<u32>(syndrome));
        SetSysReg(HV_SYS_REG_ELR_EL1, GetSysReg(HV_SYS_REG_ELR_EL1) + 4);
        return std::nullopt;
    }

    if (GetExceptionClass(syndrome) == ExceptionClass::SoftwareStepLowerEl) {
        SetSysReg(HV_SYS_REG_MDSCR_EL1, GetSysReg(HV_SYS_REG_MDSCR_EL1) & ~MDSCR_EL1_SS);

        return HaltReason::StepThread;
    }

    if (GetExceptionClass(syndrome) == ExceptionClass::TrappedWfiWfe) {
        // HCR_EL2.TWI/TWE belong to the framework and the guest's SCTLR_EL1 clears
        // nTWI/nTWE, so a guest WFI/WFE cannot be absorbed at EL1 and lands here.
        // ELR_EL1 points AT the trapping instruction, so retire it with the +4 or it
        // re-executes forever and every waiter it would signal parks in svc 0x1c.
        SetSysReg(HV_SYS_REG_ELR_EL1, GetSysReg(HV_SYS_REG_ELR_EL1) + 4);
        return std::nullopt;
    }

    if (GetExceptionClass(syndrome) != ExceptionClass::HvcAarch64) {
        static Throttle throttle;
        if (const u64 n = throttle.Tick()) {
            LOG_ERROR(Core_ARM,
                      "VM exit outside the guest's exception vectors, EC {:#x}, ESR {:#x}, "
                      "guest PC {:#x} ({} so far)",
                      static_cast<u32>(GetExceptionClass(syndrome)), syndrome,
                      GetSysReg(HV_SYS_REG_ELR_EL1), n);
        }
        // Not BreakLoop: physical_core treats that as benign and re-enters at the
        // same PC, so any undecoded exception becomes a silent 100%-CPU livelock.
        return HaltReason::PrefetchAbort;
    }

    const auto entry = static_cast<VectorEntry>(syndrome & 0xFFFF);

    if (IsCurrentEl(entry)) {
        LOG_ERROR(Core_ARM,
                  "Hypervisor EL1 fault on core {} through the {} vector: ESR {:#x}, ELR {:#x}, "
                  "FAR {:#x}, SPSR {:#x}",
                  m_core_index, VectorEntryName(entry), GetSysReg(HV_SYS_REG_ESR_EL1),
                  GetSysReg(HV_SYS_REG_ELR_EL1), GetSysReg(HV_SYS_REG_FAR_EL1),
                  GetSysReg(HV_SYS_REG_SPSR_EL1));
        return HaltReason::PrefetchAbort;
    }

    switch (entry) {
    case VectorEntry::LowerEl64Sync:
        return HandleGuestSynchronous();

    case VectorEntry::LowerEl64Irq:
    case VectorEntry::LowerEl64Fiq:
        LOG_WARNING(Core_ARM, "Unexpected guest {} on core {}", VectorEntryName(entry),
                    m_core_index);
        return std::nullopt;

    case VectorEntry::LowerEl64SError:
        LOG_ERROR(Core_ARM, "Guest SError on core {}, ESR {:#x}", m_core_index,
                  GetSysReg(HV_SYS_REG_ESR_EL1));
        return HaltReason::PrefetchAbort;

    default:
        LOG_ERROR(Core_ARM, "Guest took the {} vector on core {}, ELR {:#x}, SPSR {:#x}",
                  VectorEntryName(entry), m_core_index, GetSysReg(HV_SYS_REG_ELR_EL1),
                  GetSysReg(HV_SYS_REG_SPSR_EL1));
        return HaltReason::PrefetchAbort;
    }
}

std::optional<HaltReason> ArmHypervisor::HandleGuestSynchronous() {
    const u64 esr = GetSysReg(HV_SYS_REG_ESR_EL1);
    m_context.pc = GetSysReg(HV_SYS_REG_ELR_EL1);

    switch (GetExceptionClass(esr)) {
    case ExceptionClass::SvcAarch64:
        EnsureContextFresh();
        m_context.pc = GetSysReg(HV_SYS_REG_ELR_EL1);
        m_svc = static_cast<u32>(esr & 0xFFFF);
        m_svc_trail[m_svc_trail_next] = SvcTrail{m_svc, ~u64{0}, m_context.pc};
        m_svc_trail_next = (m_svc_trail_next + 1) % SVC_TRAIL;
        return HaltReason::SupervisorCall;

    case ExceptionClass::TrappedMsrMrsSystem:
        HandleSystemRegisterTrap(static_cast<u32>(esr));
        m_context.pc += 4;
        SetSysReg(HV_SYS_REG_ELR_EL1, m_context.pc);
        return std::nullopt;

    case ExceptionClass::DataAbortLowerEl:
        if (IsPermissionFault(esr)) {
            if (!IsWriteFault(esr)) {
                LOG_ERROR(Core_ARM, "Read permission fault at {:#x}, ESR {:#x}",
                          GetSysReg(HV_SYS_REG_FAR_EL1), esr);
                return HaltReason::PrefetchAbort;
            }
            if (HandleTrackedAccess(true)) {
                return std::nullopt;
            }
            return HaltReason::PrefetchAbort;
        }
        if (!IsTranslationFault(esr)) {
            static Throttle throttle;
            if (const u64 n = throttle.Tick()) {
                LOG_ERROR(Core_ARM, "Data abort at {:#x} is a {} (ESR {:#x}), not a missing "
                                    "mapping ({} so far)",
                          GetSysReg(HV_SYS_REG_FAR_EL1), DescribeFaultStatus(esr), esr, n);
            }
            return HaltReason::PrefetchAbort;
        }
        if (TryMapFaultingPage(IsWriteFault(esr))) {
            return std::nullopt;
        }
        return HaltReason::PrefetchAbort;

    case ExceptionClass::InstructionAbortLowerEl:
        if (!IsTranslationFault(esr)) {
            static Throttle throttle;
            if (const u64 n = throttle.Tick()) {
                LOG_ERROR(Core_ARM, "Instruction abort at {:#x} is a {} (ESR {:#x}), not a "
                                    "missing mapping ({} so far)",
                          GetSysReg(HV_SYS_REG_FAR_EL1), DescribeFaultStatus(esr), esr, n);
            }
            return HaltReason::PrefetchAbort;
        }
        if (TryMapFaultingPage(false)) {
            return std::nullopt;
        }
        return HaltReason::PrefetchAbort;

    case ExceptionClass::BrkAarch64:
    case ExceptionClass::BreakpointLowerEl:
        m_breakpoint_context = m_context;
        return HaltReason::InstructionBreakpoint;

    case ExceptionClass::SoftwareStepLowerEl:
        return HaltReason::StepThread;

    case ExceptionClass::WatchpointLowerEl:
        return HaltReason::DataAbort;

    default:
        LOG_ERROR(Core_ARM, "Unhandled guest exception, EC {:#x}, ESR {:#x}, PC {:#x}",
                  static_cast<u32>(GetExceptionClass(esr)), esr, m_context.pc);
        return HaltReason::BreakLoop;
    }
}

void ArmHypervisor::SetGuestReg(u32 index, u64 value) {
    if (index >= 31) {
        // XZR: architecturally the write is discarded.
        return;
    }
    SetReg(static_cast<hv_reg_t>(HV_REG_X0 + index), value);
    if (index < m_context.r.size()) {
        m_context.r[index] = value;
    } else if (index == 29) {
        m_context.fp = value;
    } else {
        m_context.lr = value;
    }
}

bool ArmHypervisor::ResolveGuestPage(u64 page_va, u64& out_ipa) {
    u8* const host = m_process->GetMemory().GetPointerSilent(Common::ProcessAddress{page_va});
    if (host == nullptr) {
        return false;
    }

    if (host < m_dram_backing || host >= m_dram_backing + m_dram_size) {
        return false;
    }
    out_ipa = DramMemoryMap::Base + static_cast<u64>(host - m_dram_backing);
    return true;
}

bool ArmHypervisor::EmulateFaultingInstruction() {
    std::array<u64, 31> regs{};
    for (size_t i = 0; i < regs.size(); ++i) {
        regs[i] = GetReg(static_cast<hv_reg_t>(HV_REG_X0 + i));
    }
    std::array<u128, 32> vregs{};
    for (size_t i = 0; i < vregs.size(); ++i) {
        hv_simd_fp_uchar16_t value{};
        if (hv_vcpu_get_simd_fp_reg(m_vcpu, static_cast<hv_simd_fp_reg_t>(HV_SIMD_FP_REG_Q0 + i),
                                    &value) != HV_SUCCESS) {
            return false;
        }
        std::memcpy(&vregs[i], &value, sizeof(value));
    }
    u64 sp = GetSysReg(HV_SYS_REG_SP_EL0);
    const u64 pc = GetSysReg(HV_SYS_REG_ELR_EL1);

    const std::optional<u64> next =
        MatchAndExecuteOneInstruction(m_process->GetMemory(), regs, vregs, sp, pc);
    if (!next) {
        static std::atomic<u32> reported{0};
        if (reported.fetch_add(1, std::memory_order_relaxed) < 8) {
            LOG_WARNING(Core_ARM, "Cannot emulate the store {:#010x} at {:#x}, downloading the "
                                  "page instead",
                        m_process->GetMemory().Read32(pc), pc);
        }
        return false;
    }
    for (size_t i = 0; i < regs.size(); ++i) {
        SetReg(static_cast<hv_reg_t>(HV_REG_X0 + i), regs[i]);
    }
    for (size_t i = 0; i < vregs.size(); ++i) {
        hv_simd_fp_uchar16_t value{};
        std::memcpy(&value, &vregs[i], sizeof(value));
        static_cast<void>(hv_vcpu_set_simd_fp_reg(
            m_vcpu, static_cast<hv_simd_fp_reg_t>(HV_SIMD_FP_REG_Q0 + i), value));
    }
    SetSysReg(HV_SYS_REG_SP_EL0, sp);
    SetSysReg(HV_SYS_REG_ELR_EL1, *next);
    return true;
}

bool ArmHypervisor::HandleTrackedAccess(bool is_write) {
    HvPageTable* const page_table = m_page_table.load(std::memory_order::acquire);
    if (page_table == nullptr) {
        return false;
    }

    const u64 fault_va = GetSysReg(HV_SYS_REG_FAR_EL1);
    const u64 page_va = Common::AlignDown(fault_va, HvPageTable::PAGE_SIZE);

    // Does what a JIT load or store of the page would to the GPU caches. A load keeps the
    // GPU's copy and leaves the page read-only, so the first store still comes here.
    auto& memory = m_process->GetMemory();
    using Core::Memory::NCEAccessFaultAction;
    NCEAccessFaultAction action = NCEAccessFaultAction::Retry;
    if (is_write) {
        action = memory.InvalidateNCE(Common::ProcessAddress{page_va}, HvPageTable::PAGE_SIZE);
        if (action == NCEAccessFaultAction::Instruction) {
            // The GPU still holds writes to this page. Invalidating all of it would drop
            // them, so only the bytes this store touches are written, as NCE does. The page
            // stays protected.
            if (EmulateFaultingInstruction()) {
                return true;
            }
            // Not one the interpreter knows. Bring the GPU's data down first, so dropping
            // the page below costs a download instead of the GPU's writes.
            static_cast<void>(memory.SynchronizeRasterizerMemoryForAccess(
                Common::ProcessAddress{page_va}, HvPageTable::PAGE_SIZE, false));
            action = memory.SynchronizeRasterizerMemoryForAccess(
                         Common::ProcessAddress{page_va}, HvPageTable::PAGE_SIZE, true)
                         ? NCEAccessFaultAction::Retry
                         : NCEAccessFaultAction::Unmapped;
        }
    } else if (!memory.SynchronizeRasterizerMemoryForAccess(Common::ProcessAddress{page_va},
                                                            HvPageTable::PAGE_SIZE, false)) {
        action = NCEAccessFaultAction::Unmapped;
    }
    if (action != NCEAccessFaultAction::Retry) {
        LOG_WARNING(Core_ARM, "Fault at {:#x} that the emulator could not resolve ({})", fault_va,
                    static_cast<int>(action));
        return false;
    }

    u64 ipa{};
    if (!ResolveGuestPage(page_va, ipa)) {
        return false;
    }
    // Taken before the descriptor changes, see guest_execution_lock.
    const GuestPause pause;
    if (!page_table->Map(page_va, ipa, HvPageTable::PAGE_SIZE,
                         is_write ? HvPermission::UserReadWriteExecute
                                  : HvPermission::UserReadExecute)) {
        return false;
    }
    asm volatile("dsb ish" ::: "memory");

    MarkFlushPendingEverywhere();
    // A write normally ends the tracking. When the page holds GPU-written data it stays cached
    // and the JIT would keep reporting stores to it, so the next upload has to protect it again.
    if (!is_write || memory.IsRasterizerCached(Common::ProcessAddress{page_va})) {
        std::scoped_lock lock{watched_mutex};
        if (watched_pages.insert(page_va).second) {
            ArmInterface::AddWatchedPages(1);
        }
    } else {
        ForgetWatched(page_va, page_va + HvPageTable::PAGE_SIZE);
    }
    return true;
}

void ArmHypervisor::OnGpuAccess(u64 addr, std::size_t size) {
    const u64 start = Common::AlignDown(addr, HvPageTable::PAGE_SIZE);
    const u64 end = Common::AlignUp(addr + size, HvPageTable::PAGE_SIZE);
    std::vector<u64> pages;
    {
        std::scoped_lock lock{watched_mutex};
        if (watched_pages.empty()) {
            return;
        }
        if ((end - start) / HvPageTable::PAGE_SIZE < watched_pages.size()) {
            for (u64 va = start; va < end; va += HvPageTable::PAGE_SIZE) {
                if (watched_pages.erase(va) != 0) {
                    pages.push_back(va);
                }
            }
        } else {
            for (auto it = watched_pages.begin(); it != watched_pages.end();) {
                if (*it >= start && *it < end) {
                    pages.push_back(*it);
                    it = watched_pages.erase(it);
                } else {
                    ++it;
                }
            }
        }
        if (pages.empty()) {
            return;
        }
        ArmInterface::AddWatchedPages(-static_cast<s64>(pages.size()));
    }
    HvPageTable* const page_table = m_page_table.load(std::memory_order::acquire);
    if (page_table == nullptr) {
        return;
    }
    const GuestPause pause;
    for (const u64 va : pages) {
        page_table->Unmap(va, HvPageTable::PAGE_SIZE);
    }
    asm volatile("dsb ish" ::: "memory");
    MarkFlushPendingEverywhere();
}

bool ArmHypervisor::TryMapFaultingPage(bool is_write) {
    HvPageTable* const page_table = m_page_table.load(std::memory_order::acquire);
    if (page_table == nullptr) {
        return false;
    }

    const u64 fault_va = GetSysReg(HV_SYS_REG_FAR_EL1);
    const u64 page_va = Common::AlignDown(fault_va, HvPageTable::PAGE_SIZE);

    u64 ipa{};
    if (!ResolveGuestPage(page_va, ipa)) {
        static Throttle throttle;
        if (const u64 n = throttle.Tick()) {
            LOG_WARNING(Core_ARM, "Guest address {:#x} is not mapped by the emulator ({} so far)",
                        fault_va, n);

        }
        return false;
    }

    // A fault maps this much around the faulting page to amortise the exit. Tried
    // it at PAGE_SIZE (only the faulting page, never repoint a live descriptor)
    // against the SMM2 fault and saw no difference, so the widening is clear and
    // worth keeping.
    constexpr u64 WINDOW = 64ull * 1024;
    const u64 window_start = Common::AlignDown(page_va, WINDOW);
    const u64 window_end = window_start + WINDOW;

    auto& memory = m_process->GetMemory();
    const bool tracked =
        TRACK_GPU_WRITES && memory.IsRasterizerCached(Common::ProcessAddress{page_va});
    if (tracked && Settings::values.use_reactive_flushing.GetValue()) {
        // A read can need what the GPU wrote here, and mapping it straight away lets it
        // through without the download.
        return HandleTrackedAccess(is_write);
    }
    if (tracked) {
        bool replaced_live = false;
        // Unconditional: replaced_live only reports a repointed descriptor, and
        // narrowing permissions needs the shootdown just as much.
        const GuestPause pause;
        if (!page_table->Map(page_va, ipa, HvPageTable::PAGE_SIZE, HvPermission::UserReadExecute,
                             std::addressof(replaced_live))) {
            return false;
        }
        asm volatile("dsb ish" ::: "memory");
        MarkFlushPendingEverywhere();
        return true;
    }

    const auto is_contiguous_with_anchor = [&](u64 va) {
        u64 candidate{};
        return ResolveGuestPage(va, candidate) && candidate == ipa + (va - page_va) &&
               !(TRACK_GPU_WRITES && memory.IsRasterizerCached(Common::ProcessAddress{va}));
    };

    u64 first = page_va;
    while (first > window_start && is_contiguous_with_anchor(first - HvPageTable::PAGE_SIZE)) {
        first -= HvPageTable::PAGE_SIZE;
    }
    u64 end = page_va + HvPageTable::PAGE_SIZE;
    while (end < window_end && is_contiguous_with_anchor(end)) {
        end += HvPageTable::PAGE_SIZE;
    }

    // Permissive on purpose, see HvPermission::UserReadWriteExecute.
    bool replaced_live = false;
    // One pause for the window map and the downgrade pass below. Releasing between
    // them lets a core resume against the wide read-write window.
    const GuestPause pause;
    if (!page_table->Map(first, ipa - (page_va - first), end - first,
                         HvPermission::UserReadWriteExecute, std::addressof(replaced_live))) {
        return false;
    }

    asm volatile("dsb ish" ::: "memory");

    if (TRACK_GPU_WRITES) {
        for (u64 va = first; va < end; va += HvPageTable::PAGE_SIZE) {
            if (!memory.IsRasterizerCached(Common::ProcessAddress{va})) {
                continue;
            }
            page_table->Map(va, ipa + (va - page_va), HvPageTable::PAGE_SIZE,
                            HvPermission::UserReadExecute);
        }
        asm volatile("dsb ish" ::: "memory");
    }

    // The window extension repoints up to fifteen descriptors the guest never
    // faulted on, so we owe the shootdown whether or not replaced_live fired.
    MarkFlushPendingEverywhere();
    if (replaced_live) {
        static Throttle throttle;
        if (const u64 n = throttle.Tick()) {
            LOG_WARNING(Core_ARM, "Demand mapping repointed a live descriptor near {:#x} ({} so far)",
                        page_va, n);
        }
    }
    return true;
}

void ArmHypervisor::DumpSvcTrail() const {
    std::string trail;
    for (size_t i = 0; i < SVC_TRAIL; i++) {
        const SvcTrail& entry = m_svc_trail[(m_svc_trail_next + i) % SVC_TRAIL];
        if (entry.pc == 0) {
            continue;
        }
        trail += entry.result == ~u64{0}
                     ? std::format("svc{:#x}@{:#x}=<no return> ", entry.number, entry.pc)
                     : std::format("svc{:#x}@{:#x}={:#x} ", entry.number, entry.pc, entry.result);
    }
    LOG_ERROR(Core_ARM, "  last supervisor calls on core {}, oldest first: {}", m_core_index,
              trail);
}

void ArmHypervisor::HandleSystemRegisterTrap(u32 esr) {
    const bool is_read = (esr & 1) != 0;
    const u32 rt = (esr >> 5) & 0x1F;

    if (!is_read) {
        LOG_WARNING(Core_ARM, "Ignoring guest system register write, ESR {:#x}", esr);
        return;
    }

    const auto answer = [&](u64 value) { SetGuestReg(rt, value); };

    // Op0/Op2/Op1/CRn/CRm, the fields that identify the register.
    switch ((esr >> 1) & 0x1FFE0F) {
    case 0b11'000'011'1110'00000'0000: // CNTFRQ_EL0
        answer(Hardware::CNTFREQ);
        break;
    case 0b11'010'011'1110'00000'0000: // CNTVCT_EL0
        // Both answered from the emulator's clock instead of the host's.
        [[fallthrough]];
    case 0b11'001'011'1110'00000'0000: // CNTPCT_EL0
        answer(m_system.CoreTiming().GetClockTicks());
        break;
    case 0b11'001'011'0000'00000'0000: // CTR_EL0
        answer(CTR_EL0_VALUE);
        break;
    default:
        {
            // Rate limited: a register the guest polls is read once per check.
            static Throttle throttle;
            if (const u64 n = throttle.Tick()) {
                LOG_WARNING(Core_ARM, "Unhandled guest system register read, ESR {:#x} ({} so far)",
                            esr, n);
            }
        }
        answer(0);
        break;
    }
}

u32 ArmHypervisor::GetSvcNumber() const {
    return m_svc;
}

void ArmHypervisor::GetSvcArguments(std::span<uint64_t, 8> args) const {
    // From the cache, not the vCPU: this works from any thread, where
    // hv_vcpu_get_reg returns nothing and we would dispatch on stale args.
    std::scoped_lock context_lock{m_context_mutex};
    for (size_t i = 0; i < 8; i++) {
        args[i] = m_context.r[i];
    }
}

void ArmHypervisor::SetSvcArguments(std::span<const uint64_t, 8> args) {
    // Syscall return values, read by the guest on resume. This used to race
    // StoreContextFromVcpu and drop the result.
    std::scoped_lock context_lock{m_context_mutex};
    m_svc_trail[(m_svc_trail_next + SVC_TRAIL - 1) % SVC_TRAIL].result = args[0];
    for (size_t i = 0; i < 8; i++) {
        m_context.r[i] = args[i];
    }
    if (OnOwnerThread()) {
        for (size_t i = 0; i < 8; i++) {
            SetReg(static_cast<hv_reg_t>(HV_REG_X0 + i), args[i]);
        }
    } else {
        m_context_dirty.store(true, std::memory_order::release);
    }
}

void ArmHypervisor::StoreContextFromVcpu(Kernel::Svc::ThreadContext& ctx) const {
    if (!m_vcpu_created.load(std::memory_order::acquire)) {
        return;
    }
    for (size_t i = 0; i < 29; i++) {
        ctx.r[i] = GetReg(static_cast<hv_reg_t>(HV_REG_X0 + i));
    }
    ctx.fp = GetReg(HV_REG_FP);
    ctx.lr = GetReg(HV_REG_LR);
    ctx.sp = GetSysReg(HV_SYS_REG_SP_EL0);
    ctx.pc = GetSysReg(HV_SYS_REG_ELR_EL1);
    ctx.pstate = static_cast<u32>(GetSysReg(HV_SYS_REG_SPSR_EL1));
    for (size_t i = 0; i < 32; i++) {
        hv_simd_fp_uchar16_t value{};
        const hv_return_t result = hv_vcpu_get_simd_fp_reg(
            m_vcpu, static_cast<hv_simd_fp_reg_t>(HV_SIMD_FP_REG_Q0 + i), &value);
        ReportIfFailed("hv_vcpu_get_simd_fp_reg", static_cast<u32>(i),
                       static_cast<u32>(result));
        if (result != HV_SUCCESS) {
            continue;
        }
        std::memcpy(&ctx.v[i], &value, sizeof(value));
    }
    ctx.fpcr = static_cast<u32>(GetReg(HV_REG_FPCR));
    ctx.fpsr = static_cast<u32>(GetReg(HV_REG_FPSR));
    ctx.tpidr = GetSysReg(HV_SYS_REG_TPIDR_EL0);
}

void ArmHypervisor::LoadContextToVcpu(const Kernel::Svc::ThreadContext& ctx) {
    if (!m_vcpu_created.load(std::memory_order::acquire)) {
        return;
    }
    for (size_t i = 0; i < 29; i++) {
        SetReg(static_cast<hv_reg_t>(HV_REG_X0 + i), ctx.r[i]);
    }
    SetReg(HV_REG_FP, ctx.fp);
    SetReg(HV_REG_LR, ctx.lr);
    SetSysReg(HV_SYS_REG_SP_EL0, ctx.sp);
    SetSysReg(HV_SYS_REG_ELR_EL1, ctx.pc);
    SetSysReg(HV_SYS_REG_SPSR_EL1, static_cast<u64>(ctx.pstate) & ~SPSR_MODE_MASK);
    SetReg(HV_REG_CPSR, CPSR_EL1H);
    SetReg(HV_REG_PC, TRAMPOLINE_VA);
    for (size_t i = 0; i < 32; i++) {
        hv_simd_fp_uchar16_t value{};
        std::memcpy(&value, &ctx.v[i], sizeof(value));
        ReportIfFailed("hv_vcpu_set_simd_fp_reg", static_cast<u32>(i),
                       static_cast<u32>(hv_vcpu_set_simd_fp_reg(
                           m_vcpu, static_cast<hv_simd_fp_reg_t>(HV_SIMD_FP_REG_Q0 + i), value)));
    }
    SetReg(HV_REG_FPCR, ctx.fpcr);
    SetReg(HV_REG_FPSR, ctx.fpsr);
    SetSysReg(HV_SYS_REG_TPIDR_EL0, ctx.tpidr);
}

void ArmHypervisor::GetContext(Kernel::Svc::ThreadContext& ctx) const {
    std::scoped_lock context_lock{m_context_mutex};
    // The cache is only current as of the last exit that returned a HaltReason;
    // demand faults, WFI and sysreg traps are handled internally and do not refresh
    // it, so on the owner thread read the live registers instead.
    if (!m_context_fresh && OnOwnerThread()) {
        StoreContextFromVcpu(m_context);
        m_context_fresh = true;
    }
    ctx = m_context;
}

void ArmHypervisor::SetContext(const Kernel::Svc::ThreadContext& ctx) {
    std::scoped_lock context_lock{m_context_mutex};
    if (std::memcmp(std::addressof(ctx), std::addressof(m_context), sizeof(ctx)) == 0 &&
        !m_context_dirty.load(std::memory_order::acquire)) {
        return;
    }

    m_context = ctx;
    if (OnOwnerThread()) {
        m_context_dirty.exchange(false, std::memory_order::acquire);
        LoadContextToVcpu(ctx);
        SetSysReg(HV_SYS_REG_TPIDRRO_EL0, m_tpidrro_el0);
    } else {
        m_context_dirty.store(true, std::memory_order::release);
    }
}

void ArmHypervisor::SetTpidrroEl0(u64 value) {
    std::scoped_lock context_lock{m_context_mutex};
    m_tpidrro_el0 = value;
    if (OnOwnerThread()) {
        SetSysReg(HV_SYS_REG_TPIDRRO_EL0, value);
    } else {
        m_context_dirty.store(true, std::memory_order::release);
    }
}

void ArmHypervisor::SignalInterrupt(Kernel::KThread* thread) {
    m_interrupted.store(true);
    if (m_vcpu_created.load(std::memory_order::acquire)) {
        hv_vcpus_exit(&m_vcpu, 1);
    }
}

void ArmHypervisor::ClearInstructionCache() {
    // Pause as well as flag: the trampoline that does the IC maintenance only runs
    // on the next entry.
    const GuestPause pause;
    MarkFlushPendingEverywhere();
}

void ArmHypervisor::InvalidateCacheRange(u64 addr, std::size_t size) {
    DropMapping(addr, size, FlushAlways::Yes);
}

void ArmHypervisor::InvalidateMapping(u64 addr, std::size_t size) {
    DropMapping(addr, size, FlushAlways::No);
}

void ArmHypervisor::DropMapping(u64 addr, std::size_t size, FlushAlways always) {
    HvPageTable* const page_table = m_page_table.load(std::memory_order::acquire);
    if (page_table == nullptr) {
        return;
    }

    const u64 start = Common::AlignDown(addr, HvPageTable::PAGE_SIZE);
    const u64 end = Common::AlignUp(addr + size, HvPageTable::PAGE_SIZE);
    ForgetWatched(start, end);
    // The GPU thread hits this path as well as the kernel, and a core that still
    // has the mapping cached would store into a page nothing will ever read again.
    const GuestPause pause;
    if (!page_table->Unmap(start, end - start) && always == FlushAlways::No) {
        return;
    }

    asm volatile("dsb ish" ::: "memory");
    MarkFlushPendingEverywhere();
}

void ArmHypervisor::RewindBreakpointInstruction() {
    m_context = m_breakpoint_context;
    m_context_dirty.store(true, std::memory_order::release);
}

} // namespace Core
