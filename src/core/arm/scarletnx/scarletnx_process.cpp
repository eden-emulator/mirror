// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <mutex>

#include <signal.h>

#include "common/fs/path_util.h"
#include "common/logging.h"
#include "common/settings.h"
#include "core/arm/scarletnx/arm_scarletnx_32.h"
#include "core/arm/scarletnx/arm_scarletnx_64.h"
#include "core/arm/scarletnx/scarletnx_api.h"
#include "core/arm/scarletnx/scarletnx_process.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/device_memory.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/kernel.h"
#include "core/memory.h"

namespace Core {

namespace {

// ScarletNX refuses dispatch tables spanning more than 1 GiB of guest code.
constexpr u64 MaxModuleSpan = 1ULL << 30;
constexpr u64 A32Limit = 1ULL << 32;

std::atomic<Timing::CoreTiming*> g_core_timing{};

// Needs be TLS because SNX expects like, more than 4 threads (host == guest threads)
// and well -- quite impervious to "emulated" threads... maybe one day this can be removed
thread_local ScarletNxFaultScope* g_fault_scope{};
static struct {
    struct sigaction old_sigsegv;
    struct sigaction old_sigbus;
    std::once_flag installed;
} g_snx_data;

bool Overlaps(const ScarletNxProcess::CodeRange& range, u64 addr, u64 size) {
    return addr < range.addr + range.size && range.addr < addr + size;
}

void ChainSignal(int sig, siginfo_t* info, void* raw_context) {
    const struct sigaction& old = sig == SIGSEGV ? g_snx_data.old_sigsegv : g_snx_data.old_sigbus;
    if (old.sa_flags & SA_SIGINFO) {
        old.sa_sigaction(sig, info, raw_context);
    } else if (old.sa_handler == SIG_DFL) {
        // The faulting instruction runs again and takes the default action.
        signal(sig, SIG_DFL);
    } else if (old.sa_handler != SIG_IGN) {
        old.sa_handler(sig);
    }
}

void FaultHandler(int sig, siginfo_t* info, void* raw_context) {
    ScarletNxFaultScope* const scope = g_fault_scope;
    u8* const addr = static_cast<u8*>(info->si_addr);
    if (!scope || addr < scope->arena || addr >= scope->arena + scope->arena_size) {
        ChainSignal(sig, info, raw_context);
        return;
    }

    // Pages the rasterizer protects fault on access: invalidate them and retry. A second fault at
    // the same addr means retrying will not help.
    const u64 guest_address = u64(addr - scope->arena);
    if (guest_address != scope->last_fault) {
        scope->last_fault = guest_address;
        if (scope->memory->InvalidateNCE(guest_address & ~Memory::YUZU_PAGEMASK, Memory::YUZU_PAGESIZE)) {
            return;
        }
    }
    scope->fault_address = guest_address;
    siglongjmp(scope->env, 1);
}

} // namespace

void InstallScarletNxFaultHandler() {
    std::call_once(g_snx_data.installed, [] {
        struct sigaction action {};
        action.sa_flags = SA_SIGINFO;
        action.sa_sigaction = &FaultHandler;
        sigemptyset(&action.sa_mask);
        sigaction(SIGSEGV, &action, &g_snx_data.old_sigsegv);
        sigaction(SIGBUS, &action, &g_snx_data.old_sigbus);
    });
}

void SetScarletNxFaultScope(ScarletNxFaultScope* scope) {
    g_fault_scope = scope;
}

u64 ScarletNxTicks() {
    return g_core_timing.load(std::memory_order_relaxed)->GetClockTicks();
}

std::unique_ptr<ArmInterface> MakeScarletNxInterface(std::shared_ptr<ScarletNxProcess> process, bool is_64bit, std::size_t core_index) {
    if (is_64bit)
        return std::make_unique<ArmScarletNx64>(std::move(process), core_index);
    return std::make_unique<ArmScarletNx32>(std::move(process), core_index);
}

ScarletNxProcess::ScarletNxProcess(System& system, Kernel::KProcess& process)
    : m_system{system}
    , m_process{process}
    , m_arena{system.DeviceMemory().buffer.VirtualBasePointer()}
{
    InstallScarletNxFaultHandler();
    g_core_timing.store(&system.CoreTiming(), std::memory_order_relaxed);

    m_recompiler = snx_recompiler_new(SNX_RECOMPILER_FLAG_LLVM);
    ASSERT(m_recompiler && "cannot create the recompiler");

    // Linked modules are kept by a hash of their code, so later launches skip the recompile.
    const auto cache = Common::FS::GetEdenPath(Common::FS::EdenPath::CacheDir) / "scarletnx";
    if (snx_recompiler_set_cache(m_recompiler, Common::FS::PathToUTF8String(cache).c_str()) != 0)
        LOG_WARNING(Core_ARM, "no code cache: {}", snx_recompiler_error(m_recompiler));
}

ScarletNxProcess::~ScarletNxProcess() {
    for (auto& mod : m_modules)
        snx_recompiled_free(mod.mod);
    snx_recompiler_free(m_recompiler);
}

bool ScarletNxProcess::ShouldUse(System& system, const Kernel::KProcess& process) {
    if (Settings::values.cpu_backend.GetValue() != Settings::CpuBackend::ScarletNx)
        return false;
    auto const f = [](auto& s) {
        LOG_WARNING(Core_ARM, "snx: {}", s);
        return false;
    };
    if (!process.IsApplication())
        return f("no qlaunch yet");
    if (!system.Kernel().IsMulticore())
        return f("single core mode needs cycle counting");
    if (system.DebuggerEnabled())
        return f("the debugger needs stepping and watchpoints");
    /// ScarletNX expects flat memory, so it will only work with fastmem
    if (!Settings::IsFastmemEnabled() || !system.DeviceMemory().buffer.VirtualBasePointer())
        return f("guest memory must be mapped by fastmem");
    if (snx_recompiler_abi() != SNX_ABI)
        return f("the library was built with a different ABI");
    if (!snx_recompiler_has(SNX_RECOMPILER_FLAG_LLVM))
        return f("LLVM cannot target this host");
    return true;
}

std::vector<ScarletNxProcess::CodeRange> ScarletNxProcess::GetCodeRanges() const {
    std::vector<CodeRange> r;
    const auto& page_table = m_process.GetPageTable().GetBasePageTable();
    u64 addr = GetInteger(page_table.GetAddressSpaceStart());
    while (true) {
        Kernel::KMemoryInfo info;
        Kernel::Svc::PageInfo page_info;
        if (page_table.QueryInfo(&info, &page_info, addr).IsError())
            break;
        if (True(info.GetPermission() & Kernel::KMemoryPermission::UserExecute)) {
            if (!r.empty() && r.back().addr + r.back().size == info.GetAddress()) {
                r.back().size += info.GetSize();
            } else {
                r.push_back({info.GetAddress(), info.GetSize()});
            }
        }
        // The block past the addr space ends at 0.
        if (info.GetEndAddress() <= addr)
            break;
        addr = info.GetEndAddress();
    }
    return r;
}

const ScarletNxProcess::Module* ScarletNxProcess::FindModuleLocked(u64 pc) const {
    for (const auto& mod : m_modules) {
        if (mod.retired || pc < mod.lo || pc >= mod.hi)
            continue;
        for (const auto& range : mod.ranges)
            if (pc >= range.addr && pc < range.addr + range.size)
                return &mod;
    }
    return nullptr;
}

snx_recompiled* ScarletNxProcess::RecompileRegionLocked(u64 pc) {
    const auto r = GetCodeRanges();
    const auto it = std::ranges::find_if(r, [pc](const CodeRange& range) {
        return pc >= range.addr && pc < range.addr + range.size;
    });
    if (it == r.end())
        return nullptr;

    const auto is_covered = [this](const CodeRange& range) {
        return std::ranges::any_of(m_modules, [&](const Module& mod) {
            return !mod.retired && std::ranges::any_of(mod.ranges, [&](const CodeRange& cr) {
                return Overlaps(cr, range.addr, range.size);
            });
        });
    };

    // Recompile as many neighbouring regions as one dispatch table can cover.
    auto first = size_t(it - r.begin());
    auto last = first;
    const auto end_of = [&](size_t i) { return r[i].addr + r[i].size; };
    while (first > 0 && !is_covered(r[first - 1]) && end_of(last) - r[first - 1].addr <= MaxModuleSpan)
        --first;
    while (last + 1 < r.size() && !is_covered(r[last + 1])
    && end_of(last + 1) - r[first].addr <= MaxModuleSpan)
        ++last;

    Module mod{};
    mod.ranges.assign(r.begin() + first, r.begin() + last + 1);
    mod.lo = r[first].addr;
    mod.hi = end_of(last);

    std::vector<snx_code_range> code;
    u64 code_size = 0;
    for (const auto& range : mod.ranges) {
        code.push_back({range.addr, m_arena + range.addr, range.size});
        code_size += range.size;
    }

    LOG_INFO(Core_ARM, "recompiling {} KiB of code at {:#x}-{:#x}", code_size / 1024, mod.lo, mod.hi);
    mod.mod = snx_recompile_ranges(m_recompiler, code.data(), code.size(), &pc, 1);
    if (mod.mod) {
        LOG_INFO(Core_ARM, "recompiled {} functions", snx_recompiled_functions(mod.mod));
    } else {
        // Kept, so the region is not recompiled again on every miss.
        LOG_ERROR(Core_ARM, "recompilation failed: {}", snx_recompiler_error(m_recompiler));
    }

    m_modules.push_back(std::move(mod));
    return m_modules.back().mod;
}

ScarletNxProcess::SnxGuestFn ScarletNxProcess::Lookup(u64 pc) {
    snx_recompiled* mod;
    {
        std::scoped_lock lk{m_mutex};
        const Module* const known = FindModuleLocked(pc);
        mod = known ? known->mod : RecompileRegionLocked(pc);
    }
    if (!mod)
        return nullptr;
    // Waits for the mod's JIT when discovery missed the code at pc.
    return snx_recompiled_lookup(mod, pc);
}

snx_recompiled* ScarletNxProcess::FindModule(u64 pc) {
    std::scoped_lock lk{m_mutex};
    const Module* const mod = FindModuleLocked(pc);
    return mod ? mod->mod : nullptr;
}

snx_recompiled32* ScarletNxProcess::MakeModule32() {
    std::scoped_lock lk{m_mutex};
    std::vector<snx_code_range> code;
    for (const auto& range : GetCodeRanges()) {
        if (range.addr >= A32Limit) {
            continue;
        }
        code.push_back({range.addr, nullptr, std::min(range.size, A32Limit - range.addr)});
    }
    if (code.empty())
        return nullptr;
    snx_recompiled32* const mod = snx_recompile32_ranges(m_recompiler, code.data(), code.size());
    if (!mod)
        LOG_ERROR(Core_ARM, "cannot prepare A32 code: {}", snx_recompiler_error(m_recompiler));
    return mod;
}

void ScarletNxProcess::InvalidateCacheRange(u64 addr, u64 size) {
    std::scoped_lock lk{m_mutex};
    for (auto& mod : m_modules) {
        if (mod.retired || !std::ranges::any_of(mod.ranges, [&](const CodeRange& range) {
            return Overlaps(range, addr, size);
        }))
            continue;
        // Running code diverts out of the retired functions, and the region is recompiled from
        // the new code when it is next reached.
        if (mod.mod)
            snx_recompiled_invalidate(mod.mod, addr, size, nullptr, nullptr);
        mod.retired = true;
    }
    m_code_generation.fetch_add(1, std::memory_order_release);
}

void ScarletNxProcess::ClearInstructionCache() {
    std::scoped_lock lk{m_mutex};
    for (auto& mod : m_modules) {
        if (!mod.retired && mod.mod)
            snx_recompiled_invalidate(mod.mod, mod.lo, mod.hi - mod.lo, nullptr, nullptr);
        mod.retired = true;
    }
    m_code_generation.fetch_add(1, std::memory_order_release);
}

} // namespace Core
