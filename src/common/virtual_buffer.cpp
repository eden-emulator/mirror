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

namespace Common {

void* AllocateMemoryPages(std::size_t size) noexcept {
#ifdef _WIN32
    void* addr = VirtualAlloc(nullptr, size, MEM_COMMIT, PAGE_READWRITE);
    ASSERT(addr != nullptr);
#elif defined(__OPENORBIS__)
    u64 align = 16384;
    void *addr = nullptr;
    off_t direct_mem_off;
    int32_t rc;
    if ((rc = sceKernelAllocateDirectMemory(0, sceKernelGetDirectMemorySize(), size, align, 3, &direct_mem_off)) < 0) {
        ASSERT(false && "sceKernelAllocateDirectMemory");
        return nullptr;
    }
    if ((rc = sceKernelMapDirectMemory(&addr, size, 0x33, 0, direct_mem_off, align)) < 0) {
        ASSERT(false && "sceKernelMapDirectMemory");
        return nullptr;
    }
    ASSERT(addr != nullptr);
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
#elif defined(__OPENORBIS__)
#else
    int rc = munmap(addr, size);
    ASSERT(rc == 0);
#endif
}

} // namespace Common
