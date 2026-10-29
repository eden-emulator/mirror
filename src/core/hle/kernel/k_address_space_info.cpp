// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include "common/alignment.h"

#include "common/assert.h"
#include "common/literals.h"
#include "core/hle/kernel/k_address_space_info.h"

namespace Kernel {

namespace {

using namespace Common::Literals;

constexpr u64 Size_Invalid = UINT64_MAX;

// clang-format off
constexpr std::array<KAddressSpaceInfo, 13> AddressSpaceInfos{{
   { .bit_width = 32, .address = 2_MiB       , .size = 1_GiB   - 2_MiB  , .type = KAddressSpaceInfo::Type::MapSmall, },
   { .bit_width = 32, .address = 1_GiB       , .size = 4_GiB   - 1_GiB  , .type = KAddressSpaceInfo::Type::MapLarge, },
   { .bit_width = 32, .address = Size_Invalid, .size = 1_GiB            , .type = KAddressSpaceInfo::Type::Alias,    },
   { .bit_width = 32, .address = Size_Invalid, .size = 1_GiB            , .type = KAddressSpaceInfo::Type::Heap,     },
   { .bit_width = 36, .address = 128_MiB     , .size = 2_GiB   - 128_MiB, .type = KAddressSpaceInfo::Type::MapSmall, },
   { .bit_width = 36, .address = 2_GiB       , .size = 64_GiB  - 2_GiB  , .type = KAddressSpaceInfo::Type::MapLarge, },
   { .bit_width = 36, .address = Size_Invalid, .size = 8_GiB            , .type = KAddressSpaceInfo::Type::Heap,     },
   { .bit_width = 36, .address = Size_Invalid, .size = 6_GiB            , .type = KAddressSpaceInfo::Type::Alias,    },
#ifdef HAS_NCE
   // With NCE, we use a 38-bit address space due to memory limitations. This should (safely) truncate ASLR region.
   { .bit_width = 39, .address = 128_MiB     , .size = 256_GiB - 128_MiB, .type = KAddressSpaceInfo::Type::Map39Bit, },
#else
   { .bit_width = 39, .address = 128_MiB     , .size = 512_GiB - 128_MiB, .type = KAddressSpaceInfo::Type::Map39Bit, },
#endif
   { .bit_width = 39, .address = Size_Invalid, .size = 64_GiB           , .type = KAddressSpaceInfo::Type::MapSmall  },
   { .bit_width = 39, .address = Size_Invalid, .size = 8_GiB            , .type = KAddressSpaceInfo::Type::Heap,     },
   { .bit_width = 39, .address = Size_Invalid, .size = 64_GiB           , .type = KAddressSpaceInfo::Type::Alias,    },
   { .bit_width = 39, .address = Size_Invalid, .size = 2_GiB            , .type = KAddressSpaceInfo::Type::Stack,    },
}};
// clang-format on

const KAddressSpaceInfo& GetAddressSpaceInfo(size_t width, KAddressSpaceInfo::Type type) {
    for (auto& info : AddressSpaceInfos) {
        if (info.bit_width == width && info.type == type) {
            return info;
        }
    }
    UNREACHABLE_MSG("Could not find AddressSpaceInfo");
}

} // namespace

std::size_t KAddressSpaceInfo::GetAddressSpaceStart(size_t width, KAddressSpaceInfo::Type type) {
    return GetAddressSpaceInfo(width, type).address;
}

std::size_t KAddressSpaceInfo::GetAddressSpaceSize(size_t width, KAddressSpaceInfo::Type type,
                                                  size_t physical_memory_size) {
    if (width == 39 && (type == Type::Heap || type == Type::Alias)) {
        const size_t heap_size = std::clamp(
            Common::AlignUp(std::min(physical_memory_size, size_t{32_GiB}), size_t{1_GiB}),
            size_t{8_GiB}, size_t{32_GiB});
        return type == Type::Heap ? heap_size : std::min(heap_size * 8, size_t{128_GiB});
    }
    return GetAddressSpaceInfo(width, type).size;
}

} // namespace Kernel
