// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <atomic>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#undef near
#undef far
#endif
#include <cstdlib>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "common/logging.h"
#include "core/core.h"
#include "core/hle/dlss_trace.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/physical_core.h"
#include "core/hle/kernel/svc_types.h"
#include "core/memory.h"

namespace Core::HLE::DlssTrace {

std::atomic<u64> g_watch_address{0};
std::atomic<const unsigned char*> g_watch_host{nullptr};

#ifdef _WIN32
namespace {
std::string Symbolise(void* address) {
    static std::once_flag once;
    std::call_once(once, [] {
        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
        SymInitialize(GetCurrentProcess(), nullptr, TRUE);
    });
    alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256];
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 255;
    DWORD64 displacement = 0;
    if (SymFromAddr(GetCurrentProcess(), reinterpret_cast<DWORD64>(address), &displacement, symbol)) {
        return std::string(symbol->Name) + "+" + std::to_string(displacement);
    }
    return "?";
}
} // namespace
#endif

bool TraceActive() {
    static const bool active = std::getenv("EDEN_DLSS_TRACE") != nullptr;
    return active;
}

void GuestWriteWatch(Core::Memory::Memory& memory, const char* tag, u64 vaddr, size_t size, u64 value, u64 pc,
                     u64 lr, u64 fp, u64 x0, u64 x1, u64 x2) {
    const u64 watch = g_watch_address.load(std::memory_order_relaxed);
    if (watch == 0 || vaddr >= watch + 0x40 || vaddr + size <= watch) {
        return;
    }
    static std::atomic<int> hits{0};
    if (hits.fetch_add(1) >= 24) {
        return;
    }
    u64 f1 = 0, f2 = 0, f3 = 0;
    if (fp && memory.IsValidVirtualAddressRange(fp, 16)) {
        f1 = memory.Read64(fp + 8);
        const u64 n = memory.Read64(fp);
        if (n && memory.IsValidVirtualAddressRange(n, 16)) {
            f2 = memory.Read64(n + 8);
            const u64 n2 = memory.Read64(n);
            if (n2 && memory.IsValidVirtualAddressRange(n2, 16)) {
                f3 = memory.Read64(n2 + 8);
            }
        }
    }
    LOG_ERROR(Core_ARM,
              "DlssTrace WRITE-WATCH {} addr={:#x} size={} value={:#x} pc={:#x} lr={:#x} frames={:#x},{:#x},{:#x} "
              "x0={:#x} x1={:#x} x2={:#x}",
              tag, vaddr, size, value, pc, lr, f1, f2, f3, x0, x1, x2);
}

void WriteWatch(const char* tag, const void* dst, size_t size) {
    const unsigned char* watch = g_watch_host.load(std::memory_order_relaxed);
    if (watch == nullptr) {
        return;
    }
    const auto* begin = static_cast<const unsigned char*>(dst);
    if (begin >= watch + 0x40 || begin + size <= watch) {
        return;
    }
    static std::atomic<int> hits{0};
    if (hits.fetch_add(1) >= 16) {
        return;
    }
    std::string trace;
#ifdef _WIN32
    void* frames[20];
    const USHORT count = CaptureStackBackTrace(1, 20, frames, nullptr);
    for (USHORT i = 0; i < count; ++i) {
        trace += "\n    " + Symbolise(frames[i]);
    }
#endif
    LOG_ERROR(HW_Memory, "DlssTrace HOST-WRITE-WATCH {} dst={} size={} first qword after write unknown; backtrace:{}", tag,
              dst, size, trace);
}

