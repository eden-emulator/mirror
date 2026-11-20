// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <array>
#include <chrono>
#include <future>
#include <thread>

#include "audio_core/sink/sink_stream.h"
#include "core/core.h"

namespace {

class RecordingSinkStream final : public AudioCore::Sink::SinkStream {
public:
    using SinkStream::SinkStream;

    bool running{};
    unsigned int starts{};
    unsigned int stops{};

protected:
    void SetRunning(bool is_running) override {
        running = is_running;
        if (is_running) {
            ++starts;
        } else {
            ++stops;
        }
    }
};

} // namespace

TEST_CASE("SinkStream pauses host audio with emulation", "[audio_core]") {
    using AudioCore::Sink::StreamType;
    const auto type = GENERATE(StreamType::Render, StreamType::Out, StreamType::In);
    Core::System system;
    RecordingSinkStream stream{system, type};

    stream.Start();
    REQUIRE(stream.running);

    stream.SetSystemPaused(true);
    REQUIRE_FALSE(stream.running);
    REQUIRE(stream.IsPaused());
    REQUIRE(stream.stops == 1);

    SECTION("Repeated pause and resume preserve the active stream") {
        stream.SetSystemPaused(true);
        REQUIRE(stream.stops == 1);
        stream.SetSystemPaused(false);
        REQUIRE(stream.running);
        REQUIRE_FALSE(stream.IsPaused());
        REQUIRE(stream.starts == 2);
        stream.SetSystemPaused(false);
        REQUIRE(stream.starts == 2);
    }

    SECTION("A guest start cannot restart host audio while emulation is paused") {
        stream.Start();
        REQUIRE_FALSE(stream.running);
        REQUIRE(stream.starts == 1);
        stream.SetSystemPaused(false);
        REQUIRE(stream.running);
        REQUIRE(stream.starts == 2);
    }

    SECTION("A guest stop during the pause prevents restart on resume") {
        stream.Stop();
        stream.SetSystemPaused(false);
        REQUIRE_FALSE(stream.running);
        REQUIRE(stream.IsPaused());
        REQUIRE(stream.starts == 1);
    }
}

TEST_CASE("SinkStream preserves stopped streams across emulation pause", "[audio_core]") {
    using AudioCore::Sink::StreamType;
    const auto type = GENERATE(StreamType::Render, StreamType::Out, StreamType::In);
    Core::System system;
    RecordingSinkStream stream{system, type};

    stream.SetSystemPaused(true);

    SECTION("An unused stream remains stopped") {
        stream.SetSystemPaused(false);
        REQUIRE_FALSE(stream.running);
        REQUIRE(stream.starts == 0);
        REQUIRE(stream.stops == 0);
    }

    SECTION("A stream started during the pause is deferred until resume") {
        stream.Start();
        REQUIRE_FALSE(stream.running);
        REQUIRE(stream.starts == 0);
        stream.SetSystemPaused(false);
        REQUIRE(stream.running);
        REQUIRE(stream.starts == 1);
    }
}

TEST_CASE("SinkStream retains queued audio across emulation pause", "[audio_core]") {
    using AudioCore::Sink::StreamType;
    const auto type = GENERATE(StreamType::Render, StreamType::Out);
    Core::System system;
    RecordingSinkStream stream{system, type};
    AudioCore::Sink::SinkBuffer buffer{.frames = 1};
    std::array<s16, 2> samples{};
    stream.Start();
    stream.AppendBuffer(buffer, samples);
    REQUIRE(stream.GetQueueSize() == 1);

    stream.SetSystemPaused(true);
    REQUIRE(stream.GetQueueSize() == 1);
    stream.SetSystemPaused(false);
    REQUIRE(stream.GetQueueSize() == 1);
    REQUIRE(stream.running);
}

TEST_CASE("SinkStream emulation pause releases a blocked audio producer", "[audio_core]") {
    using namespace std::chrono_literals;
    Core::System system;
    RecordingSinkStream stream{system, AudioCore::Sink::StreamType::Render};
    stream.Start();
    AudioCore::Sink::SinkBuffer buffer{.frames = 1};
    std::array<s16, 2> samples{};
    for (int i = 0; i < 4; ++i) {
        stream.AppendBuffer(buffer, samples);
    }

    std::promise<void> entered;
    std::promise<void> finished;
    auto completion = finished.get_future();
    std::jthread producer{[&](std::stop_token stop_token) {
        entered.set_value();
        stream.WaitFreeSpace(stop_token);
        finished.set_value();
    }};
    entered.get_future().wait();
    REQUIRE(completion.wait_for(20ms) == std::future_status::timeout);

    stream.SetSystemPaused(true);
    REQUIRE(completion.wait_for(1s) == std::future_status::ready);
    REQUIRE_FALSE(stream.running);
}
