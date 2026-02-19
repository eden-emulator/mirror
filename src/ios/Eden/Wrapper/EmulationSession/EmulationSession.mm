//
//  EmulationSession.m - Sudachi
//  Created by Jarrod Norwell on 1/20/24.
//

#import "EmulationSession.h"

#include <SDL.h>

#include <codecvt>
#include <locale>
#include <string>
#include <string_view>
#include <dlfcn.h>

#include "common/fs/fs.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/savedata_factory.h"
#include "core/loader/nro.h"
#include "frontend_common/content_manager.h"

#include "common/detached_tasks.h"
#include "common/dynamic_library.h"
#include "common/fs/path_util.h"
#include "common/logging/backend.h"
#include "common/logging/log.h"
#include "common/microprofile.h"
#include "common/scm_rev.h"
#include "common/scope_exit.h"
#include "common/settings.h"
#include "common/string_util.h"
#include "core/core.h"
#include "core/cpu_manager.h"
#include "core/crypto/key_manager.h"
#include "core/file_sys/card_image.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/fs_filesystem.h"
#include "core/file_sys/submission_package.h"
#include "core/file_sys/vfs/vfs.h"
#include "core/file_sys/vfs/vfs_real.h"
#include "core/frontend/applets/cabinet.h"
#include "core/frontend/applets/controller.h"
#include "core/frontend/applets/error.h"
#include "core/frontend/applets/general.h"
#include "core/frontend/applets/mii_edit.h"
#include "core/frontend/applets/profile_select.h"
#include "core/frontend/applets/software_keyboard.h"
#include "core/frontend/applets/web_browser.h"
#include "core/hle/service/am/applet_manager.h"
#include "core/hle/service/am/frontend/applets.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/loader/loader.h"
#include "frontend_common/yuzu_config.h"
#include "hid_core/frontend/emulated_controller.h"
#include "hid_core/hid_core.h"
#include "hid_core/hid_types.h"
#include "video_core/renderer_base.h"
#include "video_core/renderer_vulkan/renderer_vulkan.h"
#include "video_core/vulkan_common/vulkan_instance.h"
#include "video_core/vulkan_common/vulkan_surface.h"

#define jconst [[maybe_unused]] const auto
#define jauto [[maybe_unused]] auto

static EmulationSession s_instance;

EmulationSession::EmulationSession() {
    m_vfs = std::make_shared<FileSys::RealVfsFilesystem>();
}

EmulationSession& EmulationSession::GetInstance() {
    return s_instance;
}

const Core::System& EmulationSession::System() const {
    return m_system;
}

Core::System& EmulationSession::System() {
    return m_system;
}

FileSys::ManualContentProvider* EmulationSession::GetContentProvider() {
    return m_manual_provider.get();
}

InputCommon::InputSubsystem& EmulationSession::GetInputSubsystem() {
    return m_input_subsystem;
}

const EmulationWindow& EmulationSession::Window() const {
    return *m_window;
}

EmulationWindow& EmulationSession::Window() {
    return *m_window;
}

CA::MetalLayer* EmulationSession::NativeWindow() const {
    return m_native_window;
}

void EmulationSession::SetNativeWindow(CA::MetalLayer* native_window, CGSize size) {
    m_native_window = native_window;
    m_size = size;
}

void EmulationSession::InitializeGpuDriver() {
    m_vulkan_library = std::make_shared<Common::DynamicLibrary>(dlopen("@executable_path/Frameworks/MoltenVK", RTLD_NOW));
}

bool EmulationSession::IsRunning() const {
    return m_is_running;
}

bool EmulationSession::IsPaused() const {
    return m_is_running && m_is_paused;
}

const Core::PerfStatsResults& EmulationSession::PerfStats() {
    m_perf_stats = m_system.GetAndResetPerfStats();
    return m_perf_stats;
}

void EmulationSession::SurfaceChanged() {
    if (!IsRunning()) {
        return;
    }
    m_window->OnSurfaceChanged(m_native_window, m_size);
}

