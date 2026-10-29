// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <optional>
#include <span>
#include "common/common_types.h"
#include "common/swap.h"
#include "core/hle/service/nvdrv/devices/native_sparse_remap.h"

namespace Service::AM {
struct GpuSparseMapRequest {
    u64_le address;
    u64_le size;
    u8 read_only;
    u8 cache;
    u8 reserved0;
    u8 kind;
    u8 fallback_kind;
    u8 comptag_mode;
    u16_le reserved1;
    u32_le memory;
    u32_le reserved2;
    u64_le source_offset;
};
static_assert(sizeof(GpuSparseMapRequest) == 40);

inline std::optional<Nvidia::Devices::NativeSparseRemapEntry> DecodeGpuSparseMap(
    const GpuSparseMapRequest& request) {
    if (!request.size || (u64{request.address} | u64{request.size} |
        u64{request.source_offset}) & 0xfff || request.cache > 1 || request.read_only ||
        request.comptag_mode || request.reserved0 || request.reserved1 || request.reserved2) {
        return std::nullopt;
    }
    Nvidia::Devices::NativeSparseRemapEntry entry{};
    // SDK 46d68c..46d6a4 converts ioctl bit7 to byte16 and cacheable bit2 to byte17.
    entry.flags = 0x1000 | (request.cache ? 4 : 0);
    entry.kind = request.kind;
    entry.fallback_kind = request.fallback_kind;
    entry.handle = request.memory;
    entry.source_pages = u64{request.source_offset} >> 12;
    entry.destination_pages = u64{request.address} >> 12;
    entry.pages = u64{request.size} >> 12;
    if (!Nvidia::Devices::DecodeNativeSparseRemap(entry)) return std::nullopt;
    return entry;
}

struct GpuMemoryRegistration {
    u64 begin;
    u64 end;
    u8 flags;
    u8 device;
};

constexpr std::optional<GpuMemoryRegistration> DecodeGpuMemoryRegistration(std::span<const u32> packet) {
    if (packet.size() < 8 || packet[0] != 0x75 || packet[1] != 6) return std::nullopt;
    // The SDK writes STRB at offsets 0x18 and 0x1c, leaving three padding bytes.
    return GpuMemoryRegistration{u64{packet[2]} | (u64{packet[3]} << 32),
        u64{packet[4]} | (u64{packet[5]} << 32), static_cast<u8>(packet[6]), static_cast<u8>(packet[7])};
}
struct GpuSparseRegionRequest {
    u32 address_space;
    u64 begin;
    u64 end;
    u8 page_shift;
    u8 allocation_mode;
    u8 device;
    u8 sync;
    u32 notification;
};

constexpr std::optional<GpuSparseRegionRequest> DecodeGpuSparseRegion(
    std::span<const u32> packet) {
    if (packet.size() < 12 || packet[0] != 0xe6 || packet[1] != 10) return std::nullopt;
    return GpuSparseRegionRequest{packet[2], u64{packet[3]} | (u64{packet[4]} << 32),
        u64{packet[5]} | (u64{packet[6]} << 32), static_cast<u8>(packet[7]),
        static_cast<u8>(packet[8]), static_cast<u8>(packet[9]), static_cast<u8>(packet[10]),
        packet[11]};
}

constexpr bool IsValidGpuSparseRegionRange(const GpuSparseRegionRequest& request) {
    if ((request.page_shift != 12 && request.page_shift != 16) || request.end < request.begin ||
        request.end == ~u64{0}) return false;
    const u64 mask = (u64{1} << request.page_shift) - 1;
    return (request.begin & mask) == 0 && ((request.end + 1) & mask) == 0;
}
} // namespace Service::AM