namespace {
constexpr u32 EntrySvcBase = 0x7B00;
constexpr u32 ReturnSvc = 0x7B7F;

struct Target {
    const char* name;
    u64 offset;
};

constexpr Target SdkTargets[] = {
    {"sdk.iface0x00", 0x5de134}, {"sdk.iface0x08", 0x5de15c}, {"sdk.iface0x10", 0x5de164},
    {"sdk.iface0x18", 0x5de178}, {"sdk.iface0x20", 0x5de1c4}, {"sdk.iface0x28", 0x5de1ec},
    {"sdk.iface0x30", 0x5de210}, {"sdk.iface0x38", 0x5de21c}, {"sdk.iface0x40", 0x5de22c},
    {"sdk.EvaluateDlss", 0x5dd600}, {"sdk.NetworkInitialize", 0x5dcdd0},
    {"sdk.NvnTexture.vtbl0x00", 0x5de760}, {"sdk.NvnTexture.vtbl0x08", 0x5de820},
    {"sdk.TextureInitialize", 0x5ee1a0}, {"sdk.TextureFinalize", 0x5ee8f0},
    {"sdk.MemoryPoolInitialize", 0x5e0ee0}, {"sdk.MemoryPoolFinalize", 0x5e10c0},
};
constexpr Target Subsdk1Targets[] = {
    {"lib.NgxCubin.CreateBuffer0x290", 0xa78c0}, {"lib.NgxCubin.Alloc1D", 0xa7e4c},
    {"lib.NgxCubin.Alloc2D", 0xa80a0}, {"lib.CreateDlssInstance", 0x1180}, {"lib.Evaluate", 0x1880},
};

struct Hook {
    const char* name;
    u64 entry;       // address whose instruction was replaced by the SVC
    u64 stub;        // relocated instruction + branch back
    bool replace_lr; // entry hook: intercept the return too
};

std::mutex mutex;
std::vector<Hook> hooks;
u64 return_stub = 0;
std::unordered_map<u64, std::vector<u64>> saved_lr; // keyed by sp at entry
std::atomic<u32> lines{0};
u32 evaluate_count = 0;
u64 sdk_base = 0;
std::unordered_map<u64, u64> watched; // object address -> first qword when last seen valid

bool PcRelative(u32 insn) {
    return (insn & 0x7c000000) == 0x14000000 ||  // b / bl
           (insn & 0x7e000000) == 0x34000000 ||  // cbz / cbnz
           (insn & 0x7e000000) == 0x36000000 ||  // tbz / tbnz
           (insn & 0xff000010) == 0x54000000 ||  // b.cond
           (insn & 0x1f000000) == 0x10000000 ||  // adr / adrp
           (insn & 0x3b000000) == 0x18000000;    // ldr literal
}

void InstallTargets(Kernel::KProcess& process, u64 base, const Target* targets, size_t count, u64 stub_area) {
    auto& memory = process.GetMemory();
    u64 stub = stub_area;
    for (size_t i = 0; i < count; ++i) {
        const Target& target = targets[i];
        u64 entry = base + target.offset;
        bool replace_lr = true;
        u32 original = memory.Read32(entry);
        if (PcRelative(original)) {
            entry += 4;
            original = memory.Read32(entry);
            replace_lr = false;
            if (PcRelative(original)) {
                LOG_ERROR(HW_Memory, "DlssTrace: {} has PC-relative instructions at entry, skipped", target.name);
                continue;
            }
        }
        const u32 index = static_cast<u32>(hooks.size());
        const s64 distance = (static_cast<s64>(entry + 4) - static_cast<s64>(stub + 4)) / 4;
        memory.Write32(stub, original);
        memory.Write32(stub + 4, 0x14000000 | (static_cast<u32>(distance) & 0x03ffffff));
        memory.Write32(entry, 0xd4000001 | ((EntrySvcBase + index) << 5));
        hooks.push_back(Hook{target.name, entry, stub, replace_lr});
        stub += 8;
    }
    LOG_ERROR(HW_Memory, "DlssTrace: installed hooks at base {:#x}, total {}", base, hooks.size());
}

} // namespace

void InstallModule(Kernel::KProcess& process, std::string_view name, u64 base, u64 size) {
    static const bool enabled = [] {
        const char* v = std::getenv("EDEN_DLSS_TRACE");
        return v && *v == '1';
    }();
    if (!enabled) {
        return;
    }
    (void)size;
    std::scoped_lock lock{mutex};
    auto& memory = process.GetMemory();
    if (name == "sdk") {
        // Stubs live in the zero padding at the end of the text segment (text size 0x7b6610, page end 0x7b7000).
        const u64 area = base + 0x7b6800;
        sdk_base = base;
        return_stub = area + 0x400;
        memory.Write32(return_stub, 0xd4000001 | (ReturnSvc << 5));
        InstallTargets(process, base, SdkTargets, std::size(SdkTargets), area);
    } else if (name == "subsdk1") {
        // Text size 0xe4fc0, page end 0xe5000: 16 words of padding.
        InstallTargets(process, base, Subsdk1Targets, std::size(Subsdk1Targets), base + 0xe4fc0);
    }
}

