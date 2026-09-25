// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* virtual_buffer.cpp */
// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#ifdef _WIN32
#include <windows.h>
#include <mutex>
#include <algorithm>
#include <vector>
#endif

#include <cerrno>
#include <cstring>

#ifndef _WIN32
#include <sys/mman.h>
#endif

#include "common/alignment.h"
#include "common/assert.h"
#include "common/sparse_large_vector.h"

namespace Common {

#ifdef _WIN32

struct VectorRegion {
    u64 start_page;
    u64 end_page;
};

static std::mutex& GetVectorRegionsMutex() {
    static std::mutex* m = new std::mutex();
    return *m;
}

static std::vector<VectorRegion>& GetVectorRegions() {
    static std::vector<VectorRegion>* v = new std::vector<VectorRegion>();
    return *v;
}

static LONG WINAPI FakePageFaultHandler(PEXCEPTION_POINTERS info) {
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    const u64 fault_addr = info->ExceptionRecord->ExceptionInformation[1];
    const u64 access_type = info->ExceptionRecord->ExceptionInformation[0];
    const bool is_write = (access_type == 1);
    const u64 fault_page = fault_addr >> HostPageBits;

    u64 addr = 0;
    u64 addr2 = 0;

    {
        std::lock_guard lock(GetVectorRegionsMutex());
        for (const auto& region : GetVectorRegions()) {
            if (fault_page >= region.start_page && fault_page < region.end_page) {
                addr = fault_page;
            }
            const u64 page2 = (fault_addr + 0x3F) >> HostPageBits;
            if (page2 != fault_page && page2 >= region.start_page && page2 < region.end_page) {
                addr2 = page2;
            }
            if (addr != 0 || addr2 != 0) break;
        }
    }

    if (addr == 0 && addr2 == 0) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    LOG_ERROR(HW_Memory, "Accessing an unallocated region of a SparseLargeVector at {:#x}; this shouldn't happen and is likely a Dynarmic error!", fault_addr);

    if (addr != 0 && !CommitVectorPage(addr << HostPageBits, is_write)) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (addr2 != 0 && !CommitVectorPage(addr2 << HostPageBits, is_write)) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    return EXCEPTION_CONTINUE_EXECUTION;
}

bool CommitVectorPage(uintptr_t addr, bool write) noexcept {
    MEMORY_BASIC_INFORMATION info {};
    const auto res = VirtualQuery(reinterpret_cast<void*>(addr), &info, sizeof(info));
    const DWORD perm = write ? PAGE_READWRITE : PAGE_READONLY;

    if (res == 0) {
        LOG_CRITICAL(HW_Memory, "Failed to query large buffer region at {:#x} with error {}, will try committing anyway", addr, GetLastError());
    } else if (info.State == MEM_COMMIT) {
        DWORD old_protect {};
        if (!VirtualProtect(reinterpret_cast<void*>(addr), HostPageSize, perm, &old_protect)) {
            LOG_ERROR(HW_Memory, "VirtualProtect failed at {:#x}, error {}", addr, GetLastError());
            return false;
        }
        return true;
    } else if (info.State != MEM_RESERVE) {
        LOG_ERROR(HW_Memory, "Tried to commit an unreserved large buffer region at {:#x} (state {:#x})", addr, info.State);
        return false;
    }

    if (VirtualAlloc(reinterpret_cast<LPVOID>(addr), HostPageSize, MEM_COMMIT, perm) == nullptr) {
        LOG_ERROR(HW_Memory, "Failed to commit large buffer region at {:#x}, error {}", addr, GetLastError());
        return false;
    }

    return true;
}

#endif

#ifndef MAP_NOCORE
#define MAP_NOCORE 0
#endif
#ifndef MADV_FREE
#define MADV_FREE MADV_DONTNEED
#endif

void DecommitVectorPage(uintptr_t base) noexcept {
#if defined(_WIN32)
    if (!VirtualFree(reinterpret_cast<LPVOID>(base), HostPageSize, MEM_DECOMMIT)) {
        LOG_WARNING(HW_Memory, "VirtualFree(MEM_DECOMMIT) failed at {:#x}, error {}", base, GetLastError());
    }
#elif defined(__linux__)
    if (madvise(reinterpret_cast<void*>(base), HostPageSize, MADV_DONTNEED) != 0) {
        LOG_WARNING(HW_Memory, "madvise(MADV_DONTNEED) failed at {:#x}: {}", base, std::strerror(errno));
    }
#else
    if (madvise(reinterpret_cast<void*>(base), HostPageSize, MADV_FREE) != 0) {
        LOG_WARNING(HW_Memory, "madvise(MADV_FREE) failed at {:#x}: {}", base, std::strerror(errno));
    }
    std::memset(reinterpret_cast<void*>(base), 0, HostPageSize);
#endif
}

void* AllocateMemoryPages(std::size_t size) noexcept {
    if (size == 0) {
        return nullptr;
    }

    const auto page = HostPageSize;
    if (size % page != 0) {
        LOG_WARNING(HW_Memory, "Allocating unaligned large vector with size {:#x}; aligning to {} page size", size, page);
        if (size > SIZE_MAX - (page - 1)) {
            LOG_CRITICAL(HW_Memory, "Size {:#x} would overflow page alignment", size);
            return nullptr;
        }
        size = AlignUp(size, page);
    }

#ifdef _WIN32
    void* base = VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_READWRITE);

