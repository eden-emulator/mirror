// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#ifdef _WIN32
#include <windows.h>
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

#include "common/assert.h"
#include "common/virtual_buffer.h"
#include "common/logging.h"

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
#ifdef _WIN32
    void* base = VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (base == nullptr) {
        // Probably failing to reserve is less likely than failing to commit
        base = VirtualAlloc(nullptr, size, MEM_COMMIT, PAGE_READWRITE);
    }
#elif defined(__OPENORBIS__)
    bool use_void_mem = true;
    void* addr = nullptr;
    if (size < 4294967296ull) {
        size_t align = Orbis::ORBIS_PAGE_SIZE;
        off_t offset;
        int32_t res;
        size = (size + align - 1) / align * align;
        if ((res = sceKernelAllocateDirectMemory(0, ORBIS_KERNEL_MAIN_DMEM_SIZE, size, align, ORBIS_KERNEL_WB_ONION, &offset)) == 0) {
            if ((res = sceKernelMapDirectMemory(&addr, size, ORBIS_KERNEL_PROT_CPU_READ | ORBIS_KERNEL_PROT_CPU_WRITE, 0, offset, size)) == 0) {
                if ((res = sceKernelMprotect(addr, size, VM_PROT_ALL)) == 0 && addr != nullptr) {
                    LOG_WARNING(HW_Memory, "Using DMem for {} bytes area @ {}", size, addr);
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
        addr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_VOID | MAP_PRIVATE, -1, 0);
        LOG_WARNING(HW_Memory, "Using VoidMem for {} bytes area @ {}", size, addr);
        ASSERT(addr != MAP_FAILED);
        swap_regions.emplace_back(addr, size);
    }
#else
    void* addr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    ASSERT(addr != MAP_FAILED);
#endif
    return addr;
}

void FreeMemoryPages(void* addr, [[maybe_unused]] std::size_t size) noexcept {
    if (!addr)
        return;
#ifdef _WIN32
    VirtualFree(addr, 0, MEM_RELEASE);
#elif defined(__OPENORBIS__)
    if (auto const it = std::ranges::find_if(swap_regions, [=](auto const& e) {
        return uintptr_t(addr) >= uintptr_t(e.first) && uintptr_t(addr) < uintptr_t(e.first) + e.second;
    }); it != swap_regions.end()) {
        int rc = munmap(addr, size);
        ASSERT(rc == 0);
    } else {
        sceKernelCheckedReleaseDirectMemory(off_t(addr), size_t(size));
    }
#else
    int rc = munmap(addr, size);
    ASSERT(rc == 0);
#endif
}

} // namespace Common
