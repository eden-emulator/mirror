// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "audio_core/audio_in_manager.h"
#include "audio_core/in/audio_in.h"
#include "core/hle/kernel/k_event.h"

namespace AudioCore::AudioIn {

In::In(Core::System& system_, Manager& manager_, Kernel::KEvent* event_, size_t session_id_)
    : manager{manager_}, parent_mutex{manager.mutex}, event{event_}
    , audio_system{system_, event, session_id_}
{}

void In::Free(Core::System& system) {
    std::scoped_lock l{parent_mutex};
    manager.ReleaseSessionId(system, audio_system.GetSessionId());
}

System& In::GetSystem() {
    return audio_system;
}

AudioIn::State In::GetState() {
    std::scoped_lock l{parent_mutex};
    return audio_system.GetState();
}

Result In::StartSystem() {
    std::scoped_lock l{parent_mutex};
    return audio_system.Start();
}

void In::StartSession() {
    std::scoped_lock l{parent_mutex};
    audio_system.StartSession();
}

Result In::StopSystem() {
    std::scoped_lock l{parent_mutex};
    return audio_system.Stop();
}

Result In::AppendBuffer(const AudioInBuffer& buffer, u64 tag) {
    std::scoped_lock l{parent_mutex};

    if (audio_system.AppendBuffer(buffer, tag)) {
        return ResultSuccess;
    }
    return Service::Audio::ResultBufferCountReached;
}

void In::ReleaseAndRegisterBuffers() {
    std::scoped_lock l{parent_mutex};
    if (audio_system.GetState() == State::Started) {
        audio_system.ReleaseBuffers();
        audio_system.RegisterBuffers();
    }
}

bool In::FlushAudioInBuffers() {
    std::scoped_lock l{parent_mutex};
    return audio_system.FlushAudioInBuffers();
}

u32 In::GetReleasedBuffers(std::span<u64> tags) {
    std::scoped_lock l{parent_mutex};
    return audio_system.GetReleasedBuffers(tags);
}

Kernel::KReadableEvent& In::GetBufferEvent() {
    std::scoped_lock l{parent_mutex};
    return event->GetReadableEvent();
}

f32 In::GetVolume() const {
    std::scoped_lock l{parent_mutex};
    return audio_system.GetVolume();
}

void In::SetVolume(f32 volume) {
    std::scoped_lock l{parent_mutex};
    audio_system.SetVolume(volume);
}

bool In::ContainsAudioBuffer(u64 tag) const {
    std::scoped_lock l{parent_mutex};
    return audio_system.ContainsAudioBuffer(tag);
}

u32 In::GetBufferCount() const {
    std::scoped_lock l{parent_mutex};
    return audio_system.GetBufferCount();
}

u64 In::GetPlayedSampleCount() const {
    std::scoped_lock l{parent_mutex};
    return audio_system.GetPlayedSampleCount();
}

} // namespace AudioCore::AudioIn
