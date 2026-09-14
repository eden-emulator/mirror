// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>

extern "C" {
#include <libswresample/swresample.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/codec.h>
#include <libavcodec/packet.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
}

#include "audio_core/adsp/apps/opus/shared_memory.h"
#include "audio_core/audio_core.h"
#include "common/logging.h"
#include "common/thread.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/hle/service/audio/errors.h"

namespace AudioCore::ADSP::OpusDecoder {

namespace {
constexpr u32 OPUS_STREAM_COUNT_MAX = 255;
// https://git.ffmpeg.org/gitweb/ffmpeg.git/blob_plain/HEAD:/libavcodec/libopusdec.c
constexpr u32 OPUS_HEAD_SIZE = 19;
constexpr u32 OPUS_MAX_CHANNELS = 2;

bool IsValidChannelCount(u32 channel_count) {
    return channel_count >= 1 || channel_count <= OPUS_MAX_CHANNELS;
}

bool IsValidStreamCounts(u32 total_stream_count, u32 stereo_stream_count) {
    return total_stream_count > 0 && total_stream_count <= OPUS_STREAM_COUNT_MAX
        && s32(stereo_stream_count) >= 0 && stereo_stream_count <= total_stream_count;
}

class OpusGenericDecodeObject {
public:
    static u32 GetWorkBufferSizeMultistream(u32 total_stream_count, u32 stereo_stream_count) {
        if (IsValidStreamCounts(total_stream_count, stereo_stream_count))
            return 48 + 2556 * (total_stream_count * stereo_stream_count);
        return 0;
    }

    static u32 GetWorkBufferSize(u32 channel_count) {
        if (channel_count == 1 || channel_count == 2)
            return 48 + 16 * channel_count;
        return 0;
    }

    /// idempotency of initialize is guaranteed
    Result InitializeDecoder(u32 sample_rate, u32 total_stream_count, u32 channel_count, u32 stereo_stream_count, u8 const* mappings) {
        // prefer libopus, ffmpeg docs say to use libopus **if** available
        // However, native opus can also work with swrescale:
        // it uses planarfloat, we can resample to s16
        AVCodec const* codec = avcodec_find_decoder_by_name("libopus");
        bool is_libopus = codec != nullptr;
        if (!codec) {
            LOG_WARNING(Audio_DSP, "using ffmpeg native opus decoder");
            codec = avcodec_find_decoder(AV_CODEC_ID_OPUS);
        }
        if (codec) {
            if ((avc = avc ? avc : avcodec_alloc_context3(codec))) {
                if (is_libopus) {
                    const std::array<u8, 2> mapping_arr{0, 1};
                    mappings = mappings ? mappings : mapping_arr.data();

                    // freed by avcodec_context_free()
                    u8 *edata = reinterpret_cast<u8*>(av_mallocz(OPUS_HEAD_SIZE + 2 * OPUS_MAX_CHANNELS + AV_INPUT_BUFFER_PADDING_SIZE));
                    ASSERT(edata);
                    edata[9] = u8(channel_count); //channels
                    edata[10] = u8(0); //opus->pre_skip
                    edata[16] = u8(0); //gain_db
                    edata[18] = u8(0); //channel_map
                    edata[OPUS_HEAD_SIZE + 0] = u8(total_stream_count);
                    edata[OPUS_HEAD_SIZE + 1] = u8(stereo_stream_count);
                    if (channel_count >= 1) edata[OPUS_HEAD_SIZE + 2] = mappings[0];
                    if (channel_count >= 2) edata[OPUS_HEAD_SIZE + 3] = mappings[1];
                    avc->extradata = edata;
                    avc->extradata_size = OPUS_HEAD_SIZE + 2 * channel_count;
                }


                // FFmpeg hardcodes sample rate
                avc->sample_rate = sample_rate;
                avc->request_sample_fmt = AV_SAMPLE_FMT_S16;
                av_channel_layout_default(&avc->ch_layout, channel_count);
                if (avcodec_open2(avc, codec, nullptr) >= 0) {
                    avpkt = av_packet_alloc();
                    frame = av_frame_alloc();
                    return ResultSuccess;
                } else {
                    avcodec_free_context(&avc);
                }
            }
        }
        return Service::Audio::ResultLibOpusInternalError;
    }

