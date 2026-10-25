// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <memory>

#include <boost/context/detail/fcontext.hpp>

#include "core/arm/arm_interface.h"
#include "core/arm/scarletnx/scarletnx_api.h"
#include "core/arm/scarletnx/scarletnx_process.h"

namespace Core {

/// Runs A64 code recompiled by ScarletNX.
///
/// A recompiled guest function is a host function that returns when the guest function returns,
/// so guest calls nest on the host stack and an SVC cannot return from the generated code. Guest
/// code therefore runs on a coroutine of this core, which the SVC and interrupt hooks suspend.
/// When the kernel next runs the same thread with the same context, the coroutine resumes;
/// otherwise its host frames are dropped and execution restarts at the new pc through dispatch.
class ArmScarletNx64 final : public ArmInterface {
public:
    ArmScarletNx64(std::shared_ptr<ScarletNxProcess> process, std::size_t core_index);
    ~ArmScarletNx64() override;

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

protected:
    const Kernel::DebugWatchpoint* HaltedWatchpoint() const override {
        return nullptr;
    }

    void RewindBreakpointInstruction() override {}

private:
    static ArmScarletNx64& From(CPU* cpu) {
        return *static_cast<ArmScarletNx64*>(cpu->thread);
    }
    static const RtAPI* Runtime();

    // Runtime hooks called by the generated code.
    static void Svc(CPU* cpu, u32 imm);
    static void Undefined(CPU* cpu, u64 pc, u32 word);
    static void Breakpoint(CPU* cpu, u64 pc, u32 imm);
    static void Poll(CPU* cpu);
    static void Miss(CPU* cpu, u64 pc);
    static void CodeChanged(CPU* cpu, u64 addr);

    static void Entry(boost::context::detail::transfer_t transfer);
    [[noreturn]] void Execute();
    void Yield(HaltReason reason, bool resumable, bool in_svc);
    void ResetGuest();
    void LoadCpu();
    void SaveCpu();

    std::shared_ptr<ScarletNxProcess> m_process;
    std::size_t m_core_index{};

    // The context the kernel reads and writes; m_cpu holds it while guest code runs.
    Kernel::Svc::ThreadContext m_ctx{};
    u64 m_tpidrro{};
    CPU m_cpu{};

    HaltReason m_halt{};
    u32 m_svc{};
    std::atomic<bool> m_interrupt{};

    // The coroutine, and what it was suspended in, when it can be resumed.
    boost::context::detail::fcontext_t m_guest{};
    boost::context::detail::fcontext_t m_host{};
    u8* m_stack{};
    bool m_resumable{};
    bool m_suspended_in_svc{};
    u64 m_suspended_thread_id{};
    Kernel::Svc::ThreadContext m_suspended_ctx{};

    ScarletNxFaultScope m_fault_scope{};
};

} // namespace Core
