// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include <bit>
#include <cstddef>
#include <span>
#include <utility>
#include <boost/functional/hash.hpp>

#include "common/common_types.h"
#include "common/container/unordered_map.h"
#include "dynarmic/backend/x64/xbyak.h"

namespace Dynarmic::Backend::X64 {

class BlockOfCode;

/// @brief ConstantPool allocates a block of memory from BlockOfCode.
/// It places constants into this block of memory, returning the address
/// of the memory location where the constant is placed. If the constant
/// already exists, its memory location is reused.
class ConstantPool final {
public:
    ConstantPool(BlockOfCode& code, size_t size) noexcept;
    Xbyak::Address GetConstant(BlockOfCode& code, const Xbyak::AddressFrame& frame, u64 lower, u64 upper = 0) noexcept;
    // key = identity hash, value = offset from pool.data()
    ::Common::unordered_map<std::pair<u64, u64>, u32, boost::hash<std::pair<u64, u64>>> constant_info;
    std::span<u128> pool;
};

}  // namespace Dynarmic::Backend::X64
