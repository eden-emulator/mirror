// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

//
//  EmulationSession.h - Sudachi
//  Created by Jarrod Norwell on 1/20/24.
//

#pragma once

#import <QuartzCore/CAMetalLayer.h>

#import <Metal/Metal.hpp>
#import "EmulationWindow/EmulationWindow.h"

#include "common/detached_tasks.h"
#include "core/core.h"
#include "core/file_sys/registered_cache.h"
#include "core/hle/service/acc/profile_manager.h"
#include "core/perf_stats.h"
#include "frontend_common/content_manager.h"
#include "video_core/rasterizer_interface.h"

class EmulationSession final {
public:
    explicit EmulationSession();
    ~EmulationSession() = default;

    static EmulationSession& GetInstance();
    const Core::System& System() const;
    Core::System& System();
    FileSys::ManualContentProvider* GetContentProvider();
    InputCommon::InputSubsystem& GetInputSubsystem();

    const EmulationWindow& Window() const;
    EmulationWindow& Window();
    CA::MetalLayer* NativeWindow() const;
    void SetNativeWindow(CA::MetalLayer* native_window, CGSize size);
    void SurfaceChanged();

    void InitializeGpuDriver();

    bool IsRunning() const;
    bool IsPaused() const;
    void PauseEmulation();
    void UnPauseEmulation();
    void HaltEmulation();
    void RunEmulation();
    void ShutdownEmulation();

    const Core::PerfStatsResults& PerfStats();
    void ConfigureFilesystemProvider(const std::string& filepath);
    void InitializeSystem(bool reload);
    void SetAppletId(int applet_id);
    Core::SystemResultStatus InitializeEmulation(const std::string& filepath,
                                                 const std::size_t program_index,
                                                 const bool frontend_initiated);
    Core::SystemResultStatus BootOS();

    static void OnEmulationStarted();
    static u64 GetProgramId(std::string programId);
    bool IsInitialized() { return is_initialized; };

    bool IsHandheldOnly();
    void SetDeviceType([[maybe_unused]] int index, int type);
    void OnGamepadConnectEvent([[maybe_unused]] int index);
    void OnGamepadDisconnectEvent([[maybe_unused]] int index);
private:
    static void LoadDiskCacheProgress(VideoCore::LoadCallbackStage stage, int progress, int max);
    static void OnEmulationStopped(Core::SystemResultStatus result);
    static void ChangeProgram(std::size_t program_index);

private:
    // Window management
    std::unique_ptr<EmulationWindow> m_window;
    CA::MetalLayer* m_native_window{};

    // Core emulation
    Core::System m_system;
    InputCommon::InputSubsystem m_input_subsystem;
    Common::DetachedTasks m_detached_tasks;
    Core::PerfStatsResults m_perf_stats{};
    std::shared_ptr<FileSys::VfsFilesystem> m_vfs;
    Core::SystemResultStatus m_load_result{Core::SystemResultStatus::ErrorNotInitialized};
    std::atomic<bool> m_is_running = false;
    std::atomic<bool> m_is_paused = false;
    std::unique_ptr<FileSys::ManualContentProvider> m_manual_provider;
    int m_applet_id{1};

    // GPU driver parameters
    std::shared_ptr<Common::DynamicLibrary> m_vulkan_library;

    // Synchronization
    std::condition_variable_any m_cv;
    mutable std::mutex m_mutex;
    bool is_initialized = false;
    CGSize m_size;

    // Program index for next boot
    std::atomic<s32> m_next_program_index = -1;
};
