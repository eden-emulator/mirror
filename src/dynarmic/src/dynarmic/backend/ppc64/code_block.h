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
    explicit CodeBlock(size_t size_) noexcept : size{size_} {
        block = (u8*)mmap(nullptr, size, PROT_WRITE | PROT_READ | PROT_EXEC, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        //printf("block = %p, size= %zx\n", block, size);
        ASSERT(block != nullptr);
    }
    ~CodeBlock() noexcept {
        if (block != nullptr)
            munmap(block, size);
    }
    template<typename T>
    T ptr() const noexcept {
        static_assert(std::is_pointer_v<T> || std::is_same_v<T, uintptr_t> || std::is_same_v<T, intptr_t>);
        return reinterpret_cast<T>(block);
    }
protected:
    void* block = nullptr;
    size_t size = 0;
};

}  // namespace Dynarmic::Backend::RV64
