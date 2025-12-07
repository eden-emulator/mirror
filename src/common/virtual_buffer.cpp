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
static struct sigaction old_sa_segv;
static boost::container::static_vector<std::pair<void*, size_t>, 16> swap_regions;
static void SwapHandler(int sig, siginfo_t* si, void* raw_context) {
    if (std::ranges::find_if(swap_regions, [addr = si->si_addr](auto const& e) {
        return uintptr_t(addr) >= uintptr_t(e.first) && uintptr_t(addr) < uintptr_t(e.first) + e.second;
    }) != swap_regions.end()) {
        size_t const page_size = 4096;
        size_t const page_mask = ~0xfff;
        // should replace the existing mapping... ugh
        void* aligned_addr = reinterpret_cast<void*>(uintptr_t(si->si_addr) & page_mask);
        void* res = mmap(aligned_addr, page_size, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_ANON | MAP_PRIVATE, -1, 0);
        ASSERT(res != MAP_FAILED);
    } else {
        struct sigaction* retry_sa = &old_sa_segv;
        if ((retry_sa->sa_flags & SA_SIGINFO) != 0) {
            retry_sa->sa_sigaction(sig, si, raw_context);
        } else if (retry_sa->sa_handler == SIG_DFL) {
            signal(sig, SIG_DFL);
        } else if (retry_sa->sa_handler == SIG_IGN) {
            // ignore?
        } else {
            retry_sa->sa_handler(sig);
        }
    }
}

bool InitSwap() noexcept {
    struct sigaction sa;
    sa.sa_handler = NULL;
    sa.sa_sigaction = &SwapHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    return sigaction(SIGSEGV, &sa, &old_sa_segv) == 0;
}
#else
bool InitSwap() noexcept {
    return true;
}
#endif

void* AllocateMemoryPages(std::size_t size) noexcept {
#ifdef _WIN32
    void* addr = VirtualAlloc(nullptr, size, MEM_COMMIT, PAGE_READWRITE);
    ASSERT(addr != nullptr);
#elif defined(__OPENORBIS__)
    void* addr = mmap(nullptr, size, PROT_NONE, MAP_VOID | MAP_PRIVATE, -1, 0);
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
