// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#ifdef _WIN32
#include <windows.h>
#elif defined(__OPENORBIS__)
#include <orbis/libkernel.h>
#else
#include <sys/mman.h>
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
#endif

namespace Common {

void* AllocateMemoryPages(std::size_t size) noexcept {
#ifdef _WIN32
    void* addr = VirtualAlloc(nullptr, size, MEM_COMMIT, PAGE_READWRITE);
    ASSERT(addr != nullptr);
#elif defined(__OPENORBIS__)
    void* addr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    ASSERT(addr != MAP_FAILED);
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
    VirtualFree(addr, 0, MEM_RELEASE)
#else
    int rc = munmap(addr, size);
    ASSERT(rc == 0);
#endif
}

} // namespace Common
