// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/core.h"
#include "core/hle/service/audio/audio.h"
#include "core/hle/service/audio/audio_controller.h"
#include "core/hle/service/audio/audio_in_manager.h"
#include "core/hle/service/audio/audio_out_manager.h"
#include "core/hle/service/audio/audio_renderer_manager.h"
#include "core/hle/service/audio/final_output_recorder_manager.h"
#include "core/hle/service/audio/final_output_recorder_manager_for_applet.h"
#include "core/hle/service/audio/hardware_opus_decoder_manager.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"

namespace Service::Audio {

class IAudioOutManagerForApplet final : public ServiceFramework<IAudioOutManagerForApplet> {
public:
    explicit IAudioOutManagerForApplet(Core::System& system_)
        : ServiceFramework{system_, "audout:a"} {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key,
            FunctionInfo{0, nullptr, "RequestSuspend"},
            FunctionInfo{1, nullptr, "RequestResume"},
            FunctionInfo{2, nullptr, "GetProcessMasterVolume"},
            FunctionInfo{3, nullptr, "SetProcessMasterVolume"},
            FunctionInfo{4, nullptr, "GetProcessRecordVolume"},
            FunctionInfo{5, nullptr, "SetProcessRecordVolume"}
        );
    }
};

class IAudioSnoopManager final : public ServiceFramework<IAudioSnoopManager> {
public:
    explicit IAudioSnoopManager(Core::System& system_)
        : ServiceFramework{system_, "auddev"} {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key,
            FunctionInfo{0, nullptr, "GetDspStatistics"},
            FunctionInfo{1, nullptr, "GetAppletStateSummaries"},
            FunctionInfo{2, nullptr, "SetDspStatisticsParameter"},
            FunctionInfo{3, nullptr, "GetDspStatisticsParameter"},
            FunctionInfo{6, nullptr, "GetDspUsage"}
        );
    }
};

class IAudioInManagerForApplet final : public ServiceFramework<IAudioInManagerForApplet> {
public:
    explicit IAudioInManagerForApplet(Core::System& system_)
        : ServiceFramework{system_, "audin:a"} {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key,
            FunctionInfo{0, nullptr, "RequestSuspend"},
            FunctionInfo{1, nullptr, "RequestResume"},
            FunctionInfo{2, nullptr, "GetProcessMasterVolume"},
            FunctionInfo{3, nullptr, "SetProcessMasterVolume"}
        );
    }
};

class IAudioRendererManagerForApplet final : public ServiceFramework<IAudioRendererManagerForApplet> {
public:
    explicit IAudioRendererManagerForApplet(Core::System& system_)
        : ServiceFramework{system_, "audren:a"} {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key,
            FunctionInfo{0, nullptr, "RequestSuspend"},
            FunctionInfo{1, nullptr, "RequestResume"},
            FunctionInfo{2, nullptr, "GetProcessMasterVolume"},
            FunctionInfo{3, nullptr, "SetProcessMasterVolume"},
            FunctionInfo{4, nullptr, "RegisterAppletResourceUserId"},
            FunctionInfo{5, nullptr, "UnregisterAppletResourceUserId"},
            FunctionInfo{6, nullptr, "GetProcessRecordVolume"},
            FunctionInfo{7, nullptr, "SetProcessRecordVolume"}
        );
    }
};

class IAudioOutManagerForDebugger final : public ServiceFramework<IAudioOutManagerForDebugger> {
public:
    explicit IAudioOutManagerForDebugger(Core::System& system_)
        : ServiceFramework{system_, "audout:d"} {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key,
            FunctionInfo{0, nullptr, "RequestSuspend"},
            FunctionInfo{1, nullptr, "RequestResume"}
        );
    }
};

class IAudioInManagerForDebugger final : public ServiceFramework<IAudioInManagerForDebugger> {
public:
    explicit IAudioInManagerForDebugger(Core::System& system_)
        : ServiceFramework{system_, "audin:d"} {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key,
            FunctionInfo{0, nullptr, "RequestSuspend"},
            FunctionInfo{1, nullptr, "RequestResume"}
        );
    }
};

class IFinalOutputRecorderManagerForDebugger final : public ServiceFramework<IFinalOutputRecorderManagerForDebugger> {
public:
    explicit IFinalOutputRecorderManagerForDebugger(Core::System& system_)
        : ServiceFramework{system_, "audrec:d"} {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key,
            FunctionInfo{0, nullptr, "RequestSuspend"},
            FunctionInfo{1, nullptr, "RequestResume"}
        );
    }
};