void EmulationSession::ConfigureFilesystemProvider(const std::string& filepath) {
    const auto file = m_system.GetFilesystem()->OpenFile(filepath, FileSys::OpenMode::Read);
    if (!file) {
        return;
    }

    auto loader = Loader::GetLoader(m_system, file);
    if (!loader) {
        return;
    }

    const auto file_type = loader->GetFileType();
    if (file_type == Loader::FileType::Unknown || file_type == Loader::FileType::Error) {
        return;
    }

    u64 program_id = 0;
    const auto res2 = loader->ReadProgramId(program_id);
    if (res2 == Loader::ResultStatus::Success && file_type == Loader::FileType::NCA) {
        m_manual_provider->AddEntry(FileSys::TitleType::Application,
                                    FileSys::GetCRTypeFromNCAType(FileSys::NCA{file}.GetType()),
                                    program_id, file);
    } else if (res2 == Loader::ResultStatus::Success &&
               (file_type == Loader::FileType::XCI || file_type == Loader::FileType::NSP)) {
        const auto nsp = file_type == Loader::FileType::NSP
                             ? std::make_shared<FileSys::NSP>(file)
                             : FileSys::XCI{file}.GetSecurePartitionNSP();
        for (const auto& title : nsp->GetNCAs()) {
            for (const auto& entry : title.second) {
                m_manual_provider->AddEntry(entry.first.first, entry.first.second, title.first,
                                            entry.second->GetBaseFile());
            }
        }
    }
}

void EmulationSession::InitializeSystem(bool reload) {
    if (!reload) {
        SDL_SetMainReady();

        // Initialize logging system
        Common::Log::Initialize();
        Common::Log::SetColorConsoleBackendEnabled(true);
        Common::Log::Start();
    }

    // Initialize filesystem.
    m_system.SetFilesystem(m_vfs);
    m_system.GetUserChannel().clear();
    m_manual_provider = std::make_unique<FileSys::ManualContentProvider>();
    m_system.SetContentProvider(std::make_unique<FileSys::ContentProviderUnion>());
    m_system.RegisterContentProvider(FileSys::ContentProviderUnionSlot::FrontendManual,
                                     m_manual_provider.get());
    m_system.GetFileSystemController().CreateFactories(*m_vfs);

    is_initialized = true;
}

void EmulationSession::SetAppletId(int applet_id) {
    m_applet_id = applet_id;
    m_system.GetFrontendAppletHolder().SetCurrentAppletId(
        static_cast<Service::AM::AppletId>(m_applet_id));
}

Core::SystemResultStatus EmulationSession::InitializeEmulation(const std::string& filepath,
                                                               const std::size_t program_index,
                                                               const bool frontend_initiated) {
    std::scoped_lock lock(m_mutex);

    // Create the render window.
    m_window = std::make_unique<EmulationWindow>(&m_input_subsystem, m_native_window, m_size, m_vulkan_library);

    // Initialize system.
    m_system.SetShuttingDown(false);
    m_system.ApplySettings();
    YuzuSettings::LogSettings();
    m_system.HIDCore().ReloadInputDevices();
    m_system.SetFrontendAppletSet({
        nullptr,                     // Amiibo Settings
        nullptr,                     // Controller Selector
        nullptr,                     // Error Display
        nullptr,                     // Mii Editor
        nullptr,                     // Parental Controls
        nullptr,                     // Photo Viewer
        nullptr,                     // Profile Selector
        nullptr, // std::move(android_keyboard), // Software Keyboard
        nullptr,                     // Web Browser
    });

    // Initialize filesystem.
    ConfigureFilesystemProvider(filepath);

    // Load the ROM.
    Service::AM::FrontendAppletParameters params{
        .applet_id = static_cast<Service::AM::AppletId>(m_applet_id),
        .launch_type = frontend_initiated ? Service::AM::LaunchType::FrontendInitiated
                                          : Service::AM::LaunchType::ApplicationInitiated,
        .program_index = static_cast<s32>(program_index),
    };
    m_load_result = m_system.Load(EmulationSession::GetInstance().Window(), filepath, params);
    if (m_load_result != Core::SystemResultStatus::Success) {
        return m_load_result;
    }

    // Complete initialization.
    m_system.GPU().Start();
    m_system.GetCpuManager().OnGpuReady();
    m_system.RegisterExitCallback([&] { HaltEmulation(); });

    if (YuzuSettings::values.use_disk_shader_cache.GetValue()) {
        m_system.Renderer().ReadRasterizer()->LoadDiskResources(
                                                              m_system.GetApplicationProcessProgramID(), std::stop_token{},
                                                              [](VideoCore::LoadCallbackStage, size_t value, size_t total) {});
    }

    // Register an ExecuteProgram callback such that Core can execute a sub-program
    m_system.RegisterExecuteProgramCallback([&](std::size_t program_index_) {
        m_next_program_index = program_index_;
        EmulationSession::GetInstance().HaltEmulation();
        ChangeProgram(m_next_program_index);
    });

    OnEmulationStarted();
    return Core::SystemResultStatus::Success;
}


