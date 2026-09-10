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

#ifdef __OPENORBIS__
#include <ranges>
#include <csignal>
#include <fcntl.h>
#include <boost/container/static_vector.hpp>
#include <orbis/SystemService.h>
#include <orbis/libkernel.h>
typedef void (*SceKernelExceptionHandler)(int32_t, void*);
extern "C" int32_t sceKernelInstallExceptionHandler(int32_t signum, SceKernelExceptionHandler handler);
#endif

#include "common/alignment.h"
#include "common/assert.h"
#include "common/sparse_large_vector.h"

// PlayStation 4
// Flag needs to be undef-ed on non PS4 since it has different semantics
// on some platforms.
#ifdef __OPENORBIS__
#   ifndef MAP_SYSTEM
#       define MAP_SYSTEM 0x2000
#   endif
#   ifndef MAP_VOID
#       define MAP_VOID 0x100
#   endif
// sigaction(2) has a motherfucking bug on musl where the thing isnt even properly prefixed
#   undef sa_sigaction
#   define sa_sigaction __sa_handler.__sa_sigaction
#endif

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

#ifdef __OPENORBIS__

namespace Orbis {
constexpr size_t ORBIS_PAGE_SIZE = 16384;

struct Ucontext {
    struct Sigset {
        u64 bits[2];
    } uc_sigmask;
    int field1_0x10[12];
    struct Mcontext {
        u64 mc_onstack;
        u64 mc_rdi;
        u64 mc_rsi;
        u64 mc_rdx;
        u64 mc_rcx;
        u64 mc_r8;
        u64 mc_r9;
        u64 mc_rax;
        u64 mc_rbx;
        u64 mc_rbp;
        u64 mc_r10;
        u64 mc_r11;
        u64 mc_r12;
        u64 mc_r13;
        u64 mc_r14;
        u64 mc_r15;
        int mc_trapno;
        u16 mc_fs;
        u16 mc_gs;
        u64 mc_addr;
        int mc_flags;
        u16 mc_es;
        u16 mc_ds;
        u64 mc_err;
        u64 mc_rip;
        u64 mc_cs;
        u64 mc_rflags;
        u64 mc_rsp;
        u64 mc_ss;
        u64 mc_len;
        u64 mc_fpformat;
        u64 mc_ownedfp;
        u64 mc_lbrfrom;
        u64 mc_lbrto;
        u64 mc_aux1;
        u64 mc_aux2;
        u64 mc_fpstate[104];
        u64 mc_fsbase;
        u64 mc_gsbase;
        u64 mc_spare[6];
    } uc_mcontext;
    struct Ucontext* uc_link;
    struct ExStack {
        void* ss_sp;
        std::size_t ss_size;
        int ss_flags;
        int _align;
    } uc_stack;
    int uc_flags;
    int __spare[4];
    int field7_0x4f4[3];
};
}

static boost::container::static_vector<std::pair<void*, size_t>, 16> swap_regions;
static std::mutex evil_swap_mutex;
extern "C" int sceKernelRemoveExceptionHandler(s32 sig_num);
static void SwapHandler(int sig, void* raw_context) {
    std::unique_lock lk{evil_swap_mutex};
    auto& mctx = ((Orbis::Ucontext*)raw_context)->uc_mcontext;
    if (auto const it = std::ranges::find_if(swap_regions, [addr = mctx.mc_addr](auto const& e) {
        return uintptr_t(addr) >= uintptr_t(e.first) && uintptr_t(addr) < uintptr_t(e.first) + e.second;
    }); it != swap_regions.end()) {
        size_t const page_size = Orbis::ORBIS_PAGE_SIZE * 128; //128K
        size_t const page_mask = ~(page_size - 1);
        // should replace the existing mapping... ugh
        void* aligned_addr = reinterpret_cast<void*>(uintptr_t(mctx.mc_addr) & page_mask);
        void* res = mmap(aligned_addr, page_size, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_ANON | MAP_PRIVATE, -1, 0);
        if (res == MAP_FAILED) {
            LOG_ERROR(HW_Memory, "{:#x},{} @ {}:{:#x}", mctx.mc_addr, aligned_addr, it->first, it->second);
            sceKernelRemoveExceptionHandler(SIGSEGV); // to not catch the next signal
        } else {
            LOG_DEBUG(HW_Memory, "{:#x},{} @ {}:{:#x}", mctx.mc_addr, aligned_addr, it->first, it->second);
        }
    } else {
        LOG_ERROR(HW_Memory, "fault in addr {:#x} at {:#x}", mctx.mc_addr, mctx.mc_rip); // print caller address
        sceKernelRemoveExceptionHandler(SIGSEGV); // to not catch the next signal
    }
}
void InitSwap() noexcept {
    sceKernelInstallExceptionHandler(SIGSEGV, &SwapHandler);
}
#else
void InitSwap() noexcept {}
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
#elif defined(__OPENORBIS__)
    bool use_void_mem = true;
    void* base = nullptr;
    if (size < 4294967296ull) {
        size_t align = Orbis::ORBIS_PAGE_SIZE;
        off_t offset;
        int32_t res;
        size = (size + align - 1) / align * align;
        if ((res = sceKernelAllocateDirectMemory(0, ORBIS_KERNEL_MAIN_DMEM_SIZE, size, align, ORBIS_KERNEL_WB_ONION, &offset)) == 0) {
            if ((res = sceKernelMapDirectMemory(&base, size, ORBIS_KERNEL_PROT_CPU_READ | ORBIS_KERNEL_PROT_CPU_WRITE, 0, offset, size)) == 0) {
                if ((res = sceKernelMprotect(base, size, VM_PROT_ALL)) == 0 && base != nullptr) {
                    LOG_WARNING(HW_Memory, "Using DMem for {} bytes area @ {}", size, base);
                    use_void_mem = false; //Memory properly mapped
                } else {
                    sceKernelReleaseDirectMemory(offset, size);
                    LOG_ERROR(HW_Memory, "{} = sceKernelMprotect({}, {})", res, offset, size);
                }
            } else {
                sceKernelReleaseDirectMemory(offset, size);
                LOG_ERROR(HW_Memory, "{} = sceKernelMapDirectMemory({}, {})", res, offset, size);
            }
        } else {
            LOG_ERROR(HW_Memory, "{} = sceKernelAllocateDirectMemory({}, {}, {})", res, size, align, offset);
        }
    }
    if (use_void_mem) {
        base = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_VOID | MAP_PRIVATE, -1, 0);
        LOG_WARNING(HW_Memory, "Using VoidMem for {} bytes area @ {}", size, base);
        ASSERT(base != MAP_FAILED);
        swap_regions.emplace_back(base, size);
    }
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
#elif defined(__OPENORBIS__)
    if (auto const it = std::ranges::find_if(swap_regions, [=](auto const& e) {
        return uintptr_t(base) >= uintptr_t(e.first) && uintptr_t(base) < uintptr_t(e.first) + e.second;
    }); it != swap_regions.end()) {
        int rc = munmap(base, size);
        ASSERT(rc == 0);
    } else {
        sceKernelCheckedReleaseDirectMemory(off_t(base), size_t(size));
    }
#else
    ASSERT(munmap(base, size) == 0);
#endif
}

} // namespace Common
