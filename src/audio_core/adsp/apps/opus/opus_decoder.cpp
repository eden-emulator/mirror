// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <chrono>

#include "audio_core/adsp/apps/opus/opus_decode_object.h"
#include "audio_core/adsp/apps/opus/opus_multistream_decode_object.h"
#include "audio_core/adsp/apps/opus/shared_memory.h"
#include "audio_core/audio_core.h"
#include "audio_core/common/common.h"
#include "common/logging.h"
#include "common/thread.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/hle/service/audio/errors.h"

namespace AudioCore::ADSP::OpusDecoder {

namespace {
constexpr size_t OpusStreamCountMax = 255;

bool IsValidChannelCount(u32 channel_count) {
    return channel_count == 1 || channel_count == 2;
}

bool IsValidMultiStreamChannelCount(u32 channel_count) {
    return channel_count <= OpusStreamCountMax;
}

bool IsValidMultiStreamStreamCounts(s32 total_stream_count, s32 stereo_stream_count) {
    return IsValidMultiStreamChannelCount(total_stream_count) && total_stream_count > 0 &&
           stereo_stream_count >= 0 && stereo_stream_count <= total_stream_count;
}
} // namespace

OpusDecoder::OpusDecoder(Core::System& system_) : system{system_} {
    init_thread = std::jthread([this](std::stop_token stop_token) { Init(stop_token); });
}

OpusDecoder::~OpusDecoder() {
    if (!running) {
        init_thread.request_stop();
        return;
    }

    // Shutdown the thread
    Send(Direction::DSP, Message::Shutdown);
    auto msg = Receive(Direction::Host);
    ASSERT_MSG(msg == Message::ShutdownOK, "Expected Opus shutdown code {}, got {}", Message::ShutdownOK, msg);
    main_thread.request_stop();
    main_thread.join();
    running = false;

    // Must shutdown as there are AV allocations which are manual
    for (auto& e : decode_objects)
        e.second.Shutdown();
    for (auto& e : ms_decode_objects)
        e.second.Shutdown();
}

void OpusDecoder::Send(Direction dir, u32 message) {
    mailbox.Send(dir, std::move(message));
}

u32 OpusDecoder::Receive(Direction dir, std::stop_token stop_token) {
    return mailbox.Receive(dir, stop_token);
}

void OpusDecoder::Init(std::stop_token stop_token) {
    Common::SetCurrentThreadName("DSP_OpusDecoder_Init");

    if (Receive(Direction::DSP, stop_token) != Message::Start) {
        LOG_ERROR(Service_Audio, "DSP OpusDecoder failed to receive Start message. Opus initialization failed.");
        return;
    }
    // Main OpusDecoder thread, responsible for processing the incoming Opus packets.
    main_thread = std::jthread([this](std::stop_token stop_token) {
        Common::SetCurrentThreadName("DSP_OpusDecoder_Main");
        while (!stop_token.stop_requested()) {
            auto msg = Receive(Direction::DSP, stop_token);
            switch (msg) {
            case Shutdown:
                Send(Direction::Host, Message::ShutdownOK);
                return;
            case GetWorkBufferSize: {
                auto channel_count = s32(shared_memory->host_send_data[0]);

                ASSERT(IsValidChannelCount(channel_count));

                shared_memory->dsp_return_data[0] = OpusDecodeObject::GetWorkBufferSize(channel_count);
                Send(Direction::Host, Message::GetWorkBufferSizeOK);
                break;
            }
            case InitializeDecodeObject: {
                auto buffer = shared_memory->host_send_data[0];
                auto buffer_size = shared_memory->host_send_data[1];
                auto sample_rate = s32(shared_memory->host_send_data[2]);
                auto channel_count = s32(shared_memory->host_send_data[3]);

                ASSERT(sample_rate >= 0);
                ASSERT(IsValidChannelCount(channel_count));
                ASSERT(buffer_size >= OpusDecodeObject::GetWorkBufferSize(channel_count));

                if (auto const it = decode_objects.find(buffer); it != decode_objects.end()) {
                    it->second.Shutdown();
                    shared_memory->dsp_return_data[0] = it->second.InitializeDecoder(sample_rate, channel_count).raw;
                } else {
                    OpusDecodeObject obj{};
                    shared_memory->dsp_return_data[0] = obj.InitializeDecoder(sample_rate, channel_count).raw;
                    decode_objects.insert_or_assign(buffer, obj);
                }
                Send(Direction::Host, Message::InitializeDecodeObjectOK);
                break;
            }
            case ShutdownDecodeObject: {
                auto buffer = shared_memory->host_send_data[0];
                //[[maybe_unused]] auto buffer_size = shared_memory->host_send_data[1];
                if (auto const it = decode_objects.find(buffer); it != decode_objects.end()) {
                    shared_memory->dsp_return_data[0] = it->second.Shutdown().raw;
                } else {
                    LOG_ERROR(Audio_DSP, "operating unregistered buffer {}", buffer);
                    shared_memory->dsp_return_data[0] = Service::Audio::ResultLibOpusInvalidState.raw;
                }
                Send(Direction::Host, Message::ShutdownDecodeObjectOK);
                break;
            }
            case DecodeInterleaved: {
                auto start_time = system.CoreTiming().GetGlobalTimeUs();

                auto buffer = shared_memory->host_send_data[0];
                auto input_data = shared_memory->host_send_data[1];
                auto input_data_size = shared_memory->host_send_data[2];
                auto output_data = shared_memory->host_send_data[3];
                auto output_data_size = shared_memory->host_send_data[4];
                //auto final_range = static_cast<u32>(shared_memory->host_send_data[5]);
                auto reset_requested = shared_memory->host_send_data[6];

                u32 decoded_samples{0};

                if (auto const it = decode_objects.find(buffer); it != decode_objects.end()) {
                    auto res = ResultSuccess;
                    if (reset_requested)
                        res = it->second.ResetDecoder();
                    if (res == ResultSuccess)
                        res = it->second.Decode(decoded_samples, output_data, output_data_size, input_data, input_data_size);

                    auto end_time = system.CoreTiming().GetGlobalTimeUs();
                    shared_memory->dsp_return_data[0] = res.raw;
                    shared_memory->dsp_return_data[1] = decoded_samples;
                    shared_memory->dsp_return_data[2] = (end_time - start_time).count();
                } else {
                    LOG_ERROR(Audio_DSP, "operating unregistered buffer {}", buffer);
                    shared_memory->dsp_return_data[0] = Service::Audio::ResultLibOpusInvalidState.raw;
                }
                Send(Direction::Host, Message::DecodeInterleavedOK);
                break;
            }
            case MapMemory: {
                [[maybe_unused]] auto buffer = shared_memory->host_send_data[0];
                [[maybe_unused]] auto buffer_size = shared_memory->host_send_data[1];
                Send(Direction::Host, Message::MapMemoryOK);
                break;
            }
            case UnmapMemory: {
                [[maybe_unused]] auto buffer = shared_memory->host_send_data[0];
                [[maybe_unused]] auto buffer_size = shared_memory->host_send_data[1];
                Send(Direction::Host, Message::UnmapMemoryOK);
                break;
            }
            case GetWorkBufferSizeForMultiStream: {
                auto total_stream_count = s32(shared_memory->host_send_data[0]);
                auto stereo_stream_count = s32(shared_memory->host_send_data[1]);

                ASSERT(IsValidMultiStreamStreamCounts(total_stream_count, stereo_stream_count));

                shared_memory->dsp_return_data[0] = OpusMultiStreamDecodeObject::GetWorkBufferSize(
                    total_stream_count, stereo_stream_count);
                Send(Direction::Host, Message::GetWorkBufferSizeForMultiStreamOK);
                break;
            }
            case InitializeMultiStreamDecodeObject: {
                auto buffer = shared_memory->host_send_data[0];
                auto buffer_size = shared_memory->host_send_data[1];
                auto sample_rate = s32(shared_memory->host_send_data[2]);
                auto channel_count = s32(shared_memory->host_send_data[3]);
                auto total_stream_count = s32(shared_memory->host_send_data[4]);
                auto stereo_stream_count = s32(shared_memory->host_send_data[5]);
                // Nintendo seem to have a bug here, they try to use &host_send_data[6] for the channel
                // mappings, but [6] is never set, and there is not enough room in the argument data for
                // more than 40 channels, when 255 are possible.
                // It also means the mapping values are undefined, though likely always 0,
                // and the mappings given by the game are ignored. The mappings are copied to this
                // dedicated buffer host side, so let's do as intended.
                auto mappings = shared_memory->channel_mapping.data();

                ASSERT(IsValidMultiStreamStreamCounts(total_stream_count, stereo_stream_count));
                ASSERT(sample_rate >= 0);
                ASSERT(buffer_size >= OpusMultiStreamDecodeObject::GetWorkBufferSize(total_stream_count, stereo_stream_count));

                if (auto const it = ms_decode_objects.find(buffer); it != ms_decode_objects.end()) {
                    it->second.Shutdown();
                    shared_memory->dsp_return_data[0] = it->second.InitializeDecoder(sample_rate, total_stream_count, channel_count, stereo_stream_count, mappings).raw;
                } else {
                    OpusMultiStreamDecodeObject obj{};
                    shared_memory->dsp_return_data[0] = obj.InitializeDecoder(sample_rate, total_stream_count, channel_count, stereo_stream_count, mappings).raw;
                    ms_decode_objects.insert_or_assign(buffer, obj);
                }
                Send(Direction::Host, Message::InitializeMultiStreamDecodeObjectOK);
                break;
            }
            case ShutdownMultiStreamDecodeObject: {
                auto buffer = shared_memory->host_send_data[0];
                //[[maybe_unused]] auto buffer_size = shared_memory->host_send_data[1];
                if (auto const it = ms_decode_objects.find(buffer); it != ms_decode_objects.end()) {
                    shared_memory->dsp_return_data[0] = it->second.Shutdown().raw;
                } else {
                    LOG_ERROR(Audio_DSP, "operating unregistered buffer {}", buffer);
                    shared_memory->dsp_return_data[0] = Service::Audio::ResultLibOpusInvalidState.raw;
                }
                Send(Direction::Host, Message::ShutdownMultiStreamDecodeObjectOK);
                break;
            }
            case DecodeInterleavedForMultiStream: {
                auto start_time = system.CoreTiming().GetGlobalTimeUs();

                auto buffer = shared_memory->host_send_data[0];
                auto input_data = shared_memory->host_send_data[1];
                auto input_data_size = shared_memory->host_send_data[2];
                auto output_data = shared_memory->host_send_data[3];
                auto output_data_size = shared_memory->host_send_data[4];
                //auto final_range = static_cast<u32>(shared_memory->host_send_data[5]);
                auto reset_requested = shared_memory->host_send_data[6];

                u32 decoded_samples{0};

                if (auto const it = ms_decode_objects.find(buffer); it != ms_decode_objects.end()) {
                    auto res = ResultSuccess;
                    if (reset_requested)
                        res = it->second.ResetDecoder();
                    if (res == ResultSuccess)
                        res = it->second.Decode(decoded_samples, output_data, output_data_size, input_data, input_data_size);

                    auto end_time = system.CoreTiming().GetGlobalTimeUs();
                    shared_memory->dsp_return_data[0] = res.raw;
                    shared_memory->dsp_return_data[1] = decoded_samples;
                    shared_memory->dsp_return_data[2] = (end_time - start_time).count();
                } else {
                    LOG_ERROR(Audio_DSP, "operating unregistered buffer {}", buffer);
                    shared_memory->dsp_return_data[0] = Service::Audio::ResultLibOpusInvalidState.raw;
                }
                Send(Direction::Host, Message::DecodeInterleavedForMultiStreamOK);
                break;
            }
            default:
                LOG_ERROR(Audio_DSP, "Invalid OpusDecoder command {}", msg);
                continue;
            }
        }
    });
    running = true;
    Send(Direction::Host, Message::StartOK);
}

} // namespace AudioCore::ADSP::OpusDecoder
