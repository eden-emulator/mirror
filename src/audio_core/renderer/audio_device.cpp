// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <span>

#include "audio_core/audio_core.h"
#include "audio_core/common/feature_support.h"
#include "audio_core/renderer/audio_device.h"
#include "audio_core/sink/sink.h"
#include "core/core.h"

namespace AudioCore::Renderer {

constexpr std::array usb_device_names{
    AudioDevice::AudioDeviceName{"AudioStereoJackOutput"},
    AudioDevice::AudioDeviceName{"AudioBuiltInSpeakerOutput"},
    AudioDevice::AudioDeviceName{"AudioTvOutput"},
    AudioDevice::AudioDeviceName{"AudioUsbDeviceOutput"},
};

constexpr std::array device_names{
    AudioDevice::AudioDeviceName{"AudioStereoJackOutput"},
    AudioDevice::AudioDeviceName{"AudioBuiltInSpeakerOutput"},
    AudioDevice::AudioDeviceName{"AudioTvOutput"},
};

constexpr std::array output_device_names{
    AudioDevice::AudioDeviceName{"AudioBuiltInSpeakerOutput"},
    AudioDevice::AudioDeviceName{"AudioTvOutput"},
    AudioDevice::AudioDeviceName{"AudioExternalOutput"},
};

AudioDevice::AudioDevice(Core::System& system, const u64 applet_resource_user_id_, const u32 revision)
    : applet_resource_user_id{applet_resource_user_id_}
    , user_revision{revision}
{}

u32 AudioDevice::ListAudioDeviceName(std::span<AudioDeviceName> out_buffer) const {
    std::span<const AudioDeviceName> names{};
    if (CheckFeatureSupported(SupportTags::AudioUsbDeviceOutput, user_revision)) {
        names = usb_device_names;
    } else {
        names = device_names;
    }
    const u32 out_count = u32((std::min)(out_buffer.size(), names.size()));
    for (u32 i = 0; i < out_count; i++)
        out_buffer[i] = names[i];
    return out_count;
}

u32 AudioDevice::ListAudioOutputDeviceName(std::span<AudioDeviceName> out_buffer) const {
    const u32 out_count = u32((std::min)(out_buffer.size(), output_device_names.size()));
    for (u32 i = 0; i < out_count; i++)
        out_buffer[i] = output_device_names[i];
    return out_count;
}

void AudioDevice::SetDeviceVolumes(Core::System& system, const f32 volume) {
    system.AudioCore().GetOutputSink().SetDeviceVolume(volume);
}

f32 AudioDevice::GetDeviceVolume(Core::System& system, [[maybe_unused]] std::string_view name) const {
    return system.AudioCore().GetOutputSink().GetDeviceVolume();
}

} // namespace AudioCore::Renderer
