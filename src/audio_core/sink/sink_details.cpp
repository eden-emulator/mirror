// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "audio_core/sink/sink_details.h"
#ifdef HAVE_SDL3
#include "audio_core/sink/sdl3_sink.h"
#endif
#include "audio_core/sink/null_sink.h"
#include "common/logging.h"
#include "common/settings_enums.h"

namespace AudioCore::Sink {
namespace {
struct SinkDetails {
    using FactoryFn = std::unique_ptr<Sink> (*)(std::string_view);
    using ListDevicesFn = std::vector<std::string> (*)(bool);
    using LatencyFn = u32 (*)(); // REINTRODUCED FROM 3833 - DIABLO 3 FIX
    // using SuitableFn = bool (*)(); // REVERTED FOR ABOVE - DIABLO 3 FIX

    /// Name for this sink.
    Settings::AudioEngine id;
    /// A method to call to construct an instance of this type of sink.
    FactoryFn factory;
    /// A method to call to list available devices.
    ListDevicesFn list_devices;
};

// sink_details is ordered in terms of desirability, with the best choice at the top.
static constexpr SinkDetails sink_details[] = {
#ifdef HAVE_SDL3
    SinkDetails{
        Settings::AudioEngine::Sdl3,
        [](std::string_view device_id) -> std::unique_ptr<Sink> {
            return std::make_unique<SDLSink>(device_id);
        },
        &ListSDLSinkDevices,
    },
#endif
    SinkDetails{
        Settings::AudioEngine::Null,
        [](std::string_view device_id) -> std::unique_ptr<Sink> {
            return std::make_unique<NullSink>(device_id);
        },
        [](bool capture) { return std::vector<std::string>{"null"}; },
    },
};

const SinkDetails& GetOutputSinkDetails(Settings::AudioEngine sink_id) {
    const auto find_backend{[](Settings::AudioEngine id) {
        return std::ranges::find_if(std::begin(sink_details), std::end(sink_details), [&id](const auto& e) {
            return e.id == id;
        });
    }};

    auto iter = find_backend(sink_id);
    if (sink_id == Settings::AudioEngine::Cubeb || sink_id == Settings::AudioEngine::Auto) {
        iter = find_backend(Settings::AudioEngine::Sdl3);
        LOG_INFO(Service_Audio, "Auto-selecting the {} backend", Settings::CanonicalizeEnum(iter->id));
    }
    if (iter == std::end(sink_details)) {
        LOG_ERROR(Audio, "Invalid sink_id {}", Settings::CanonicalizeEnum(sink_id));
        iter = find_backend(Settings::AudioEngine::Null);
    }
    return *iter;
}
} // Anonymous namespace

std::vector<Settings::AudioEngine> GetSinkIDs() {
    std::vector<Settings::AudioEngine> sink_ids(std::size(sink_details));
    std::transform(std::begin(sink_details), std::end(sink_details), std::begin(sink_ids), [](const auto& sink) { return sink.id; });
    return sink_ids;
}

std::vector<std::string> GetDeviceListForSink(Settings::AudioEngine sink_id, bool capture) {
    return GetOutputSinkDetails(sink_id).list_devices(capture);
}

std::unique_ptr<Sink> CreateSinkFromID(Settings::AudioEngine sink_id, std::string_view device_id) {
    return GetOutputSinkDetails(sink_id).factory(device_id);
}

} // namespace AudioCore::Sink