class IAudioRendererManagerForDebugger final : public ServiceFramework<IAudioRendererManagerForDebugger> {
public:
    explicit IAudioRendererManagerForDebugger(Core::System& system_)
        : ServiceFramework{system_, "audren:d"} {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key,
            FunctionInfo{0, nullptr, "RequestSuspend"},
            FunctionInfo{1, nullptr, "RequestResume"}
        );
    }
};

class IAudioSystemManagerForApplet final : public ServiceFramework<IAudioSystemManagerForApplet> {
public:
    explicit IAudioSystemManagerForApplet(Core::System& system_)
        : ServiceFramework{system_, "aud:a"} {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key,
            FunctionInfo{0, nullptr, "RegisterAppletResourceUserId"},
            FunctionInfo{1, nullptr, "UnregisterAppletResourceUserId"},
            FunctionInfo{2, nullptr, "RequestSuspendAudio"},
            FunctionInfo{3, nullptr, "RequestResumeAudio"},
            FunctionInfo{4, nullptr, "GetAudioOutputProcessMasterVolume"},
            FunctionInfo{5, nullptr, "SetAudioOutputProcessMasterVolume"},
            FunctionInfo{6, nullptr, "GetAudioInputProcessMasterVolume"},
            FunctionInfo{7, nullptr, "SetAudioInputProcessMasterVolume"},
            FunctionInfo{8, nullptr, "GetAudioOutputProcessRecordVolume"},
            FunctionInfo{9, nullptr, "SetAudioOutputProcessRecordVolume"},
            FunctionInfo{10, nullptr, "GetAppletStateSummaries"}
        );
    }
};

class IAudioSystemManagerForDebugger final : public ServiceFramework<IAudioSystemManagerForDebugger> {
public:
    explicit IAudioSystemManagerForDebugger(Core::System& system_)
        : ServiceFramework{system_, "aud:d"} {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key,
            FunctionInfo{0, nullptr, "RequestSuspendAudioForDebug"},
            FunctionInfo{1, nullptr, "RequestResumeAudioForDebug"}
        );
    }
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("aud:a", std::make_shared<IAudioSystemManagerForApplet>(system), 30);
    server_manager->RegisterNamedService("aud:d", std::make_shared<IAudioSystemManagerForDebugger>(system), 30);

    server_manager->RegisterNamedService("audout:d", std::make_shared<IAudioOutManagerForDebugger>(system), 30);
    server_manager->RegisterNamedService("audin:d", std::make_shared<IAudioInManagerForDebugger>(system), 30);
    server_manager->RegisterNamedService("audrec:d", std::make_shared<IFinalOutputRecorderManagerForDebugger>(system), 30);
    server_manager->RegisterNamedService("audren:d", std::make_shared<IAudioRendererManagerForDebugger>(system), 30);

    server_manager->RegisterNamedService("audin:u", std::make_shared<IAudioInManager>(system), 30);
    server_manager->RegisterNamedService("audin:a", std::make_shared<IAudioInManagerForApplet>(system), 30);
    server_manager->RegisterNamedService("audout:u", std::make_shared<IAudioOutManager>(system), 30);
    server_manager->RegisterNamedService("audout:a", std::make_shared<IAudioOutManagerForApplet>(system), 30);
    server_manager->RegisterNamedService("auddev", std::make_shared<IAudioSnoopManager>(system), 30);
    // Depends on audout:u and audin:u on ctor!
    server_manager->RegisterNamedService("audctl", std::make_shared<IAudioController>(system), 30);
    server_manager->RegisterNamedService("audrec:a", std::make_shared<IFinalOutputRecorderManagerForApplet>(system), 30);
    server_manager->RegisterNamedService("audrec:u", std::make_shared<IFinalOutputRecorderManager>(system), 30);
    server_manager->RegisterNamedService("audren:u", std::make_shared<IAudioRendererManager>(system), 30);
    server_manager->RegisterNamedService("audren:a", std::make_shared<IAudioRendererManagerForApplet>(system), 30);
    server_manager->RegisterNamedService("hwopus", std::make_shared<IHardwareOpusDecoderManager>(system), 25);
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::Audio
