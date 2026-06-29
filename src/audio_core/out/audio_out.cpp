// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "audio_core/audio_out_manager.h"
#include "audio_core/out/audio_out.h"
#include "core/hle/kernel/k_event.h"

namespace AudioCore::AudioOut {

Out::Out(Core::System& system_, Manager& manager_, Kernel::KEvent* event_, size_t session_id_)
    : manager{manager_}, parent_mutex{manager.mutex}, event{event_}
    , audio_system{system_, event, session_id_}
{}

void Out::Free(Core::System& system) {
    std::scoped_lock l{parent_mutex};
    manager.ReleaseSessionId(system, audio_system.GetSessionId());
}

System& Out::GetSystem() {
    return audio_system;
}

AudioOut::State Out::GetState() {
    std::scoped_lock l{parent_mutex};
    return audio_system.GetState();
}

Result Out::StartSystem() {
    std::scoped_lock l{parent_mutex};
    return audio_system.Start();
}

void Out::StartSession() {
    std::scoped_lock l{parent_mutex};
    audio_system.StartSession();
}

Result Out::StopSystem() {
    std::scoped_lock l{parent_mutex};
    return audio_system.Stop();
}

Result Out::AppendBuffer(const AudioOutBuffer& buffer, const u64 tag) {
    std::scoped_lock l{parent_mutex};

    if (audio_system.AppendBuffer(buffer, tag)) {
        return ResultSuccess;
    }
    return Service::Audio::ResultBufferCountReached;
}

void Out::ReleaseAndRegisterBuffers() {
    std::scoped_lock l{parent_mutex};
    if (audio_system.GetState() == State::Started) {
        audio_system.ReleaseBuffers();
        audio_system.RegisterBuffers();
    }
}

bool Out::FlushAudioOutBuffers() {
    std::scoped_lock l{parent_mutex};
    return audio_system.FlushAudioOutBuffers();
}

u32 Out::GetReleasedBuffers(std::span<u64> tags) {
    std::scoped_lock l{parent_mutex};
    return audio_system.GetReleasedBuffers(tags);
}

Kernel::KReadableEvent& Out::GetBufferEvent() {
    std::scoped_lock l{parent_mutex};
    return event->GetReadableEvent();
}

f32 Out::GetVolume() const {
    std::scoped_lock l{parent_mutex};
    return audio_system.GetVolume();
}

void Out::SetVolume(const f32 volume) {
    std::scoped_lock l{parent_mutex};
    audio_system.SetVolume(volume);
}

bool Out::ContainsAudioBuffer(const u64 tag) const {
    std::scoped_lock l{parent_mutex};
    return audio_system.ContainsAudioBuffer(tag);
}

u32 Out::GetBufferCount() const {
    std::scoped_lock l{parent_mutex};
    return audio_system.GetBufferCount();
}

u64 Out::GetPlayedSampleCount() const {
    std::scoped_lock l{parent_mutex};
    return audio_system.GetPlayedSampleCount();
}

} // namespace AudioCore::AudioOut
