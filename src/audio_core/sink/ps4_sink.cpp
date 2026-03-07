// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <span>
#include <stop_token>
#include <vector>

#include <orbis/AudioOut.h>

#include "audio_core/common/common.h"
#include "audio_core/sink/ps4_sink.h"
#include "audio_core/sink/sink_stream.h"
#include "common/logging/log.h"
#include "common/scope_exit.h"
#include "core/core.h"

namespace AudioCore::Sink {

/// @brief PS4 sink stream, responsible for sinking samples to hardware.
struct PS4SinkStream final : public SinkStream {
    /// @brief Create a new sink stream.
    /// @param device_channels_ - Number of channels supported by the hardware.
    /// @param system_channels_ - Number of channels the audio systems expect.
    /// @param output_device - Name of the output device to use for this stream.
    /// @param input_device - Name of the input device to use for this stream.
    /// @param type_ - Type of this stream.
    /// @param system_ - Core system.
    /// @param event - Event used only for audio renderer, signalled on buffer consume.
    PS4SinkStream(u32 device_channels_, u32 system_channels_, const std::string& output_device, const std::string& input_device, StreamType type_, Core::System& system_)
        : SinkStream{system_, type_}
    {
        system_channels = system_channels_;
        device_channels = device_channels_;

        auto const length = 0x800;
        auto const sample_rate = 48000;
        auto const num_channels = this->GetDeviceChannels();
        output_buffer.resize(length * num_channels * sizeof(s16));

        auto const param_type = num_channels == 1 ? ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_MONO : ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_STEREO;
        audio_dev = sceAudioOutOpen(ORBIS_USER_SERVICE_USER_ID_SYSTEM, ORBIS_AUDIO_OUT_PORT_TYPE_MAIN, 0, length, sample_rate, param_type);
        if (audio_dev > 0) {
            audio_thread = std::jthread([=, this](std::stop_token stop_token) {
                while (!stop_token.stop_requested()) {
                    if (this->type == StreamType::In) {
                        // this->ProcessAudioIn(input_buffer, length);
                    } else {
                        sceAudioOutOutput(audio_dev, nullptr);
                        this->ProcessAudioOutAndRender(output_buffer, length);
                        sceAudioOutOutput(audio_dev, output_buffer.data());
                    }
                }
            });
        } else {
            LOG_ERROR(Service_Audio, "Failed to create audio device! {:#x}", uint32_t(audio_dev));
        }
    }

    ~PS4SinkStream() override {
        LOG_DEBUG(Service_Audio, "Destroying PS4 stream {}", name);
        sceAudioOutClose(audio_dev);
        if (audio_thread.joinable()) {
            audio_thread.request_stop();
            audio_thread.join();
        }
    }

    void Finalize() override {
        if (audio_dev > 0) {
            Stop();
            sceAudioOutClose(audio_dev);
        }
    }

    void Start(bool resume = false) override {
        if (audio_dev > 0 && paused) {
            paused = false;
        }
    }

    void Stop() override {
        if (audio_dev > 0 && !paused) {

        }
    }

    std::vector<s16> output_buffer;
    std::jthread audio_thread;
    int32_t audio_dev{};
};

PS4Sink::PS4Sink(std::string_view target_device_name) {
    int32_t rc = sceAudioOutInit();
    if (rc == 0 || unsigned(rc) == ORBIS_AUDIO_OUT_ERROR_ALREADY_INIT) {
        if (target_device_name != auto_device_name && !target_device_name.empty()) {
            output_device = target_device_name;
        } else {
            output_device.clear();
        }
        device_channels = 2;
    } else {
        LOG_ERROR(Service_Audio, "Unable to open audio out! {:#x}", uint32_t(rc));
    }
}

PS4Sink::~PS4Sink() = default;

/// @brief Create a new sink stream.
/// @param system          - Core system.
/// @param system_channels - Number of channels the audio system expects. May differ from the device's channel count.
/// @param name            - Name of this stream.
/// @param type            - Type of this stream, render/in/out.
/// @return A pointer to the created SinkStream
SinkStream* PS4Sink::AcquireSinkStream(Core::System& system, u32 system_channels_, const std::string&, StreamType type) {
    system_channels = system_channels_;
    SinkStreamPtr& stream = sink_streams.emplace_back(std::make_unique<PS4SinkStream>(device_channels, system_channels, output_device, input_device, type, system));
    return stream.get();
}

void PS4Sink::CloseStream(SinkStream* stream) {
    for (size_t i = 0; i < sink_streams.size(); i++) {
        if (sink_streams[i].get() == stream) {
            sink_streams[i].reset();
            sink_streams.erase(sink_streams.begin() + i);
            break;
        }
    }
}

void PS4Sink::CloseStreams() {
    sink_streams.clear();
}

f32 PS4Sink::GetDeviceVolume() const {
    return sink_streams.size() > 0 ? sink_streams[0]->GetDeviceVolume() : 1.f;
}

void PS4Sink::SetDeviceVolume(f32 volume) {
    for (auto& stream : sink_streams)
        stream->SetDeviceVolume(volume);
}

void PS4Sink::SetSystemVolume(f32 volume) {
    for (auto& stream : sink_streams)
        stream->SetSystemVolume(volume);
}

std::vector<std::string> ListPS4SinkDevices(bool capture) {
    return {{"Default"}};
}

u32 GetPS4Latency() {
    return TargetSampleCount * 2;
}

} // namespace AudioCore::Sink
