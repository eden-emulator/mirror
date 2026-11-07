// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include <cstring>
#include <cassert>
#include "dynarmic/backend/x64/block_of_code.h"
#include "dynarmic/backend/x64/constant_pool.h"

namespace Dynarmic::Backend::X64 {

ConstantPool::ConstantPool(BlockOfCode& code, size_t size) noexcept {
    auto const align_size = 64; //always align to cacheline
    code.EnsureMemoryCommitted(2 * align_size + size);
    code.align(align_size);
    pool = std::span<u128>(static_cast<u128*>(code.AllocateFromCodeSpace(size)), size / sizeof(u128));
}

Xbyak::Address ConstantPool::GetConstant(BlockOfCode& code, const Xbyak::AddressFrame& frame, u64 lo, u64 hi) noexcept {
    // offset grows iff new constant is added
    auto const key = std::make_pair(lo, hi);
    auto const val = u32(constant_info.size());
    auto const [it, _] = constant_info.insert({key, val});
    // if it != constant_info.end(): assume(pool[it->second] == c);
    pool[it->second] = u128{lo, hi};
    return frame[code.rip + std::addressof(pool[it->second])];
}

}  // namespace Dynarmic::Backend::X64
