// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/common_types.h"

namespace Service::Nvidia::Devices {

struct GpuAddressSpaceLayout {
    u64 start;
    u64 split;
    u64 end;
};

constexpr GpuAddressSpaceLayout GetGpuAddressSpaceLayout(bool extended) {
    // Leave room for other 4 KiB allocations beyond the native bootstrap
    // reservation of 0x7ff8000000 bytes.
    // Both layouts fit within the GPU's reported 40-bit virtual address width.
    return {0x8000000, extended ? 3ULL << 38 : 1ULL << 34,
            extended ? 1ULL << 40 : 1ULL << 37};
}

constexpr u32 GetGpuAddressSpaceBigPageSize(bool extended, u32 requested) {
    // The native SDK passes its preferred allocation size here. A preference for
    // ordinary 4 KiB mappings still needs a separate large-page pool in NVDRV.
    return extended && requested == 0x1000 ? 0x10000 : requested;
}

constexpr bool IsSupportedNativeAddressSpaceView(u32 flags, u32 page_size, u32 reserved,
                                                  GpuAddressSpaceLayout requested,
                                                  GpuAddressSpaceLayout parent) {
    const bool small_pages_only = (flags == 5 && requested.split == 0) ||
                                  (flags == 0 && requested.split == requested.end);
    return small_pages_only && page_size == 0x1000 && reserved == 0 &&
           requested.start >= parent.start && requested.start < requested.end &&
           requested.end <= parent.split && (requested.start & 0xfff) == 0 &&
           (requested.end & 0xfff) == 0;
}

constexpr u64 GpuPageAddress(u32 page, u32 page_bits) {
    return static_cast<u64>(page) << page_bits;
}

} // namespace Service::Nvidia::Devices
