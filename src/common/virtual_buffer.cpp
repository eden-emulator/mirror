// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
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
#include <boost/container/static_vector.hpp>
#include <orbis/SystemService.h>
typedef void (*SceKernelExceptionHandler)(int32_t, void*);
extern "C" int32_t sceKernelInstallExceptionHandler(int32_t signum, SceKernelExceptionHandler handler);
#endif

#include "common/assert.h"
#include "common/virtual_buffer.h"
#include "common/logging/log.h"

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
extern "C" int sceKernelRemoveExceptionHandler(s32 sig_num);
static void SwapHandler(int sig, void* raw_context) {
    auto& mctx = ((Orbis::Ucontext*)raw_context)->uc_mcontext;
    if (std::ranges::find_if(swap_regions, [addr = mctx.mc_addr](auto const& e) {
        return uintptr_t(addr) >= uintptr_t(e.first) && uintptr_t(addr) < uintptr_t(e.first) + e.second;
    }) != swap_regions.end()) {
        size_t const page_size = 4096;
        size_t const page_mask = ~0xfff;
        // should replace the existing mapping... ugh
        void* aligned_addr = reinterpret_cast<void*>(uintptr_t(mctx.mc_addr) & page_mask);
        void* res = mmap(aligned_addr, page_size, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_ANON | MAP_PRIVATE, -1, 0);
        ASSERT(res != MAP_FAILED);
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
    void* addr = VirtualAlloc(nullptr, size, MEM_COMMIT, PAGE_READWRITE);
    ASSERT(addr != nullptr);
#elif defined(__OPENORBIS__)
    void* addr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_VOID | MAP_PRIVATE, -1, 0);
    ASSERT(addr != MAP_FAILED);
    swap_regions.emplace_back(addr, size);
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
#else
    int rc = munmap(addr, size);
    ASSERT(rc == 0);
#endif
}

} // namespace Common
