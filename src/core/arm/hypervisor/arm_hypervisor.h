// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <thread>

#include <Hypervisor/Hypervisor.h>

#include "core/arm/arm_interface.h"
#include "core/hle/kernel/svc_types.h"

namespace Core {
void DetachAllPageTables();

class System;
class HvMachine;
class HvPageTable;

class ArmHypervisor final : public ArmInterface {
public:
    ArmHypervisor(System& system, bool uses_wall_clock, Kernel::KProcess* process,
                  std::size_t core_index);
    ~ArmHypervisor() override;

    [[nodiscard]] static bool IsAvailable();

    void Initialize() override;

    Architecture GetArchitecture() const override {
        return Architecture::AArch64;
    }

    HaltReason RunThread(Kernel::KThread* thread) override;
    HaltReason StepThread(Kernel::KThread* thread) override;

    void GetContext(Kernel::Svc::ThreadContext& ctx) const override;
    void SetContext(const Kernel::Svc::ThreadContext& ctx) override;
    void SetTpidrroEl0(u64 value) override;

    void GetSvcArguments(std::span<uint64_t, 8> args) const override;
    void SetSvcArguments(std::span<const uint64_t, 8> args) override;
    u32 GetSvcNumber() const override;

    void SignalInterrupt(Kernel::KThread* thread) override;
    void ClearInstructionCache() override;
    void InvalidateCacheRange(u64 addr, std::size_t size) override;
    void InvalidateMapping(u64 addr, std::size_t size) override;
    void OnGpuAccess(u64 addr, std::size_t size) override;

protected:
    const Kernel::DebugWatchpoint* HaltedWatchpoint() const override {
        return m_halted_watchpoint;
    }

    void RewindBreakpointInstruction() override;

private:
    [[nodiscard]] bool EnsureVcpu();

    HaltReason Run(bool stepping);

    [[nodiscard]] bool ParkAtTrampoline();

    void LoadContextToVcpu(const Kernel::Svc::ThreadContext& ctx);
    void StoreContextFromVcpu(Kernel::Svc::ThreadContext& ctx) const;

    std::optional<HaltReason> HandleExit();

    HaltReason ConsumePendingVectorOr(HaltReason fallback);

    std::optional<HaltReason> HandleGuestSynchronous();

    void SetGuestReg(u32 index, u64 value);

    [[nodiscard]] bool TryMapFaultingPage(bool is_write);

    [[nodiscard]] bool HandleTrackedAccess(bool is_write);
    [[nodiscard]] bool EmulateFaultingInstruction();

    [[nodiscard]] bool ResolveGuestPage(u64 page_va, u64& out_ipa);

    void HandleSystemRegisterTrap(u32 esr);

    enum class FlushAlways { No, Yes };

    // Kicks every vCPU out of the guest and keeps them out while it lives.
    // Construct this before touching any page table descriptor.
    class GuestPause {
    public:
        GuestPause();

    private:
        std::unique_lock<std::shared_mutex> lock;
    };

    // Marks every core on this page table to run the flush trampoline on its next
    // entry. Call with a GuestPause held.
    void MarkFlushPendingEverywhere();

    void RegisterBackend();
    void UnregisterBackend();

    void DropMapping(u64 addr, std::size_t size, FlushAlways always);

    void ArmVirtualTimer();

    // Kicks whoever is in the guest so it re-arms its vtimer short.
    static void ShortenRunningSlice();

    void EnsureContextFresh();

    [[nodiscard]] bool OnOwnerThread() const;

    void ReportIfFailed(const char* call, u32 reg, u32 result) const;

    [[nodiscard]] u64 GetReg(hv_reg_t reg) const;
    void SetReg(hv_reg_t reg, u64 value);
    [[nodiscard]] u64 GetSysReg(hv_sys_reg_t reg) const;
    void SetSysReg(hv_sys_reg_t reg, u64 value);

    System& m_system;
    Kernel::KProcess* m_process{};
    std::size_t m_core_index{};

    std::atomic<HvPageTable*> m_page_table{};

public:
    void DetachPageTable() {
        m_page_table.store(nullptr, std::memory_order::release);
    }

private:

    hv_vcpu_t m_vcpu{};
    hv_vcpu_exit_t* m_exit{};
    std::atomic<bool> m_vcpu_created{false};
    bool m_vcpu_failed{};
    std::thread::id m_owner_thread{};

    // Guards m_context, m_context_fresh and m_tpidrro_el0. The context is ~800
    // bytes and both the owning core and foreign threads write it, the dirty flag
    // alone never made those writes atomic against each other.
    mutable std::mutex m_context_mutex;
    mutable Kernel::Svc::ThreadContext m_context{};

    u32 m_svc{};

    struct SvcTrail {
        u32 number;
        u64 result;
        u64 pc;
    };
    static constexpr size_t SVC_TRAIL = 24;
    std::array<SvcTrail, SVC_TRAIL> m_svc_trail{};
    size_t m_svc_trail_next{};
    void DumpSvcTrail() const;

    u64 m_tpidrro_el0{};

    mutable bool m_context_fresh{true};

    std::atomic<bool> m_context_dirty{};

    const Kernel::DebugWatchpoint* m_halted_watchpoint{};
    Kernel::Svc::ThreadContext m_breakpoint_context{};

    u32 m_pending_vector{};
    bool m_pending_vector_valid{};

    std::atomic<bool> m_interrupted{false};
    std::atomic<bool> m_in_guest{false};
    std::atomic<bool> m_flush_pending{false};
    // Whether the entry we just made went through the flush trampoline. Owner
    // thread only so no atomics needed. We consume the flag inside the lock but
    // check whether the flush ran after releasing it.
    bool m_flushing_this_entry{false};

    const u8* m_dram_backing{};
    u64 m_dram_size{};
};

} // namespace Core
