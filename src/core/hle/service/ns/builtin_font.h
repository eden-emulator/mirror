// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/common_types.h"

namespace Service::NS {

// Native font-service wire record; the shared-memory offset is relative to command 2000.
struct BuiltInFontDescriptor {
    u8 type{};
    u8 flags{};
    u16 reserved{};
    u32 offset{};
    u32 size{};
};
static_assert(sizeof(BuiltInFontDescriptor) == 12);
static_assert(alignof(BuiltInFontDescriptor) == 4);

constexpr bool DescribeBuiltInFont(BuiltInFontDescriptor& descriptor, u8 type, u32 offset,
                                 u32 size, u64 memory_size) {
    descriptor = {};
    if (size == 0 || offset > memory_size || size > memory_size - offset) {
        return false;
    }
    descriptor = {type, 0, 0, offset, size};
    return true;
}

} // namespace Service::NS
