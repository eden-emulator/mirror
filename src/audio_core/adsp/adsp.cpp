// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "audio_core/adsp/adsp.h"
#include "audio_core/host.h"

namespace AudioCore::ADSP {

ADSP::ADSP(Host& system, Sink::Sink& sink) {
    audio_renderer.emplace(system, sink);
    opus_decoder.emplace(system);
    opus_decoder->Send(Direction::DSP, OpusDecoder::Message::Start);
    if (opus_decoder->Receive(Direction::Host) != OpusDecoder::Message::StartOK) {
        LOG_ERROR(Service_Audio, "OpusDecoder failed to initialize.");
        return;
    }
}

AudioRenderer::AudioRenderer& ADSP::AudioRenderer() {
    return *audio_renderer;
}

OpusDecoder::OpusDecoder& ADSP::OpusDecoder() {
    return *opus_decoder;
}

} // namespace AudioCore::ADSP
