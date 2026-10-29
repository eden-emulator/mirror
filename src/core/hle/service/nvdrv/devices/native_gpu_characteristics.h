// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstring>
#include <span>
#include "common/common_types.h"
#include "common/swap.h"

namespace Service::Nvidia::Devices {

inline constexpr u32 NativeGpuSubcontextCount = 64;

inline constexpr size_t NativeGpuCharacteristicsSize = 0xd0;
inline constexpr size_t ExtendedNativeGpuCharacteristicsSize = 0xd8;

inline bool ApplyNativeGpuProfile(std::span<u8> characteristics, u32 version) {
    if ((version != 0x807 && version != 0x808) || characteristics.size() < 0x78) {
        return false;
    }
    const u32_le value{version};
    std::memcpy(characteristics.data() + 0x3c, &value, sizeof(value));
    std::memcpy(characteristics.data() + 0x40, &value, sizeof(value));
    const u16_le channel_level{static_cast<u16>(version == 0x808 ? 0x1f : 0x1d)};
    std::memcpy(characteristics.data() + 0x76, &channel_level, sizeof(channel_level));
    return true;
}

template <size_t Size = NativeGpuCharacteristicsSize>
inline std::array<u8, Size> MakeNativeGpuCharacteristics(
    std::span<const u8, 0xa0> legacy) {
    static_assert(Size == NativeGpuCharacteristicsSize ||
                  Size == ExtendedNativeGpuCharacteristicsSize);
    std::array<u8, Size> result{};
    // The GPU description and engine classes share the legacy prefix. The native
    // SDK consumes signed ioctl interface levels where the legacy ABI has topology.
    std::memcpy(result.data(), legacy.data(), 0x70);
    // Select the SDK's native control ABI, whose normal mappings support
    // 4 KiB CPU/GPU aliases. Engine execution remains a separate implementation.
    const u32_le native_arch{0x170};
    const u32_le native_impl{0xe};
    std::memcpy(result.data(), &native_arch, sizeof(native_arch));
    std::memcpy(result.data() + 4, &native_impl, sizeof(native_impl));
    // Preserve default profiles for unnegotiated native sessions. Graphics-supervisor
    // sessions select their shader profile independently of the reply size.
    const u32_le native_sm_version{Size == ExtendedNativeGpuCharacteristicsSize ? 0x808U : 0x807U};
    std::memcpy(result.data() + 0x3c, &native_sm_version, sizeof(native_sm_version));
    std::memcpy(result.data() + 0x40, &native_sm_version, sizeof(native_sm_version));
    // Native NVN maps CPU buffers into its shared VA reservation. Those buffers
    // are 4 KiB aligned, so large pages must remain optional for allocation.
    const u32_le preferred_page_size{0x1000};
    std::memcpy(result.data() + 0x28, &preferred_page_size, sizeof(preferred_page_size));
    u32_le available_page_sizes;
    std::memcpy(&available_page_sizes, legacy.data() + 0x34, sizeof(available_page_sizes));
    available_page_sizes = (static_cast<u32>(available_page_sizes) & 0x10000) | 0x1000;
    std::memcpy(result.data() + 0x34, &available_page_sizes, sizeof(available_page_sizes));
    u64_le flags;
    std::memcpy(&flags, legacy.data() + 0x50, sizeof(flags));
    // Native SDK class enumeration filters 2D/3D/compute/inline families
    // with bits 40..43 before selecting context identities from its profile.
    // The native SDK gates ModifyKind (AS ioctl 12) on capability bit 34.
    flags = static_cast<u64>(flags) | (1ULL << 8) | (1ULL << 22) | (1ULL << 26) | (1ULL << 34) | (1ULL << 35) | (0xfULL << 40);
    std::memcpy(result.data() + 0x50, &flags, sizeof(flags));
    constexpr std::array<s16, 9> levels{
        0x1c, // GPU control: GetGpuTime
        0xb,  // TSG: extended channel binding with subcontext identity
        -1,   // GPU debugger device is unavailable
        Size == ExtendedNativeGpuCharacteristicsSize ? 0x1f : 0x1d, // Channel interface
        0x14, // Address space: Remap
        0, 0, 0, 0, // Additional interfaces have not been implemented.
    };
    for (size_t i = 0; i < levels.size(); ++i) {
        const u16_le value{static_cast<u16>(levels[i])};
        std::memcpy(result.data() + 0x70 + i * sizeof(value), &value, sizeof(value));
    }
    const u32_le subcontext_count{NativeGpuSubcontextCount};
    std::memcpy(result.data() + 0xb4, &subcontext_count, sizeof(subcontext_count));
    return result;
}

} // namespace Service::Nvidia::Devices
