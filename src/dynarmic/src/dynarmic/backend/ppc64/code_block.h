// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <cstdio>
#include <new>
#include <sys/mman.h>

#include "dynarmic/common/common_types.h"
#include "dynarmic/common/assert.h"

namespace Dynarmic::Backend::PPC64 {

class CodeBlock {
public:
    explicit CodeBlock(std::size_t size) noexcept : memsize(size) {
        mem = (u8*)mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ASSERT(mem != nullptr);
    }
    ~CodeBlock() noexcept {
        if (mem != nullptr)
            munmap(mem, memsize);
    }
    template<typename T>
    T ptr() const noexcept {
        static_assert(std::is_pointer_v<T> || std::is_same_v<T, uintptr_t> || std::is_same_v<T, intptr_t>);
        return reinterpret_cast<T>(mem);
    }
protected:
    u8* mem = nullptr;
    size_t memsize = 0;
};

}  // namespace Dynarmic::Backend::RV64
