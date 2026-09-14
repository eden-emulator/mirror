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

struct OpusGenericDecodeParams {
    AVPacket* pkt = nullptr;
    AVFrame* frame = nullptr;
    u8* tmp_buf[8] = {nullptr};
};

struct OpusGenericDecodeObject {
    static u32 GetWorkBufferSizeMultistream(u32 total_stream_count, u32 stereo_stream_count) {
        if (IsValidStreamCounts(total_stream_count, stereo_stream_count))
            return 32 + 2556 * (total_stream_count * stereo_stream_count);
        return 0;
    }

    static u32 GetWorkBufferSize(u32 channel_count) {
        if (channel_count == 1 || channel_count == 2)
            return 32 + 2556 * channel_count;
        return 0;
    }

    /// idempotency of initialize is guaranteed
    Result InitializeDecoder(u32 sample_rate, u32 total_stream_count, u32 channel_count, u32 stereo_stream_count, u8 const* mappings) {
        LOG_DEBUG(Audio_DSP, "sample_rate={}, total_stream_count={}, channel_count={}, stereo_stream_count={}, mappings={}", sample_rate, total_stream_count, channel_count, stereo_stream_count, fmt::ptr(mappings));
        ASSERT(channel_count >= 1 && channel_count <= 2);
        // prefer libopus, ffmpeg docs say to use libopus **if** available
        // However, native opus can also work with swrescale:
        // it uses planarfloat, we can resample to s16
        AVCodec const* codec = avcodec_find_decoder_by_name("libopus");
        bool is_libopus = codec != nullptr;
        if (!codec) {
            LOG_WARNING(Audio_DSP, "using ffmpeg native opus decoder");
            codec = avcodec_find_decoder(AV_CODEC_ID_OPUS);
        }
        ASSERT(!(avc && avcodec_is_open(avc)));
        if ((avc = avcodec_alloc_context3(codec)) != nullptr) {
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
            avc->max_samples = 0x10000; //64K
            av_channel_layout_default(&avc->ch_layout, channel_count);
            if (avcodec_open2(avc, codec, nullptr) >= 0) {
                ASSERT(avc->request_sample_fmt == AV_SAMPLE_FMT_S16);
                // opus fltp -> libopus s16
                if (swr_alloc_set_opts2(
                    &swr,
                    &avc->ch_layout, AV_SAMPLE_FMT_S16, avc->sample_rate,
                    &avc->ch_layout, (enum AVSampleFormat)avc->sample_fmt, avc->sample_rate,
                    0, nullptr) >= 0) {
                    if (swr_init(swr) >= 0) {
                        return ResultSuccess;
                    }
                }
            }
        }
        Shutdown();
        return Service::Audio::ResultLibOpusInternalError;
    }

    Result Shutdown() {
        LOG_DEBUG(Audio_DSP, "called avc={}", fmt::ptr(avc));
        swr_free(&swr);
        avcodec_free_context(&avc);
        return ResultSuccess;
    }

    Result ResetDecoder() {
        LOG_DEBUG(Audio_DSP, "called avc={}", fmt::ptr(avc));
        if (avc) {
            if (avcodec_is_open(avc)) avcodec_flush_buffers(avc);
            return ResultSuccess;
        }
        return Service::Audio::ResultLibOpusInvalidState;
    }

    Result Decode(u32& out_sample_count, u64 output_data, u64 output_data_size, u64 input_data, u64 input_data_size, OpusGenericDecodeParams params) {
        LOG_DEBUG(Audio_DSP, "called out_sample_count={},output_data={:#x},output_data_size={},input_data={:#x},input_data_size={}", fmt::ptr(&out_sample_count), output_data, output_data_size, input_data, input_data_size);
        ASSERT(avc && avcodec_is_open(avc));
        out_sample_count = 0;
        int r;
        LOG_DEBUG(Audio_DSP, "old packet data={}", fmt::ptr(params.pkt->data));
        if ((r = av_new_packet(params.pkt, int(input_data_size))) >= 0) {
            LOG_DEBUG(Audio_DSP, "new packet data={}", fmt::ptr(params.pkt->data));
            std::memcpy(params.pkt->data, reinterpret_cast<const u8*>(input_data), input_data_size);

            r = avcodec_send_packet(avc, params.pkt);
            av_packet_unref(params.pkt);
            if (r >= 0) {
                while ((r = avcodec_receive_frame(avc, params.frame)) >= 0) {
                    LOG_DEBUG(Audio_DSP, "frame data={},data[0]={},nb_samples={}", fmt::ptr(params.frame->data), fmt::ptr(params.frame->data[0]), params.frame->nb_samples);
                    ASSERT(std::in_range<u16>(params.frame->nb_samples));
                    u8 *dst_arr[2] = {reinterpret_cast<u8*>(output_data), nullptr};
                    if (params.frame->format == avc->request_sample_fmt) {
                        // input_bsize == output_bsize
                        av_samples_copy(dst_arr, params.frame->data, out_sample_count, 0, params.frame->nb_samples, params.frame->ch_layout.nb_channels, (enum AVSampleFormat)params.frame->format);
                        out_sample_count += params.frame->nb_samples;
                    } else {
                        int out_samples = int(av_rescale_rnd(swr_get_delay(swr, params.frame->sample_rate) + params.frame->nb_samples, params.frame->sample_rate, params.frame->sample_rate, AV_ROUND_UP));
                        out_samples = swr_convert(swr, params.tmp_buf, out_samples, (const u8 **)params.frame->data, params.frame->nb_samples);
                        av_samples_copy(dst_arr, params.tmp_buf, out_sample_count, 0, out_samples, params.frame->ch_layout.nb_channels, avc->request_sample_fmt);
                        out_sample_count += out_samples;
                    }
                    av_frame_unref(params.frame);
                }
                if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) {
                    LOG_DEBUG(Audio_DSP, "{}", r); // non-errors
                } else if (r < 0) {
                    LOG_ERROR(Audio_DSP, "{}", r); // errors
                }
                return ResultSuccess;
            }
        }
        LOG_ERROR(Audio_DSP, "{}", r);
        return Service::Audio::ResultLibOpusInvalidState;
    }

    AVCodecContext* avc = nullptr;
    SwrContext* swr = nullptr;
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
        // Staging buffers used by various decoders
        // 1 <= channels <= 2, then, UPB channels => 2
        // 64K is enough for most
        OpusGenericDecodeParams params{};
        av_samples_alloc(params.tmp_buf, nullptr, 2, (int)0x10000, AV_SAMPLE_FMT_S16, 0);
        params.pkt = av_packet_alloc();
        params.frame = av_frame_alloc();

        while (!stop_token.stop_requested()) {
            auto msg = Receive(Direction::DSP, stop_token);
            LOG_DEBUG(Audio_DSP, "msg={}, buffer={}", msg, shared_memory->host_send_data[0]);
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
                        res = it->second.Decode(decoded_samples, output_data, output_data_size, input_data, input_data_size, params);

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
                        res = it->second.Decode(decoded_samples, output_data, output_data_size, input_data, input_data_size, params);

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
        av_freep(params.tmp_buf);
        av_frame_free(&params.frame);
        av_packet_free(&params.pkt);
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
