// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <span>
#include <cstring>
#include <algorithm>
#include "common/swap.h"
#include "core/hle/service/nvdrv/devices/native_gpu_characteristics.h"
#include "common/common_types.h"

namespace Service::Nvidia::Devices {

struct NativeTsgBinding {
    s32 channel_fd;
    u32 subcontext_id;
};

inline std::optional<NativeTsgBinding> DecodeNativeTsgBinding(std::span<const u8> input,
                                                            bool extended) {
    if (input.size() != (extended ? 24U : 4U)) {
        return std::nullopt;
    }
    s32_le fd;
    std::memcpy(&fd, input.data(), sizeof(fd));
    u32 subcontext{};
    if (extended) {
        u32_le id;
        std::memcpy(&id, input.data() + 4, sizeof(id));
        subcontext = id;
        if (subcontext >= NativeGpuSubcontextCount ||
            std::any_of(input.begin() + 8, input.end(), [](u8 value) { return value != 0; })) {
            return std::nullopt;
        }
    }
    if (static_cast<s32>(fd) < 0) {
        return std::nullopt;
    }
    return NativeTsgBinding{fd, subcontext};
}

// Native profile context identities. This classifies allocation metadata;
// engine method execution is handled separately by the GPU frontend.
inline std::optional<u32> GetNativeGpuContextSlot(u32 identity) {
    switch (identity) {
    case 0xc797: return 1;
    case 0xc7c0: return 2;
    case 0xc7b5: return 4;
    case 0xc56f: return 5;
    default: return std::nullopt;
    }
}

} // namespace Service::Nvidia::Devices