Core::SystemResultStatus EmulationSession::BootOS() {
    std::scoped_lock lock(m_mutex);

    // Create the render window.
    m_window = std::make_unique<EmulationWindow>(&m_input_subsystem, m_native_window, m_size, m_vulkan_library);

    // Initialize system.
    m_system.SetShuttingDown(false);
    m_system.ApplySettings();
    YuzuSettings::LogSettings();
    m_system.HIDCore().ReloadInputDevices();
    m_system.SetFrontendAppletSet({
        nullptr,                     // Amiibo Settings
        nullptr,                     // Controller Selector
        nullptr,                     // Error Display
        nullptr,                     // Mii Editor
        nullptr,                     // Parental Controls
        nullptr,                     // Photo Viewer
        nullptr,                     // Profile Selector
        nullptr, // std::move(android_keyboard), // Software Keyboard
        nullptr,                     // Web Browser
    });

    constexpr u64 QLaunchId = static_cast<u64>(Service::AM::AppletProgramId::QLaunch);
    auto bis_system = m_system.GetFileSystemController().GetSystemNANDContents();

    auto qlaunch_applet_nca = bis_system->GetEntry(QLaunchId, FileSys::ContentRecordType::Program);

    m_system.GetFrontendAppletHolder().SetCurrentAppletId(Service::AM::AppletId::QLaunch);

    const auto filename = qlaunch_applet_nca->GetFullPath();

    auto params = Service::AM::FrontendAppletParameters {
        .program_id = QLaunchId,
        .applet_id = Service::AM::AppletId::QLaunch,
        .applet_type = Service::AM::AppletType::LibraryApplet
    };

    m_load_result = m_system.Load(EmulationSession::GetInstance().Window(), filename, params);

    if (m_load_result != Core::SystemResultStatus::Success) {
        return m_load_result;
    }

    // Complete initialization.
    m_system.GPU().Start();
    m_system.GetCpuManager().OnGpuReady();
    m_system.RegisterExitCallback([&] { HaltEmulation(); });

    if (YuzuSettings::values.use_disk_shader_cache.GetValue()) {
        m_system.Renderer().ReadRasterizer()->LoadDiskResources(
                                                              m_system.GetApplicationProcessProgramID(), std::stop_token{},
                                                              [](VideoCore::LoadCallbackStage, size_t value, size_t total) {});
    }

    // Register an ExecuteProgram callback such that Core can execute a sub-program
    m_system.RegisterExecuteProgramCallback([&](std::size_t program_index_) {
        m_next_program_index = program_index_;
        EmulationSession::GetInstance().HaltEmulation();
    });

    OnEmulationStarted();
    return Core::SystemResultStatus::Success;
}

void EmulationSession::ShutdownEmulation() {
    std::scoped_lock lock(m_mutex);

    if (m_next_program_index != -1) {
        ChangeProgram(m_next_program_index);
        m_next_program_index = -1;
    }

    m_is_running = false;

    // Unload user input.
    m_system.HIDCore().UnloadInputDevices();

    // Enable all controllers
    m_system.HIDCore().SetSupportedStyleTag({Core::HID::NpadStyleSet::All});

    // Shutdown the main emulated process
    if (m_load_result == Core::SystemResultStatus::Success) {
        m_system.DetachDebugger();
        m_system.ShutdownMainProcess();
        m_detached_tasks.WaitForAllTasks();
        m_load_result = Core::SystemResultStatus::ErrorNotInitialized;
        m_window.reset();
        OnEmulationStopped(Core::SystemResultStatus::Success);
        return;
    }

    // Tear down the render window.
    m_window.reset();
}

void EmulationSession::PauseEmulation() {
    std::scoped_lock lock(m_mutex);
    m_system.Pause();
    m_is_paused = true;
}

void EmulationSession::UnPauseEmulation() {
    std::scoped_lock lock(m_mutex);
    m_system.Run();
    m_is_paused = false;
}

void EmulationSession::HaltEmulation() {
    std::scoped_lock lock(m_mutex);
    m_is_running = false;
    m_cv.notify_one();
}

void EmulationSession::RunEmulation() {
    {
        std::scoped_lock lock(m_mutex);
        m_is_running = true;
    }

    // Load the disk shader cache.
    if (YuzuSettings::values.use_disk_shader_cache.GetValue()) {
        LoadDiskCacheProgress(VideoCore::LoadCallbackStage::Prepare, 0, 0);
        m_system.Renderer().ReadRasterizer()->LoadDiskResources(
            m_system.GetApplicationProcessProgramID(), std::stop_token{}, LoadDiskCacheProgress);
        LoadDiskCacheProgress(VideoCore::LoadCallbackStage::Complete, 0, 0);
    }

    void(m_system.Run());

    if (m_system.DebuggerEnabled()) {
        m_system.InitializeDebugger();
    }

    while (true) {
        {
            [[maybe_unused]] std::unique_lock lock(m_mutex);
            if (m_cv.wait_for(lock, std::chrono::milliseconds(800),
                              [&]() { return !m_is_running; })) {
                // Emulation halted.
                break;
            }
        }
    }

    // Reset current applet ID.
    m_applet_id = static_cast<int>(Service::AM::AppletId::Application);
}

