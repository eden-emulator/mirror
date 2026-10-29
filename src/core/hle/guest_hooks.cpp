// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <atomic>
#include <cstdlib>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "common/alignment.h"
#include "common/logging.h"
#include "core/core.h"
#include "core/hle/guest_hooks.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/physical_core.h"
#include "core/hle/kernel/svc_types.h"
#include "core/memory.h"

namespace Core::HLE::GuestHooks {

namespace {

constexpr u32 EntrySvcBase = 0x7C00;
constexpr u32 ReturnSvc = 0x7C7F;
// Stub words reserved at the end of a text segment: the return stub plus 8 bytes per entry stub.
constexpr u64 StubBytes = 0x40;
constexpr size_t MaxHooks = 7;

struct Spec {
    std::string module;
    u64 offset;
};

struct Hook {
    std::string name;
    u64 address;     // patched instruction
    u64 stub;        // relocated instruction + branch back (entry hooks only)
    u64 return_stub; // address of the return svc of the module
    u32 call_reg;    // register of the replaced `blr`, or ~0u for an entry hook
};

struct ModuleRange {
    std::string name;
    u64 base;
    u64 size;
};

std::mutex mutex;
std::vector<Hook> hooks;
std::vector<ModuleRange> modules;
std::unordered_map<u64, std::vector<u64>> saved_returns; // keyed by sp at the hooked instruction
std::atomic<u32> lines{0};

const std::vector<Spec>& Specs() {
    static const std::vector<Spec> specs = [] {
        std::vector<Spec> result;
        const char* env = std::getenv("EDEN_GUEST_HOOKS");
        if (env == nullptr) {
            return result;
        }
        std::string_view text{env};
        while (!text.empty()) {
            const size_t comma = text.find(',');
            const std::string_view item = text.substr(0, comma);
            text = comma == std::string_view::npos ? std::string_view{} : text.substr(comma + 1);
            const size_t plus = item.find('+');
            if (plus == std::string_view::npos || plus == 0) {
                LOG_ERROR(Core, "GuestHooks: ignoring '{}', expected <module>+<offset>", item);
                continue;
            }
            const std::string offset{item.substr(plus + 1)};
            char* end = nullptr;
            const u64 value = std::strtoull(offset.c_str(), &end, 0);
            if (end == offset.c_str() || *end != '\0') {
                LOG_ERROR(Core, "GuestHooks: ignoring '{}', bad offset", item);
                continue;
            }
            result.push_back(Spec{std::string{item.substr(0, plus)}, value});
        }
        return result;
    }();
    return specs;
}

bool PcRelative(u32 insn) {
    return (insn & 0x7c000000) == 0x14000000 ||  // b / bl
           (insn & 0x7e000000) == 0x34000000 ||  // cbz / cbnz
           (insn & 0x7e000000) == 0x36000000 ||  // tbz / tbnz
           (insn & 0xff000010) == 0x54000000 ||  // b.cond
           (insn & 0x1f000000) == 0x10000000 ||  // adr / adrp
           (insn & 0x3b000000) == 0x18000000;    // ldr literal
}

bool IsBlr(u32 insn) {
    return (insn & 0xfffffc1f) == 0xd63f0000;
}

std::string Describe(u64 address) {
    for (const auto& module : modules) {
        if (address >= module.base && address < module.base + module.size) {
            return fmt::format("{}+{:#x}", module.name, address - module.base);
        }
    }
    return "?";
}

u64 Register(const Kernel::Svc::ThreadContext& ctx, u32 index) {
    if (index < ctx.r.size()) {
        return ctx.r[index];
    }
    return index == 29 ? ctx.fp : index == 30 ? ctx.lr : ctx.sp;
}

} // namespace

void InstallModule(Kernel::KProcess& process, std::string_view name, u64 base, u64 size,
                   u64 text_size) {
    const auto& specs = Specs();
    if (specs.empty()) {
        return;
    }
    std::scoped_lock lock{mutex};
    modules.push_back(ModuleRange{std::string{name}, base, size});

    const u64 page_end = Common::AlignUp(text_size, 0x1000);
    auto& memory = process.GetMemory();
    u64 stub_base = page_end - StubBytes;
    if (text_size > stub_base) {
        // The text segment has no zero padding to hold the stubs.
        stub_base = 0;
    }
    u64 used = 0;
    for (const Spec& spec : specs) {
        if (spec.module != name) {
            continue;
        }
        const std::string label = fmt::format("{}+{:#x}", name, spec.offset);
        if (stub_base == 0 || used >= MaxHooks || spec.offset >= text_size) {
            LOG_ERROR(Core, "GuestHooks: {} skipped (no stub space or outside the text segment)", label);
            continue;
        }
        const u64 return_stub = base + stub_base;
        if (used == 0) {
            memory.Write32(return_stub, 0xd4000001 | (ReturnSvc << 5));
        }
        const u64 address = base + spec.offset;
        const u32 original = memory.Read32(address);
        const u32 index = static_cast<u32>(hooks.size());
        Hook hook{label, address, 0, return_stub, ~0u};
        if (IsBlr(original)) {
            hook.call_reg = (original >> 5) & 31;
        } else if (PcRelative(original)) {
            LOG_ERROR(Core, "GuestHooks: {} has a PC-relative instruction, skipped", label);
            continue;
        } else {
            hook.stub = base + stub_base + 8 + used * 8;
            const s64 distance = (static_cast<s64>(address + 4) - static_cast<s64>(hook.stub + 4)) / 4;
            memory.Write32(hook.stub, original);
            memory.Write32(hook.stub + 4, 0x14000000 | (static_cast<u32>(distance) & 0x03ffffff));
        }
        memory.Write32(address, 0xd4000001 | ((EntrySvcBase + index) << 5));
        hooks.push_back(std::move(hook));
        ++used;
        LOG_ERROR(Core, "GuestHooks: {} at {:#x} {}", label, address,
                  hooks.back().call_reg == ~0u ? "entry hook" : "call-site hook");
    }
}

bool HandleSvc(Core::System& system, u32 imm, std::span<u64, 8> args) {
    if (imm < EntrySvcBase || imm > ReturnSvc) {
        return false;
    }
    auto& kernel = system.Kernel();
    auto& process = Kernel::GetCurrentProcess(kernel);
    auto* const arm = process.GetArmInterface(kernel.CurrentPhysicalCore().CoreIndex());
    Kernel::Svc::ThreadContext ctx{};
    arm->GetContext(ctx);
    std::scoped_lock lock{mutex};

    if (imm == ReturnSvc) {
        auto& stack = saved_returns[ctx.sp];
        if (stack.empty()) {
            LOG_ERROR(Core, "GuestHooks: return svc without a saved address, sp={:#x}", ctx.sp);
            return true;
        }
        const u64 resume = stack.back();
        stack.pop_back();
        if (lines.fetch_add(1) < 20000) {
            LOG_ERROR(Core, "GuestHooks RET x0={:#x} -> {}", args[0], Describe(resume));
        }
        ctx.pc = resume;
        arm->SetContext(ctx);
        return true;
    }

    const u32 index = imm - EntrySvcBase;
    if (index >= hooks.size()) {
        return true;
    }
    const Hook& hook = hooks[index];
    u64 target = 0;
    if (hook.call_reg != ~0u) {
        target = Register(ctx, hook.call_reg);
        if (lines.fetch_add(1) < 20000) {
            LOG_ERROR(Core,
                      "GuestHooks CALL {} target={:#x} ({}) x0={:#x} x1={:#x} x2={:#x} x3={:#x}",
                      hook.name, target, Describe(target), args[0], args[1], args[2], args[3]);
        }
        saved_returns[ctx.sp].push_back(hook.address + 4);
        ctx.lr = hook.return_stub;
        ctx.pc = target;
    } else {
        if (lines.fetch_add(1) < 20000) {
            LOG_ERROR(Core, "GuestHooks ENTER {} x0={:#x} x1={:#x} x2={:#x} x3={:#x} lr={:#x} ({})",
                      hook.name, args[0], args[1], args[2], args[3], ctx.lr, Describe(ctx.lr));
        }
        saved_returns[ctx.sp].push_back(ctx.lr);
        ctx.lr = hook.return_stub;
        ctx.pc = hook.stub;
    }
    arm->SetContext(ctx);
    return true;
}

} // namespace Core::HLE::GuestHooks