    if (base != nullptr) {
        {
            std::lock_guard lock(GetVectorRegionsMutex());
            GetVectorRegions().push_back({
                reinterpret_cast<u64>(base) >> HostPageBits,
                (reinterpret_cast<u64>(base) + size) >> HostPageBits,
            });
        }

        static std::once_flag flag;
        std::call_once(flag, []() { AddVectoredExceptionHandler(1, FakePageFaultHandler); });
    } else {
        LOG_WARNING(HW_Memory, "Failed to reserve large vector region with error {}, trying to commit instead..", GetLastError());
        base = VirtualAlloc(nullptr, size, MEM_COMMIT, PAGE_READWRITE);
    }
    ASSERT_MSG(base, "Failed to reserve {:#x} sized region with error {}", size, GetLastError());
#else
    int flags = MAP_ANON | MAP_PRIVATE;
#ifdef MAP_NORESERVE
    flags |= MAP_NORESERVE;
#endif
#if defined(MAP_NOCORE)
    flags |= MAP_NOCORE;
#endif
    void* base = mmap(nullptr, size, PROT_READ, flags, -1, 0);
    if (base == MAP_FAILED) {
        base = nullptr;
    }
#ifdef MADV_HUGEPAGE
    if (base != nullptr) {
        madvise(base, size, MADV_HUGEPAGE);
    }
#endif
    ASSERT_MSG(base, "Failed to allocate {:#x} sized region with error {}", size, std::strerror(errno));
#endif

    return base;
}

void FreeMemoryPages(void* base, [[maybe_unused]] std::size_t size) noexcept {
    if (base == nullptr) {
        return;
    }

    if (const auto page = HostPageSize; size % page != 0) {
        size = AlignUp(size, page);
    }

#ifdef _WIN32
    {
        std::lock_guard lock(GetVectorRegionsMutex());
        auto& regions = GetVectorRegions();
        const u64 base_page = reinterpret_cast<u64>(base) >> HostPageBits;
        regions.erase(std::remove_if(regions.begin(), regions.end(),
            [base_page](const VectorRegion& r) { return r.start_page == base_page; }), regions.end());
    }
    if (!VirtualFree(base, 0, MEM_RELEASE)) {
        LOG_ERROR(HW_Memory, "VirtualFree failed, error {}", GetLastError());
    }
#else
    if (munmap(base, size) != 0) {
        LOG_ERROR(HW_Memory, "munmap failed: {}", std::strerror(errno));
    }
#endif
}

} // namespace Common