bool HandleSvc(Core::System& system, u32 imm, std::span<u64, 8> args) {
    if (imm < EntrySvcBase || imm > ReturnSvc) {
        // Memory-management SVCs touching a watched object's neighbourhood (heap resize, map/unmap).
        static const bool tracing = std::getenv("EDEN_DLSS_TRACE") != nullptr;
        if (tracing) {
            // Check the watched objects at every SVC to narrow down when and where they are overwritten.
            std::scoped_lock lock{mutex};
            if (!watched.empty()) {
                auto& k = system.Kernel();
                auto& p = Kernel::GetCurrentProcess(k);
                auto& m = p.GetMemory();
                if (imm == 0x21) {
                    auto* const a0 = p.GetArmInterface(k.CurrentPhysicalCore().CoreIndex());
                    Kernel::Svc::ThreadContext c0{};
                    a0->GetContext(c0);
                    LOG_ERROR(HW_Memory, "DlssTrace IPC handle={:#x} cmd: {:08x} {:08x} {:08x} {:08x} {:08x} {:08x} {:08x} {:08x} lr={:#x}",
                              args[0], m.Read32(c0.tpidr), m.Read32(c0.tpidr + 4), m.Read32(c0.tpidr + 8),
                              m.Read32(c0.tpidr + 0xc), m.Read32(c0.tpidr + 0x10), m.Read32(c0.tpidr + 0x14),
                              m.Read32(c0.tpidr + 0x18), m.Read32(c0.tpidr + 0x1c), c0.lr);
                }
                for (auto it = watched.begin(); it != watched.end();) {
                    const u64 now = m.IsValidVirtualAddressRange(it->first, 8) ? m.Read64(it->first) : 0;
                    if (now == it->second) {
                        ++it;
                        continue;
                    }
                    auto* const a = p.GetArmInterface(k.CurrentPhysicalCore().CoreIndex());
                    Kernel::Svc::ThreadContext c{};
                    a->GetContext(c);
                    LOG_ERROR(HW_Memory, "DlssTrace WATCH-SVC object {:#x} {:#x} -> {:#x} at svc {:#x} pc={:#x} lr={:#x} sp={:#x}",
                              it->first, it->second, now, imm, c.pc, c.lr, c.sp);
                    u64 fp = c.fp;
                    for (int depth = 0; depth < 10 && fp != 0 && m.IsValidVirtualAddressRange(fp, 16); ++depth) {
                        LOG_ERROR(HW_Memory, "DlssTrace WATCH-SVC frame {}: lr={:#x}", depth, m.Read64(fp + 8));
                        fp = m.Read64(fp);
                    }
                    it = watched.erase(it);
                }
            }
        }
        if (tracing && (imm == 1 || imm == 2 || imm == 3 || imm == 4 || imm == 5 || imm == 0x2C || imm == 0x2D ||
                        imm == 0x35 || imm == 0x36 || imm == 0x37 || imm == 0x73 || imm == 0x74 || imm == 0x75)) {
            std::scoped_lock lock{mutex};
            if (!watched.empty()) {
                const u64 watched_page = watched.begin()->first & ~0xfffffULL;
                bool close_by = false;
                for (size_t i = 0; i < 3; ++i) {
                    close_by |= args[i] >= watched_page - 0x1000000 && args[i] < watched_page + 0x2000000;
                }
                if (close_by) {
                    auto& k = system.Kernel();
                    auto& p = Kernel::GetCurrentProcess(k);
                    auto* const a = p.GetArmInterface(k.CurrentPhysicalCore().CoreIndex());
                    Kernel::Svc::ThreadContext c{};
                    a->GetContext(c);
                    LOG_ERROR(HW_Memory, "DlssTrace MEMSVC {:#x} x0={:#x} x1={:#x} x2={:#x} x3={:#x} lr={:#x} pc={:#x}", imm,
                              args[0], args[1], args[2], args[3], c.lr, c.pc);
                }
            }
        }
        return false;
    }
    auto& kernel = system.Kernel();
    auto& process = Kernel::GetCurrentProcess(kernel);
    auto& memory = process.GetMemory();
    auto* const arm = process.GetArmInterface(kernel.CurrentPhysicalCore().CoreIndex());
    Kernel::Svc::ThreadContext ctx{};
    arm->GetContext(ctx);
    const auto first_qword = [&](u64 address) -> u64 {
        if (address < 0x1000000 || !memory.IsValidVirtualAddressRange(address, 8)) {
            return 0;
        }
        return memory.Read64(address);
    };
    std::scoped_lock lock{mutex};
    // One-shot watch: report the first hook event that finds a tracked texture object's vtable changed.
    for (auto it = watched.begin(); it != watched.end();) {
        const u64 now = first_qword(it->first);
        if (now != it->second) {
            LOG_ERROR(HW_Memory,
                      "DlssTrace WATCH object {:#x} first qword {:#x} -> {:#x} (noticed at svc {:#x}, lr={:#x}, evaluates so far {})",
                      it->first, it->second, now, imm, ctx.lr, evaluate_count);
            it = watched.erase(it);
        } else {
            ++it;
        }
    }
    if (imm == ReturnSvc) {
        auto& stack = saved_lr[ctx.sp];
        if (stack.empty()) {
            LOG_ERROR(HW_Memory, "DlssTrace: return svc without saved LR, sp={:#x}", ctx.sp);
            return true;
        }
        const u64 lr = stack.back();
        stack.pop_back();
        if (lines.fetch_add(1) < 20000) {
            LOG_ERROR(HW_Memory, "DlssTrace RET x0={:#x} [x0]={:#x} -> {:#x}", args[0], first_qword(args[0]), lr);
        }
        ctx.pc = lr;
        arm->SetContext(ctx);
        return true;
    }
    const u32 index = imm - EntrySvcBase;
    if (index >= hooks.size()) {
        return true;
    }
    const Hook& hook = hooks[index];
    if (std::string_view{hook.name} == "sdk.iface0x40") {
        const u64 vt = first_qword(args[1]);
        if (vt == sdk_base + 0xdd0d48) {
            if (watched.try_emplace(args[1], vt).second && g_watch_address.load() == 0) {
                g_watch_address.store(args[1]);
                g_watch_host.store(memory.GetPointer<unsigned char>(args[1]));
            }
        }
    }
    if (std::string_view{hook.name} == "sdk.EvaluateDlss") {
        ++evaluate_count;
        // x1 = network, x2 = params: sizes for the dynamic-resolution check.
        LOG_ERROR(HW_Memory,
                  "DlssTrace EVALUATE #{} net+0x1d8: {:#x} {:#x} {:#x} {:#x} params+0x80: {:#x} {:#x} {:#x} {:#x} tex: {:#x} {:#x} {:#x} {:#x}",
                  evaluate_count, memory.Read64(args[1] + 0x1d8), memory.Read64(args[1] + 0x1e0),
                  memory.Read64(args[1] + 0x1e8), memory.Read64(args[1] + 0x1f0), memory.Read64(args[2] + 0x80),
                  memory.Read64(args[2] + 0x88), memory.Read64(args[2] + 0x90), memory.Read64(args[2] + 0x98),
                  memory.Read64(args[2]), memory.Read64(args[2] + 0x10), memory.Read64(args[2] + 0x20),
                  memory.Read64(args[2] + 0x30));
    }
    if (lines.fetch_add(1) < 20000) {
        LOG_ERROR(HW_Memory,
                  "DlssTrace ENTER {} x0={:#x}[{:#x}] x1={:#x}[{:#x}] x2={:#x} x3={:#x} x4={:#x} x5={:#x} lr={:#x} sp={:#x}",
                  hook.name, args[0], first_qword(args[0]), args[1], first_qword(args[1]), args[2], args[3], args[4],
                  args[5], ctx.lr, ctx.sp);
    }
    if (hook.replace_lr && return_stub != 0) {
        saved_lr[ctx.sp].push_back(ctx.lr);
        ctx.lr = return_stub;
    }
    ctx.pc = hook.stub;
    arm->SetContext(ctx);
    return true;
}

} // namespace Core::HLE::DlssTrace