void EmulationSession::LoadDiskCacheProgress(VideoCore::LoadCallbackStage stage, int progress,
                                             int max) {

}

void EmulationSession::OnEmulationStarted() {

}

void EmulationSession::OnEmulationStopped(Core::SystemResultStatus result) {

}

void EmulationSession::ChangeProgram(std::size_t program_index) {
    LOG_INFO(Frontend, "Trying To Switch Program");
    // Halt current emulation session
    EmulationSession::GetInstance().HaltEmulation();
    // Save the current state if necessary

    // Shutdown the current emulation session cleanly
        // Update the program index
    EmulationSession::GetInstance().m_next_program_index = program_index;

    // Initialize the new program
    // Start the new emulation session
    EmulationSession::GetInstance().RunEmulation();
}

u64 EmulationSession::GetProgramId(std::string programId) {
    try {
        return std::stoull(programId);
    } catch (...) {
        return 0;
    }
}

static Core::SystemResultStatus RunEmulation(const std::string& filepath,
                                             const size_t program_index,
                                             const bool frontend_initiated) {
    MicroProfileOnThreadCreate("EmuThread");
    SCOPE_EXIT {
        MicroProfileShutdown();
    };

    LOG_INFO(Frontend, "starting");

    if (filepath.empty()) {
        LOG_CRITICAL(Frontend, "failed to load: filepath empty!");
        return Core::SystemResultStatus::ErrorLoader;
    }

    SCOPE_EXIT {
        EmulationSession::GetInstance().ShutdownEmulation();
    };

    jconst result = EmulationSession::GetInstance().InitializeEmulation(filepath, program_index,
                                                                        frontend_initiated);
    if (result != Core::SystemResultStatus::Success) {
        return result;
    }

    EmulationSession::GetInstance().RunEmulation();

    return Core::SystemResultStatus::Success;
}



bool EmulationSession::IsHandheldOnly() {
    jconst npad_style_set = m_system.HIDCore().GetSupportedStyleTag();

    if (npad_style_set.fullkey == 1) {
        return false;
    }

    if (npad_style_set.handheld == 0) {
        return false;
    }

    return !YuzuSettings::IsDockedMode();
}

void EmulationSession::SetDeviceType([[maybe_unused]] int index, int type) {
    jauto controller = m_system.HIDCore().GetEmulatedControllerByIndex(index);
    controller->SetNpadStyleIndex(static_cast<Core::HID::NpadStyleIndex>(type));
}

void EmulationSession::OnGamepadConnectEvent([[maybe_unused]] int index) {
    jauto controller = m_system.HIDCore().GetEmulatedControllerByIndex(index);

    // Ensure that player1 is configured correctly and handheld disconnected
    if (controller->GetNpadIdType() == Core::HID::NpadIdType::Player1) {
        jauto handheld = m_system.HIDCore().GetEmulatedController(Core::HID::NpadIdType::Handheld);

        if (controller->GetNpadStyleIndex() == Core::HID::NpadStyleIndex::Handheld) {
            handheld->SetNpadStyleIndex(Core::HID::NpadStyleIndex::Fullkey);
            controller->SetNpadStyleIndex(Core::HID::NpadStyleIndex::Fullkey);
            handheld->Disconnect();
        }
    }

    // Ensure that handheld is configured correctly and player 1 disconnected
    if (controller->GetNpadIdType() == Core::HID::NpadIdType::Handheld) {
        jauto player1 = m_system.HIDCore().GetEmulatedController(Core::HID::NpadIdType::Player1);

        if (controller->GetNpadStyleIndex() != Core::HID::NpadStyleIndex::Handheld) {
            player1->SetNpadStyleIndex(Core::HID::NpadStyleIndex::Handheld);
            controller->SetNpadStyleIndex(Core::HID::NpadStyleIndex::Handheld);
            player1->Disconnect();
        }
    }

    if (!controller->IsConnected()) {
        controller->Connect();
    }
}

void EmulationSession::OnGamepadDisconnectEvent([[maybe_unused]] int index) {
    jauto controller = m_system.HIDCore().GetEmulatedControllerByIndex(index);
    controller->Disconnect();
}
