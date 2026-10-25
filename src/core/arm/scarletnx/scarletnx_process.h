// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <csetjmp>
#include <memory>
#include <mutex>
#include <vector>

#include "common/common_types.h"

struct CPU;
struct snx_recompiler;
struct snx_recompiled;
struct snx_recompiled32;

namespace Kernel {
class KProcess;
}

namespace Core::Memory {
class Memory;
}

namespace Core {

class ArmInterface;
class System;

class ScarletNxProcess {
public:
    using SnxGuestFn = void (*)(CPU*, u64);

    // DeviceMemory reserves 39 bits of address space for the fastmem arena.
    static constexpr u64 ArenaSize = 1ULL << 39;

    struct CodeRange {
        u64 addr;
        u64 size;
    };

    ScarletNxProcess(System& system, Kernel::KProcess& process);
    ~ScarletNxProcess();

    static bool ShouldUse(System& system, const Kernel::KProcess& process);
    SnxGuestFn Lookup(u64 pc);
    snx_recompiled* FindModule(u64 pc);
    snx_recompiled32* MakeModule32();

    void InvalidateCacheRange(u64 addr, u64 size);
    void ClearInstructionCache();

    /// @brief general gate for when we are about to invalidate a given block aka. trigger regen
    u64 CodeGeneration() const {
        return m_code_generation.load(std::memory_order_acquire);
    }

    struct Module {
        std::vector<CodeRange> ranges;
        u64 lo;
        u64 hi;
        snx_recompiled* mod;
        bool retired;
    };

    std::vector<CodeRange> GetCodeRanges() const;
    const Module* FindModuleLocked(u64 pc) const;
    snx_recompiled* RecompileRegionLocked(u64 pc);

    std::vector<Module> m_modules;
    std::atomic<u64> m_code_generation{};

    // TODO: remove parent ref?
    System& m_system;
    Kernel::KProcess& m_process;
    u8* m_arena{};
    snx_recompiler* m_recompiler{};

    // ScarletNX's recompiler is used by one thread at a time.
    std::mutex m_mutex;
};

/// Recovers from host faults in guest memory on the thread running guest code: a fault on a
/// page the rasterizer protects is resolved and retried, any other jumps to env.
struct ScarletNxFaultScope {
    sigjmp_buf env;
    Memory::Memory* memory{};
    u8* arena{};
    u64 arena_size{};
    u64 last_fault{~0ull};
    u64 fault_address{};
};

void InstallScarletNxFaultHandler();
void SetScarletNxFaultScope(ScarletNxFaultScope* scope);

/// CNTPCT_EL0 of the emulated system.
u64 ScarletNxTicks();

std::unique_ptr<ArmInterface> MakeScarletNxInterface(std::shared_ptr<ScarletNxProcess> process, bool is_64bit, std::size_t core_index);

} // namespace Core
