// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <optional>
#include <span>
#include "common/common_types.h"

namespace Service::AM::GraphicsSupervisorProtocol {

constexpr size_t RequestGroupCount = 8;

enum class RequestGroupDuplicateStatus {
    Ready,
    OutOfRange,
    NotConfigured,
    MissingEvent,
    IdentityMismatch,
};

struct RequestGroupDuplicateRequest {
    u32 copied_handle;
    u32 group;
};

inline std::optional<RequestGroupDuplicateRequest> DecodeRequestGroupEventDuplicate(
    std::span<const u32> packet) {
    if (packet.size() < 5 || packet[0] != 0x30 || packet[1] != 0x80000001 ||
        packet[2] != 2) {
        return std::nullopt;
    }
    return RequestGroupDuplicateRequest{packet[3], packet[4]};
}

constexpr RequestGroupDuplicateStatus ClassifyRequestGroupEventDuplicate(
    u32 group, bool configured, bool exists, bool identity_matches) {
    if (group >= RequestGroupCount) {
        return RequestGroupDuplicateStatus::OutOfRange;
    }
    if (!configured) {
        return RequestGroupDuplicateStatus::NotConfigured;
    }
    if (!exists) {
        return RequestGroupDuplicateStatus::MissingEvent;
    }
    if (!identity_matches) {
        return RequestGroupDuplicateStatus::IdentityMismatch;
    }
    return RequestGroupDuplicateStatus::Ready;
}

struct SyncpointWait {
    u32 slot;
    u32 id;
    u64 threshold;
    bool wide;
};

inline std::optional<SyncpointWait> DecodeSyncpointWait(std::span<const u32> packet) {
    if (packet.size() < 7 || packet[0] != 0xd9 || packet[1] != 5 ||
        (packet[6] & 0xff) > 1) return {};
    return SyncpointWait{packet[2], ~packet[3],
                        u64{packet[4]} | (u64{packet[5]} << 32), bool(packet[6] & 0xff)};
}

inline bool IsSyncpointWaitSatisfied(u32 current, u64 threshold, bool wide) {
    if (wide) return u64{current} >= threshold;
    return ((current - static_cast<u32>(threshold)) & 0x80000000U) == 0;
}

inline std::optional<u32> DecodeVideoImageRegistration(std::span<const u32> packet) {
    if (packet.size() < 6 || packet[0] != 0x147 || packet[1] != 4 || packet[2] == 0 ||
        packet[3] || packet[4] || packet[5]) return {};
    return packet[2];
}

struct VideoDecoderConfiguration {
    u32 width;
    u32 height;
    u32 frame_rate;
    u32 input_capacity;
};

inline std::optional<VideoDecoderConfiguration> DecodeVideoDecoderConfiguration(
    std::span<const u32> packet) {
    if (packet.size() < 13 || packet[0] != 0x100075 || packet[1] != 8 ||
        packet[5] == 0 || packet[5] > 65535 || packet[6] == 0 || packet[6] > 65535 ||
        packet[7] == 0 || packet[7] > 60 || packet[8] == 0 ||
        std::any_of(packet.begin() + 9, packet.begin() + 13, [](u32 value) { return value != 0; })) return {};
    return VideoDecoderConfiguration{packet[5], packet[6], packet[7], packet[8]};
}

struct VideoDecoderInitialization {
    u64 memory_size;
    u32 codec;
    u32 input_format;
    u32 output_format;
    u32 output_mode;
};

inline std::optional<VideoDecoderInitialization> DecodeVideoDecoderInitialization(
    std::span<const u32> packet) {
    if (packet.size() < 10 || packet[0] != 0xd9 || packet[1] != 0x80000006 || packet[2] != 2) return {};
    return VideoDecoderInitialization{u64{packet[4]} | (u64{packet[5]} << 32),
                                      packet[6], packet[7], packet[8], packet[9]};
}

// Command 200 (gsv81 0x264fc, same implementation as 201): header 0xd8, five raw words, one copied transfer
// memory handle: the u64 memory size, then three u32 (the SDK helper hard-codes the first as 1, the other
// two are the input and output formats). The server passes output mode 1 itself.
inline std::optional<VideoDecoderInitialization> DecodeVideoDecoderInitialization200(
    std::span<const u32> packet) {
    if (packet.size() < 9 || packet[0] != 0xd8 || packet[1] != 0x80000005 || packet[2] != 2) return {};
    return VideoDecoderInitialization{u64{packet[4]} | (u64{packet[5]} << 32),
                                      packet[6], packet[7], packet[8], 1};
}

struct VideoContextRequest {
    bool remote_memory;
    u64 library_version;
};

inline std::optional<VideoContextRequest> DecodeVideoContext(std::span<const u32> packet) {
    if (packet.size() < 5 || packet[0] != 0x39a || packet[1] != 3 || packet[2] > 1) return {};
    return VideoContextRequest{packet[2] != 0, u64{packet[3]} | (u64{packet[4]} << 32)};
}

constexpr u8 DecodeBufferSlot(u32 packed) {
    return static_cast<u8>(packed);
}

struct BootstrapRequest {
    u32 process_handle;
    u64 applet_resource_user_id;
    u64 flags;
    std::array<u64, 4> parameters;
};

struct DisplayParameter261Request {
    u64 display_name;
    u32 value;
};

inline std::optional<DisplayParameter261Request> DecodeDisplayParameter261(
    std::span<const u32> packet) {
    if (packet.size() < 5 || packet[0] != 0x115 || packet[1] != 3) {
        return std::nullopt;
    }
    return DisplayParameter261Request{u64{packet[2]} | (u64{packet[3]} << 32), packet[4]};
}

// Command 263 (Welcome Tour) has the same wrapper shape as 261: header 0x117, the u64 display name
// and one u32 value, with a result-only response.
inline std::optional<DisplayParameter261Request> DecodeDisplayParameter263(
    std::span<const u32> packet) {
    if (packet.size() < 5 || packet[0] != 0x117 || packet[1] != 3) {
        return std::nullopt;
    }
    return DisplayParameter261Request{u64{packet[2]} | (u64{packet[3]} << 32), packet[4]};
}

// Command 301: header 0x13d, the u64 display name, a u64 value and a u64 mask. The real server
// updates the display flags as (flags & ~mask) | (value & mask) (gsv-independent visrv2 handler at
// vi2.autogen 0xf37e0). The SDK was seen setting bit 63 (Yoshi: value = mask = 1 << 63) and
// clearing it (Welcome Tour: value 0, mask 1 << 63); only those two forms are accepted. The
// response is a result only.
constexpr bool IsSupportedDisplayParameter301(std::span<const u32> packet) {
    constexpr u64 DefaultDisplayName = 0x00746c7561666564ULL;
    constexpr u64 Bit63 = 1ULL << 63;
    if (packet.size() < 8 || packet[0] != 0x13d || packet[1] != 6) {
        return false;
    }
    const u64 value = u64{packet[4]} | (u64{packet[5]} << 32);
    const u64 mask = u64{packet[6]} | (u64{packet[7]} << 32);
    return (u64{packet[2]} | (u64{packet[3]} << 32)) == DefaultDisplayName && mask == Bit63 &&
           (value == 0 || value == Bit63);
}

constexpr bool IsSupportedDisplayParameter261(const DisplayParameter261Request& request) {
    constexpr u64 DefaultDisplayName = 0x00746c7561666564ULL;
    return request.display_name == DefaultDisplayName && request.value == 0;
}

// TIPC command 1111 (DragXDrive): header 0x467 with a single data word. The SDK wrapper selects the
// word from a four-entry table (0, 0x11e, 0x13c, 0x178) by a caller index below 4, and the response
// carries only a result.
constexpr bool IsSupportedCommand1111(std::span<const u32> packet) {
    if (packet.size() < 3 || packet[0] != 0x467 || packet[1] != 1) {
        return false;
    }
    return packet[2] == 0 || packet[2] == 0x11e || packet[2] == 0x13c || packet[2] == 0x178;
}

// Commands 1201, 1202 and 1203 carry no payload (the SDK writes the bare header, 16 + command) and
// the SDK issues them as one sequence. 1202 additionally returns one handle before the result.
constexpr bool IsSupportedSequenceCommand(std::span<const u32> packet, u32 command) {
    return (command == 1201 || command == 1202 || command == 1203) && packet.size() >= 2 &&
           packet[0] == 16 + command && packet[1] == 0;
}

// The session returned by 1202 takes command 11 with one count word and returns one handle.
constexpr std::optional<u32> DecodeEventSupervisorCreate(std::span<const u32> packet) {
    if (packet.size() < 3 || packet[0] != 0x1b || packet[1] != 1) {
        return std::nullopt;
    }
    return packet[2];
}

struct EventSupervisorBind {
    u64 name;
    u32 slot;
};

// Command 103 of the same session carries a 64-bit name and a slot index (three data words) and
// returns only a result.
constexpr std::optional<EventSupervisorBind> DecodeEventSupervisorBind(std::span<const u32> packet) {
    if (packet.size() < 5 || packet[0] != 0x77 || packet[1] != 3) {
        return std::nullopt;
    }
    return EventSupervisorBind{(static_cast<u64>(packet[3]) << 32) | packet[2], packet[4]};
}

constexpr bool IsSupportedDisplaySession(u64 flags) {
    return flags == 0x300020000ULL || flags == 0x300030000ULL ||
           flags == 0x300040000ULL;
}

constexpr u32 NativeGpuProfileForBootstrap(u64 flags) {
    switch (flags) {
    case 0x8000000000000005ULL: return 0x807;
    case 0x8000000000000006ULL:
    case 0x8000000000000007ULL: return 0x808;
    default: return 0;
    }
}

inline bool IsLocalBootstrap(const BootstrapRequest& request) {
    return (request.flags == 0x8000000000000005ULL ||
            request.flags == 0x8000000000000006ULL || request.flags == 0x8000000000000007ULL) &&
           std::ranges::all_of(request.parameters, [](u64 value) { return value == 0; });
}

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
