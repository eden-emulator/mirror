// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <thread>

#include "common/container/unordered_map.h"
#include "audio_core/adsp/apps/opus/shared_memory.h"
#include "audio_core/adsp/mailbox.h"
#include "common/common_types.h"
#include "core/hle/result.h"

namespace Core {
class System;
} // namespace Core

namespace AudioCore::ADSP::OpusDecoder {

enum Message : u32 {
    Invalid = 0,
    Start = 1,
    Shutdown = 2,
    StartOK = 11,
    ShutdownOK = 12,
    GetWorkBufferSize = 21,
    InitializeDecodeObject = 22,
    ShutdownDecodeObject = 23,
    DecodeInterleaved = 24,
    MapMemory = 25,
    UnmapMemory = 26,
    GetWorkBufferSizeForMultiStream = 27,
    InitializeMultiStreamDecodeObject = 28,
    ShutdownMultiStreamDecodeObject = 29,
    DecodeInterleavedForMultiStream = 30,

    GetWorkBufferSizeOK = 41,
    InitializeDecodeObjectOK = 42,
    ShutdownDecodeObjectOK = 43,
    DecodeInterleavedOK = 44,
    MapMemoryOK = 45,
    UnmapMemoryOK = 46,
    GetWorkBufferSizeForMultiStreamOK = 47,
    InitializeMultiStreamDecodeObjectOK = 48,
    ShutdownMultiStreamDecodeObjectOK = 49,
    DecodeInterleavedForMultiStreamOK = 50,
};

/// @brief The AudioRenderer application running on the ADSP.
class OpusDecoder {
public:
    explicit OpusDecoder(Core::System& system);
    ~OpusDecoder();

    bool IsRunning() const noexcept {
        return dsp_thread.joinable();
    }

    void Send(Direction dir, u32 message);
    u32 Receive(Direction dir, std::stop_token stop_token = {});

    void SetSharedMemory(SharedMemory& shared_memory_) {
        shared_memory = &shared_memory_;
    }

private:
    /// Mailbox to communicate messages with the host, drives the main thread
    Mailbox mailbox;
    /// Structure shared with the host, input data set by the host before sending a mailbox message,
    /// and the responses are written back by the OpusDecoder.
    SharedMemory* shared_memory{};
    std::jthread dsp_thread{};
};

} // namespace AudioCore::ADSP::OpusDecoder
