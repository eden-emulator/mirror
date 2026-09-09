// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* virtual_buffer.cpp */
// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#ifdef _WIN32
#include <windows.h>
#include <mutex>
#else
#include <sys/mman.h>
#endif

#include "common/alignment.h"
#include "common/assert.h"
#include "common/sparse_large_vector.h"

namespace Common {

#ifdef _WIN32
static std::vector<std::pair<u64, u64>> vector_regions {};

// Workaround for handling non-commited memory accessed by Dynarmic; usually result of an error
static LONG WINAPI FakePageFaultHandler(PEXCEPTION_POINTERS info) {
    DWORD code = info->ExceptionRecord->ExceptionCode;
    u64 exception_addr = reinterpret_cast<u64>(info->ExceptionRecord->ExceptionAddress);

    if (code != EXCEPTION_ACCESS_VIOLATION) {
        // Not our problem
        return EXCEPTION_CONTINUE_SEARCH;
    }

    u64 addr = 0, addr2 = 0;

    for (auto region: vector_regions) {
        auto addr_shifted = exception_addr >> HostPageBits;
        if (region.first <= addr_shifted && addr_shifted <= region.second) {
            addr = addr_shifted;
        }

        // Page-boundary accesses
        if (auto addr_ = (exception_addr + 0x40) >> HostPageBits; addr_ != addr_shifted && region.first <= addr_ && addr_ <= region.second) {
            addr2 = addr_;
        }

        if (addr != 0 || addr2 != 0) {
            break;
        }
    }

    if (addr == 0 && addr2 == 0) {
        // Not our problem
        return EXCEPTION_CONTINUE_SEARCH;
    }

    LOG_ERROR(HW_Memory, "Accessing an unallocated region of a SparseLargeVector at {:#x}; this shouldn't happen and is likely a Dynarmic error!", exception_addr);

    // Commit this region
    if (addr != 0) {
        if (!CommitVectorPage(addr << HostPageBits, false)) {
            return EXCEPTION_CONTINUE_SEARCH;
        }
    }
    // Commit next region if needed
    if (addr2 != 0) {
        if (!CommitVectorPage(addr2 << HostPageBits, false)) {
            return EXCEPTION_CONTINUE_SEARCH;
        }
    }

    return EXCEPTION_CONTINUE_EXECUTION;
}

bool CommitVectorPage(uintptr_t addr, bool write) noexcept {
    MEMORY_BASIC_INFORMATION info {};
    auto res = VirtualQuery(reinterpret_cast<void*>(addr), &info, sizeof(info));
    if (res == 0) {
        LOG_CRITICAL(HW_Memory, "Failed to query large buffer region at {:#x} with error {}, will try committing anyway", addr, GetLastError());
    } else if (info.State != MEM_RESERVE) {
        LOG_ERROR(HW_Memory, "Tried to commit an unreserved large buffer region at {:#x} that is not mapped or is already committed (state {:#x})", addr, info.State);
        return false;
    }

    auto perm = write ? PAGE_READWRITE : PAGE_READONLY;
    void* res2 = VirtualAlloc(reinterpret_cast<LPVOID>(addr), HostPageSize, MEM_COMMIT, perm);
    if (res2 == nullptr) {
        LOG_ERROR(HW_Memory, "Failed to commit large buffer region at {:#x}, error {}", addr, GetLastError());
        return false;
    }

    return true;
}
#endif

#ifndef MAP_NOCORE
#define MAP_NOCORE 0
#endif

void* AllocateMemoryPages(std::size_t size) noexcept {
    if (auto page = HostPageSize; size % page != 0) {
        LOG_WARNING(HW_Memory, "Allocating unaligned large vector with size {:#x}; aligning to {} page size", size, page);
        size = AlignUp(size, page);
    }

#ifdef _WIN32
    // We will never use this memory entirely so instead of committing it up front let's just reserve it and commit each page individually
    void* base = VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_READWRITE);

    if (base != nullptr) {
        vector_regions.emplace_back(reinterpret_cast<u64>(base), reinterpret_cast<u64>(base) + size);

        static std::once_flag flag;
        std::call_once(flag, []() { AddVectoredExceptionHandler(1, FakePageFaultHandler); });
    } else {
        // Try committing everything instead??
        LOG_WARNING(HW_Memory, "Failed to reserve large vector region with error {}, trying to commit instead..", GetLastError());
        base = VirtualAlloc(nullptr, size, MEM_COMMIT, PAGE_READWRITE);
    }
    ASSERT_MSG(base, "Failed to reserve {:#x} sized region with error {}", size, GetLastError());
#else
    void* base = mmap(nullptr, size, PROT_READ, MAP_ANON | MAP_PRIVATE | MAP_NOCORE, -1, 0);
    if (base == MAP_FAILED)
        base = nullptr;
    ASSERT_MSG(base, "Failed to allocate {:#x} sized region with error {}", size, strerror(errno));
#endif
    return base;
}

void FreeMemoryPages(void* base, [[maybe_unused]] std::size_t size) noexcept {
    if (auto page = HostPageSize; size % page != 0) {
        size = AlignUp(size, page);
    }
    if (!base)
        return;
#ifdef _WIN32
    ASSERT(VirtualFree(base, 0, MEM_RELEASE));
#else
    ASSERT(munmap(base, size) == 0);
#endif
}

} // namespace Common
