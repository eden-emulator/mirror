// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "audio_core/audio_core.h"
#include "audio_core/audio_manager.h"
#include "audio_core/audio_out_manager.h"
#include "audio_core/out/audio_out.h"
#include "core/core.h"
#include "core/hle/kernel/k_event.h"
#include "core/hle/service/audio/errors.h"

namespace AudioCore::AudioOut {

Manager::Manager(Core::System& system) {
    std::iota(session_ids.begin(), session_ids.end(), 0);
    num_free_sessions = MaxOutSessions;
}

Result Manager::AcquireSessionId(Core::System& system, size_t& session_id) {
    if (num_free_sessions == 0) {
        LOG_ERROR(Service_Audio, "All 12 Audio Out sessions are in use, cannot create any more");
        return Service::Audio::ResultOutOfSessions;
    }
    session_id = session_ids[next_session_id];
    next_session_id = (next_session_id + 1) % MaxOutSessions;
    num_free_sessions--;
    return ResultSuccess;
}

void Manager::ReleaseSessionId(Core::System& system, const size_t session_id) {
    std::scoped_lock l{mutex};
    LOG_DEBUG(Service_Audio, "Freeing AudioOut session {}", session_id);
    session_ids[free_session_id] = session_id;
    num_free_sessions++;
    free_session_id = (free_session_id + 1) % MaxOutSessions;
    sessions[session_id].reset();
    applet_resource_user_ids[session_id] = 0;
}

Result Manager::LinkToManager(Core::System& system) {
    std::scoped_lock l{mutex};
    if (!linked_to_manager) {
        system.AudioCore().GetAudioManager().SetOutManager(&Manager::BufferReleaseAndRegister);
        linked_to_manager = true;
    }

    return ResultSuccess;
}

void Manager::Start(Core::System& system) {
    if (sessions_started) {
        return;
    }

    std::scoped_lock l{mutex};
    for (auto& session : sessions) {
        if (session) {
            session->StartSession();
        }
    }

    sessions_started = true;
}

void Manager::BufferReleaseAndRegister(void *data, Core::System& system) noexcept {
    Manager* this_ = (Manager*)data;
    std::scoped_lock l{this_->mutex};
    for (auto& session : this_->sessions) {
        if (session != nullptr) {
            session->ReleaseAndRegisterBuffers();
        }
    }
}

} // namespace AudioCore::AudioOut