    Result Shutdown() {
        avcodec_free_context(&avc);
        av_frame_free(&frame);
        av_packet_free(&avpkt);
        return ResultSuccess;
    }

    Result ResetDecoder() {
        if (avc) {
            if (avcodec_is_open(avc)) avcodec_flush_buffers(avc);
            return ResultSuccess;
        }
        return Service::Audio::ResultLibOpusInvalidState;
    }

    Result Decode(u32& out_sample_count, u64 output_data, u64 output_data_size, u64 input_data, u64 input_data_size) {
        out_sample_count = 0;
        if (avc) {
            int rem_output_bytes = int(output_data_size);
            while (rem_output_bytes > 0) {
                int r = avcodec_receive_frame(avc, frame);
                if (r == AVERROR(EAGAIN)) {
                    av_packet_unref(avpkt);
                    av_new_packet(avpkt, int(input_data_size));
                    std::memcpy(avpkt->data, reinterpret_cast<const u8*>(input_data), input_data_size);
                    r = avcodec_send_packet(avc, avpkt);
                    ASSERT(r >= 0);
                } else if (r == AVERROR_EOF) {
                    break;
                } else if (r >= 0) {
                    auto const bsize = av_samples_get_buffer_size(nullptr, frame->ch_layout.nb_channels, frame->nb_samples, AV_SAMPLE_FMT_S16, 1);
                    if (frame->format == AV_SAMPLE_FMT_S16) {
                        std::memcpy(reinterpret_cast<s16*>(output_data) + (int(output_data_size) - rem_output_bytes), frame->data[0], size_t(bsize));
                    } else {
                        SwrContext *swr = nullptr;
                        if (swr_alloc_set_opts2(
                            &swr,
                            &avc->ch_layout,
                            AV_SAMPLE_FMT_S16,
                            48000,
                            &avc->ch_layout,
                            (enum AVSampleFormat)frame->format,
                            48000,
                            0,
                            nullptr
                        ) >= 0) {
                            if (swr_init(swr) >= 0) {
                                AVFrame *s16_frame = av_frame_alloc();
                                s16_frame->format = AV_SAMPLE_FMT_S16;
                                s16_frame->sample_rate = frame->sample_rate;
                                av_channel_layout_copy(&s16_frame->ch_layout, &frame->ch_layout);
                                s16_frame->nb_samples = frame->nb_samples;
                                av_frame_get_buffer(s16_frame, 0);
                                swr_convert(swr, s16_frame->data, s16_frame->nb_samples, (const uint8_t **)frame->data, frame->nb_samples);
                                std::memcpy(reinterpret_cast<s16*>(output_data) + (int(output_data_size) - rem_output_bytes), s16_frame->data[0], size_t(bsize));
                                swr_free(&swr);
                            }
                        }
                    }
                    out_sample_count += frame->nb_samples;
                    rem_output_bytes -= bsize;
                } else {
                    LOG_ERROR(Audio_DSP, "{}", r);
                    break;
                }
            }
            ASSERT(rem_output_bytes == 0 && "remaining bytes!");
            return ResultSuccess;
        }
        return Service::Audio::ResultLibOpusInvalidState;
    }

