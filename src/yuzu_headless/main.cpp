// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2014 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>
#include <regex>
#include <string>

#include "common/logging.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/cpu_manager.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/vfs/vfs_real.h"
#include "core/hle/service/am/applet_manager.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/loader/loader.h"
#include "frontend_common/config.h"
#include "input_common/main.h"
#include "video_core/gpu.h"
#include "video_core/renderer_base.h"
#include "yuzu_cmd/emu_window/emu_window_sdl3.h"
#include "yuzu_cmd/emu_window/emu_window_sdl3_null.h"
#include "yuzu_cmd/emu_window/emu_window_sdl3_vk.h"

class EmuWindow_Headless : public Core::Frontend::EmuWindow {
public:
    explicit EmuWindow_Headless() = default;
    ~EmuWindow_Headless() = default;

    bool IsShown() const override {
        return true;
    }
    void OnMinimalClientAreaChangeRequest(std::pair<u32, u32> minimal_size) override {

    }
    std::unique_ptr<Core::Frontend::GraphicsContext> CreateSharedContext() const override {
        return std::make_unique<DummyContext>();
    }
};

int main(int argc, char *argv[]) {
    struct {
        Core::System system{};
        EmuWindow_Headless emu_window{};
    } state = {};

    Common::Log::Initialize();
    Common::Log::SetColorConsoleBackendEnabled(true);
    Common::Log::Start();

    if (argc < 2) {
        LOG_CRITICAL(Frontend, "Usage: {} [ms-to-run] [file]", argv[0]);
        return EXIT_FAILURE;
    }

    std::chrono::milliseconds time_quanta = std::chrono::milliseconds{atoi(argv[1])};
    auto const time_end = std::chrono::steady_clock::now() + time_quanta;
    std::string filepath = argv[2];

    // apply the log_filter setting
    // the logger was initialized before and doesn't pick up the filter on its own
    Common::Log::Filter filter;
    filter.ParseFilterString("*:Info");
    Common::Log::SetGlobalFilter(filter);

    if (filepath.empty()) {
        LOG_CRITICAL(Frontend, "Failed to load ROM: No ROM specified");
        return EXIT_FAILURE;
    }

    state.system.Initialize();

    InputCommon::InputSubsystem input_subsystem{};

    // Apply the command line arguments
    state.system.ApplySettings();
    Settings::values.renderer_backend.SetValue(Settings::RendererBackend::Null);

    state.system.SetContentProvider(std::make_unique<FileSys::ContentProviderUnion>());
    state.system.SetFilesystem(std::make_shared<FileSys::RealVfsFilesystem>());
    state.system.GetFileSystemController().CreateFactories(*state.system.GetFilesystem());
    state.system.GetUserChannel().clear();

    Service::AM::FrontendAppletParameters load_parameters{
        .applet_id = Service::AM::AppletId::Application,
    };
    if (auto const load_result = state.system.Load(state.emu_window, filepath, load_parameters); load_result != Core::SystemResultStatus::Success) {
        LOG_CRITICAL(Frontend, "load result = {}", u32(load_result));
        // shutdown
        void(state.system.Pause());
        state.system.DetachDebugger();
        state.system.ShutdownMainProcess();
        return EXIT_FAILURE;
    }
    // Core is loaded, start the GPU (makes the GPU contexts current to this thread)
    state.system.GPU().Start();
    state.system.GetCpuManager().OnGpuReady();
    // don't do anything, SDL3 already exists for us :D
    state.system.RegisterExitCallback([] {});
    void(state.system.Run());
    if (state.system.DebuggerEnabled())
        state.system.InitializeDebugger();
    while (state.system.IsPoweredOn() && !state.system.GetExitRequested()) {
        auto const time_now = std::chrono::steady_clock::now();
        if (time_now > time_end)
            break;
    }
    // shutdown
    void(state.system.Pause());
    state.system.DetachDebugger();
    state.system.ShutdownMainProcess();
    return EXIT_SUCCESS;
}

#define VMA_IMPLEMENTATION
#include "video_core/vulkan_common/vma.h"
