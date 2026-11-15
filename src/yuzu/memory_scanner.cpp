// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

#include "common/logging.h"
#include "core/core.h"
#include "core/hle/kernel/k_memory_block.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/svc_types.h"
#include "core/memory.h"
#include "qt_common/qt_common.h"
#include "yuzu/memory_scanner.h"

namespace {

Kernel::KProcess* GameProcess() {
    if (!QtCommon::system || !QtCommon::system->IsPoweredOn()) {
        return nullptr;
    }
    return QtCommon::system->ApplicationProcess();
}

/// The value as the type stores it (integers rounded and kept in range), in the low bytes
u64 ToBits(MemoryScanner::Type type, double value) {
    using Type = MemoryScanner::Type;
    if (type == Type::F32) {
        const float f = static_cast<float>(value);
        u32 bits = 0;
        std::memcpy(&bits, &f, sizeof(bits));
        return bits;
    }
    const double rounded = std::round(value);
    if (rounded <= 0) {
        return 0;
    }
    const double limit = type == Type::U8    ? 0xFF
                         : type == Type::U16 ? 0xFFFF
                         : type == Type::U32 ? 0xFFFFFFFF
                                             : 18446744073709549568.0;
    return static_cast<u64>((std::min)(rounded, limit));
}

double FromBits(MemoryScanner::Type type, u64 bits) {
    if (type == MemoryScanner::Type::F32) {
        const u32 raw = static_cast<u32>(bits);
        float f = 0;
        std::memcpy(&f, &raw, sizeof(f));
        return f;
    }
    return static_cast<double>(bits);
}

/// Whether what's stored matches the value searched (a float within rounding of it, as a
/// game shows it)
bool Matches(MemoryScanner::Type type, u64 stored, u64 wanted_bits, double wanted) {
    if (type == MemoryScanner::Type::F32) {
        const double f = FromBits(type, stored);
        return std::isfinite(f) && std::fabs(f - wanted) <= 0.5;
    }
    return stored == wanted_bits;
}

u64 LoadAt(const u8* at, std::size_t size) {
    u64 bits = 0;
    std::memcpy(&bits, at, size);
    return bits;
}

} // Anonymous namespace

std::size_t MemoryScanner::Size(Type type) {
    switch (type) {
    case Type::U8:
        return 1;
    case Type::U16:
        return 2;
    case Type::U32:
    case Type::F32:
        return 4;
    case Type::U64:
        return 8;
    }
    return 4;
}

const char* MemoryScanner::Name(Type type) {
    switch (type) {
    case Type::U8:
        return "8-bit";
    case Type::U16:
        return "16-bit";
    case Type::U32:
        return "32-bit";
    case Type::U64:
        return "64-bit";
    case Type::F32:
        return "Float";
    }
    return "";
}

const char* MemoryScanner::Name(Compare compare) {
    switch (compare) {
    case Compare::Equal:
        return "= value";
    case Compare::Increased:
        return "increased";
    case Compare::Decreased:
        return "decreased";
    case Compare::Changed:
        return "changed";
    case Compare::Unchanged:
        return "unchanged";
    }
    return "";
}

void MemoryScanner::Reset() {
    scanned = false;
    truncated = false;
    results.clear();
    results.shrink_to_fit();
    previous.clear();
    previous.shrink_to_fit();
}

bool MemoryScanner::SameRun() const {
    const auto* process = GameProcess();
    return process && process->GetProcessId() == process_id;
}

