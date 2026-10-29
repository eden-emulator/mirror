// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <span>
#include <string_view>

#include "common/common_types.h"

namespace Core {
class System;
}

namespace Core::Memory {
class Memory;
}

namespace Kernel {
class KProcess;
}

namespace Core::HLE::DlssTrace {

// Diagnostic only (EDEN_DLSS_TRACE=1): patches the entry of selected sdk / subsdk1 functions with a
// private SVC that logs the arguments and return value and then continues in a relocated copy of the
// replaced instruction.
void InstallModule(Kernel::KProcess& process, std::string_view name, u64 base, u64 size);

// Address of the object whose writes are logged by the CPU memory callbacks (0 = off).
extern std::atomic<u64> g_watch_address;

// Host-side write watch: logs a symbolised host backtrace when [dst, dst + size) overlaps the watched object.
extern std::atomic<const unsigned char*> g_watch_host;
void WriteWatch(const char* tag, const void* dst, size_t size);

// True when EDEN_DLSS_TRACE is set. Used to configure the CPU for tracing (no fastmem, no page table).
bool TraceActive();

// Logs a guest store (from the CPU memory callbacks) that overlaps the watched object, with the
// guest pc, lr, a short frame-pointer chain and x0..x2.
void GuestWriteWatch(Core::Memory::Memory& memory, const char* tag, u64 vaddr, size_t size, u64 value, u64 pc,
                     u64 lr, u64 fp, u64 x0, u64 x1, u64 x2);

// Returns true if imm is one of ours.
bool HandleSvc(Core::System& system, u32 imm, std::span<u64, 8> args);

} // namespace Core::HLE::DlssTrace
