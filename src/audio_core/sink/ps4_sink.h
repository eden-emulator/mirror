// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <vector>

#include "audio_core/sink/sink.h"

namespace Core {
class System;
}

namespace AudioCore::Sink {
class SinkStream;

/// @brief PS4 backend sink, holds multiple output streams and is responsible for sinking samples to
/// hardware. Used by Audio Render, Audio In and Audio Out.
struct PS4Sink final : public Sink {
    explicit PS4Sink(std::string_view device_id);
    ~PS4Sink() override;
    SinkStream* AcquireSinkStream(Core::System& system, u32 system_channels, const std::string& name, StreamType type) override;
    void CloseStream(SinkStream* stream) override;
    void CloseStreams() override;
    f32 GetDeviceVolume() const override;
    void SetDeviceVolume(f32 volume) override;
    void SetSystemVolume(f32 volume) override;
    /// Name of the output device used by streams
    std::string output_device;
    /// Name of the input device used by streams
    std::string input_device;
    /// Vector of streams managed by this sink
    std::vector<SinkStreamPtr> sink_streams;
};

std::vector<std::string> ListPS4SinkDevices(bool capture);
u32 GetPS4Latency();

} // namespace AudioCore::Sink