std::size_t MemoryScanner::FirstScan(Type type_, double value) {
    Reset();
    type = type_;
    auto* process = GameProcess();
    if (!process) {
        return 0;
    }
    process_id = process->GetProcessId();
    scanned = true;
    const auto start = std::chrono::steady_clock::now();
    const std::size_t size = Size(type);
    const u64 wanted_bits = ToBits(type, value);
    auto& memory = process->GetMemory();
    auto& page_table = process->GetPageTable();
    std::vector<u8> chunk;
    constexpr std::size_t ChunkSize = 1 << 20;
    u64 scanned_bytes = 0;

    // Every block of the address space the game can read and write
    u64 address = 0;
    while (!truncated) {
        Kernel::KMemoryInfo info{};
        Kernel::Svc::PageInfo page_info{};
        if (page_table.QueryInfo(&info, &page_info, address) != ResultSuccess) {
            break;
        }
        const auto block = info.GetSvcMemoryInfo();
        const u64 base = block.base_address;
        const u64 end = base + block.size;
        const bool readwrite = block.state != Kernel::Svc::MemoryState::Free &&
                               (block.permission & Kernel::Svc::MemoryPermission::ReadWrite) ==
                                   Kernel::Svc::MemoryPermission::ReadWrite;
        if (readwrite) {
            for (u64 at = base; at < end && !truncated; at += ChunkSize) {
                const std::size_t length =
                    static_cast<std::size_t>((std::min)(static_cast<u64>(ChunkSize), end - at));
                chunk.resize(length);
                if (!memory.ReadBlockUnsafe(at, chunk.data(), length)) {
                    continue;
                }
                scanned_bytes += length;
                for (std::size_t off = 0; off + size <= length; off += size) {
                    const u64 stored = LoadAt(chunk.data() + off, size);
                    if (Matches(type, stored, wanted_bits, value)) {
                        if (results.size() == MaxResults) {
                            truncated = true;
                            break;
                        }
                        results.push_back(at + off);
                        previous.push_back(stored);
                    }
                }
            }
        }
        if (end <= address) {
            break;
        }
        address = end;
    }
    LOG_INFO(Frontend, "memory editor: first scan for {} ({}) in {} MiB: {} found{} in {} ms",
             value, Name(type), scanned_bytes >> 20, results.size(),
             truncated ? " (more not kept)" : "",
             std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::steady_clock::now() - start)
                 .count());
    return results.size();
}

std::size_t MemoryScanner::NextScan(Compare compare, double value) {
    if (!scanned) {
        return 0;
    }
    if (!SameRun()) {
        Reset();
        return 0;
    }
    auto& memory = GameProcess()->GetMemory();
    const std::size_t size = Size(type);
    const u64 wanted_bits = ToBits(type, value);
    std::size_t kept = 0;
    for (std::size_t i = 0; i < results.size(); ++i) {
        u64 stored = 0;
        if (!memory.ReadBlockUnsafe(results[i], &stored, size)) {
            continue;
        }
        const double now = FromBits(type, stored);
        const double before = FromBits(type, previous[i]);
        bool keep = false;
        switch (compare) {
        case Compare::Equal:
            keep = Matches(type, stored, wanted_bits, value);
            break;
        case Compare::Increased:
            keep = now > before;
            break;
        case Compare::Decreased:
            keep = now < before;
            break;
        case Compare::Changed:
            keep = stored != previous[i];
            break;
        case Compare::Unchanged:
            keep = stored == previous[i];
            break;
        }
        if (keep) {
            results[kept] = results[i];
            previous[kept] = stored;
            ++kept;
        }
    }
    results.resize(kept);
    previous.resize(kept);
    LOG_INFO(Frontend, "memory editor: next scan ({}, {}): {} left", Name(compare), value, kept);
    return kept;
}

double MemoryScanner::Read(u64 address) const {
    if (!SameRun()) {
        return 0;
    }
    u64 stored = 0;
    if (!GameProcess()->GetMemory().ReadBlockUnsafe(address, &stored, Size(type))) {
        return 0;
    }
    return FromBits(type, stored);
}

bool MemoryScanner::Write(u64 address, double value) const {
    if (!SameRun()) {
        return false;
    }
    const u64 bits = ToBits(type, value);
    const bool written = GameProcess()->GetMemory().WriteBlock(address, &bits, Size(type));
    LOG_INFO(Frontend, "memory editor: wrote {} ({}) at {:#x}{}", value, Name(type), address,
             written ? "" : " (failed)");
    return written;
}