    AVCodecContext* avc = nullptr;
    AVPacket* avpkt = nullptr;
    AVFrame* frame = nullptr;
};
} // namespace

OpusDecoder::OpusDecoder(Core::System& system) {
    dsp_thread = std::jthread([this, &system](std::stop_token stop_token) {
        Common::SetCurrentThreadName("DSP_OpusDecoder");
        if (Receive(Direction::DSP, stop_token) != Message::Start) {
            LOG_ERROR(Service_Audio, "DSP OpusDecoder failed to receive Start message. Opus initialization failed.");
            return;
        }
        Send(Direction::Host, Message::StartOK);

        // Main OpusDecoder thread, responsible for processing the incoming Opus packets.
        ::Common::unordered_map<u64, OpusGenericDecodeObject> decode_objects;
        while (!stop_token.stop_requested()) {
            auto msg = Receive(Direction::DSP, stop_token);
            switch (msg) {
            case Shutdown:
                Send(Direction::Host, Message::ShutdownOK);
                return;
            case GetWorkBufferSize: {
                auto channel_count = s32(shared_memory->host_send_data[0]);

                ASSERT(IsValidChannelCount(channel_count));

                shared_memory->dsp_return_data[0] = OpusGenericDecodeObject::GetWorkBufferSize(channel_count);
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
                ASSERT(buffer_size >= OpusGenericDecodeObject::GetWorkBufferSize(channel_count));

                if (auto const it = decode_objects.find(buffer); it != decode_objects.end()) {
                    it->second.Shutdown();
                    shared_memory->dsp_return_data[0] = it->second.InitializeDecoder(sample_rate, 1, channel_count, channel_count == 2 ? 1 : 0, nullptr).raw;
                } else {
                    OpusGenericDecodeObject obj{};
                    shared_memory->dsp_return_data[0] = obj.InitializeDecoder(sample_rate, 1, channel_count, channel_count == 2 ? 1 : 0, nullptr).raw;
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

                ASSERT(IsValidStreamCounts(total_stream_count, stereo_stream_count));

                shared_memory->dsp_return_data[0] = OpusGenericDecodeObject::GetWorkBufferSizeMultistream(total_stream_count, stereo_stream_count);
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

                ASSERT(IsValidStreamCounts(total_stream_count, stereo_stream_count));
                ASSERT(sample_rate >= 0);
                ASSERT(buffer_size >= OpusGenericDecodeObject::GetWorkBufferSizeMultistream(total_stream_count, stereo_stream_count));
                if (auto const it = decode_objects.find(buffer); it != decode_objects.end()) {
                    it->second.Shutdown();
                    shared_memory->dsp_return_data[0] = it->second.InitializeDecoder(sample_rate, total_stream_count, channel_count, stereo_stream_count, mappings).raw;
                } else {
                    OpusGenericDecodeObject obj{};
                    shared_memory->dsp_return_data[0] = obj.InitializeDecoder(sample_rate, total_stream_count, channel_count, stereo_stream_count, mappings).raw;
                    decode_objects.insert_or_assign(buffer, obj);
                }
                Send(Direction::Host, Message::InitializeMultiStreamDecodeObjectOK);
                break;
            }
            case ShutdownMultiStreamDecodeObject: {
                auto buffer = shared_memory->host_send_data[0];
                //[[maybe_unused]] auto buffer_size = shared_memory->host_send_data[1];
                if (auto const it = decode_objects.find(buffer); it != decode_objects.end()) {
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
                Send(Direction::Host, Message::DecodeInterleavedForMultiStreamOK);
                break;
            }
            default:
                LOG_ERROR(Audio_DSP, "Invalid OpusDecoder command {}", msg);
                continue;
            }
        }
        for (auto e : decode_objects)
            e.second.Shutdown();
    });
}

OpusDecoder::~OpusDecoder() {
    if (dsp_thread.joinable()) {
        // Shutdown the thread
        auto const stop_token = dsp_thread.get_stop_token();
        Send(Direction::DSP, Message::Shutdown);
        auto msg = Receive(Direction::Host, stop_token);
        ASSERT_MSG(msg == Message::ShutdownOK, "Expected Opus shutdown code {}, got {}", Message::ShutdownOK, msg);
        dsp_thread.request_stop();
        dsp_thread.join();
    }
}

void OpusDecoder::Send(Direction dir, u32 message) {
    mailbox.Send(dir, std::move(message));
}

u32 OpusDecoder::Receive(Direction dir, std::stop_token stop_token) {
    return mailbox.Receive(dir, stop_token);
}

} // namespace AudioCore::ADSP::OpusDecoder
