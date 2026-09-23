// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <chrono>

#include "audio_core/adsp/adsp.h"
#include "audio_core/audio_core.h"
#include "audio_core/renderer/system_manager.h"
#include "common/thread.h"
#include "core/core.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/k_scheduler.h"
#include "core/core_timing.h"

namespace AudioCore::Renderer {

SystemManager::SystemManager(Core::System& core_)
    : core{core_}
{
}

SystemManager::~SystemManager() {
    Stop();
}

void SystemManager::InitializeUnsafe() {
    if (!thread.joinable()) {
        core.AudioCore().ADSP().AudioRenderer().Start();
        thread = core.Kernel().RunOnHostCoreThread("AudioRenderSystemManager", [this]() {
            auto& audio_renderer = core.AudioCore().ADSP().AudioRenderer();
            auto const stop_token = thread.get_stop_token();
            while (!stop_token.stop_requested()) {
                {
                    std::scoped_lock lk{mutex};
                    for (auto system : systems)
                        system->SendCommandToDsp();
                }
                audio_renderer.Signal();
                audio_renderer.Wait(stop_token);
            }
        });
    }
}

void SystemManager::Stop() {
    core.AudioCore().ADSP().AudioRenderer().Stop();
    if (thread.joinable()) {
        thread.request_stop();
        thread.join();
    }
}

bool SystemManager::Add(System& system_) {
    std::scoped_lock lk{mutex};
    if (systems.size() >= MaxRendererSessions) {
        LOG_ERROR(Service_Audio, "Maximum AudioRenderer Systems active, cannot add more!");
        return false;
    }
    systems.push_back(&system_);
    if (!systems.empty()) {
        InitializeUnsafe();
    }
    return true;
}

bool SystemManager::Remove(System& system_) {
    std::scoped_lock lk{mutex};
    if (systems.remove(&system_) == 0) {
        LOG_ERROR(Service_Audio, "Failed to remove a render system, it was not found in the list!");
        return false;
    }
    if (systems.empty()) {
        Stop();
    }
    return true;
}

} // namespace AudioCore::Renderer
