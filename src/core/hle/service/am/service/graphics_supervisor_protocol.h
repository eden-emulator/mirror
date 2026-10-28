// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <optional>
#include <span>
#include "common/common_types.h"

namespace Service::AM::GraphicsSupervisorProtocol {

constexpr u8 DecodeBufferSlot(u32 packed) {
    return static_cast<u8>(packed);
}

struct BootstrapRequest {
    u32 process_handle;
    u64 applet_resource_user_id;
    u64 flags;
    std::array<u64, 4> parameters;
};

inline std::optional<BootstrapRequest> DecodeBootstrap(std::span<const u32> packet) {
    // Captured remote-memory bootstrap: TIPC 4, one copied handle, 48 data bytes.
    if (packet.size() < 16 || packet[0] != 0x14 || packet[1] != 0x8000000c ||
        packet[2] != 2) {
        return std::nullopt;
    }
    const auto Read64 = [&](size_t index) {
        return u64{packet[index]} | (u64{packet[index + 1]} << 32);
    };
    return BootstrapRequest{packet[3], Read64(4), Read64(6),
                            {Read64(8), Read64(10), Read64(12), Read64(14)}};
}

} // namespace Service::AM::GraphicsSupervisorProtocol
