// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/codec.h>
#include <libavcodec/packet.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
}
#include "audio_core/adsp/apps/opus/opus_decode_object.h"
#include "audio_core/adsp/apps/opus/opus_types.h"
#include "common/assert.h"
#include "core/hle/service/audio/errors.h"

namespace AudioCore::ADSP::OpusDecoder {

u32 OpusDecodeObject::GetWorkBufferSize(u32 channel_count) {
    if (channel_count == 1 || channel_count == 2)
        return u32(sizeof(OpusDecodeObject)) + 16 * channel_count;
    return 0;
}

Result OpusDecodeObject::InitializeDecoder(u32 sample_rate, u32 channel_count) {
    if ((codec = avcodec_find_decoder(AV_CODEC_ID_OPUS)) == nullptr)
        return Service::Audio::ResultLibOpusInvalidState;
    if ((avctx = avcodec_alloc_context3(codec)) == nullptr)
        return Service::Audio::ResultLibOpusInvalidState;
    avctx->sample_rate = sample_rate;
    av_channel_layout_default(&avctx->ch_layout, channel_count);
    return ResultSuccess;
}

Result OpusDecodeObject::Shutdown() {
    if (avctx)
        avcodec_free_context(&avctx);
    return ResultSuccess;
}

Result OpusDecodeObject::ResetDecoder() {
    if (avctx) {
        avcodec_flush_buffers(avctx);
        return ResultSuccess;
    }
    return Service::Audio::ResultLibOpusInvalidState;
}

Result OpusDecodeObject::Decode(u32& out_sample_count, u64 output_data, u64 output_data_size, u64 input_data, u64 input_data_size) {
    out_sample_count = 0;
    if (avctx) {
        AVPacket* avpkt = av_packet_alloc();
        av_new_packet(avpkt, int(input_data_size));
        std::memcpy(avpkt->data, reinterpret_cast<const u8*>(input_data), input_data_size);
        avcodec_send_packet(avctx, avpkt);

        AVFrame* frame = av_frame_alloc();
        avcodec_receive_frame(avctx, frame);
        std::memcpy(reinterpret_cast<u16*>(output_data), frame->data, output_data_size);
        av_frame_free(&frame);
        av_packet_free(&avpkt);

        out_sample_count = frame->nb_samples;
        return ResultSuccess;
    }
    return Service::Audio::ResultLibOpusInvalidState;
}

} // namespace AudioCore::ADSP::OpusDecoder
