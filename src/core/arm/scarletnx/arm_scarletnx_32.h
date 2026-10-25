// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <memory>

#include "core/arm/arm_interface.h"
#include "core/arm/scarletnx/scarletnx_api.h"
#include "core/arm/scarletnx/scarletnx_process.h"

namespace Core {

/// Runs A32 and Thumb code through ScarletNX's block compiler. Blocks return to the run loop, so
/// an SVC ends the run with r15 past it. The block cache is not thread-safe, so each core has its
/// own, rebuilt when the process' code changes.
class ArmScarletNx32 final : public ArmInterface {
public:
    ArmScarletNx32(std::shared_ptr<ScarletNxProcess> process, std::size_t core_index);
    ~ArmScarletNx32() override;

    Architecture GetArchitecture() const override {
        return Architecture::AArch32;
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
    // The CPU first, so the coprocessor hook can find its interface.
    struct Cpu32 {
        SnxArmCPU cpu;
        ArmScarletNx32* parent;
    };

    static int Svc(void* context, u32 imm, u64 number, const u64 arguments[6]);
    static void Coprocessor(SnxArmCPU* cpu, u32 word);
    HaltReason Halt(int exit);
    void LoadCpu();
    void SaveCpu();

    std::shared_ptr<ScarletNxProcess> m_process;
    std::size_t m_core_index{};

    snx_recompiled32* m_module{};
    u64 m_code_generation{~u64{}};

    // The context the kernel reads and writes; m_cpu holds it while guest code runs.
    Kernel::Svc::ThreadContext m_ctx{};
    u32 m_tpidrro{};
    Cpu32 m_cpu{};
    SnxArmRt m_rt{};

    bool m_in_svc{};
    u32 m_svc{};
    std::atomic<bool> m_interrupt{};

    ScarletNxFaultScope m_fault_scope{};
};

} // namespace Core
