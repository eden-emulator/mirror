// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "audio_core/adsp/apps/opus/opus_types.h"
#include "common/common_types.h"
#include "core/hle/result.h"

struct AVCodec;
struct AVCodecContext;

namespace AudioCore::ADSP::OpusDecoder {

class OpusMultiStreamDecodeObject {
public:
    static u32 GetWorkBufferSize(u32 total_stream_count, u32 stereo_stream_count);

    Result InitializeDecoder(u32 sample_rate, u32 total_stream_count, u32 channel_count, u32 stereo_stream_count, u8* mappings);
    Result Shutdown();
    Result ResetDecoder();
    Result Decode(u32& out_sample_count, u64 output_data, u64 output_data_size, u64 input_data, u64 input_data_size);

    AVCodec const* codec = nullptr;
    AVCodecContext* avctx = nullptr;
};

} // namespace AudioCore::ADSP::OpusDecoder
