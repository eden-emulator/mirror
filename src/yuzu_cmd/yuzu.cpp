// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2014 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdio>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <regex>
#include <string>
#include <thread>
#include "common/settings_enums.h"
#include "core/launch_params.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_hints.h>

#include <fmt/ostream.h>

#include "common/logging.h"
#include "common/scm_rev.h"
#include "common/settings.h"
#include "common/string_util.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/cpu_manager.h"
#include "core/crypto/key_manager.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/vfs/vfs_real.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/global_scheduler_context.h"
#include "core/hle/kernel/k_scheduler.h"
#include "core/hle/kernel/k_thread.h"
#include "core/hle/kernel/physical_core.h"
#include "core/hle/kernel/svc.h"
#include "core/hle/service/service.h"
#include "core/hle/service/am/applet_manager.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/loader/loader.h"
#include "core/memory.h"
#include "frontend_common/config.h"
#include "input_common/drivers/tas_input.h"
#include "input_common/main.h"
#include "network/network.h"
#include "sdl_config.h"
#include "video_core/gpu.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/renderer_base.h"
#include "yuzu_cmd/emu_window/emu_window_sdl3.h"
#ifdef HAS_OPENGL
#include "yuzu_cmd/emu_window/emu_window_sdl3_gl.h"
#endif
#include "yuzu_cmd/emu_window/emu_window_sdl3_null.h"
#include "yuzu_cmd/emu_window/emu_window_sdl3_vk.h"

#ifdef _WIN32
// windows.h needs to be included before shellapi.h
#include <windows.h>
#include <shellapi.h>
#include <dbghelp.h>
#include "common/windows/timer_resolution.h"
#pragma comment(lib, "dbghelp.lib")
#endif

#ifdef _WIN32
extern "C" {
// tells Nvidia and AMD drivers to use the dedicated GPU by default on laptops with switchable
// graphics
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

static void OnStateChanged(const Network::RoomMember::State& state) {
    switch (state) {
    case Network::RoomMember::State::Idle:
        LOG_DEBUG(Network, "Network is idle");
        break;
    case Network::RoomMember::State::Joining:
        LOG_DEBUG(Network, "Connection sequence to room started");
        break;
    case Network::RoomMember::State::Joined:
        LOG_DEBUG(Network, "Successfully joined to the room");
        break;
    case Network::RoomMember::State::Moderator:
        LOG_DEBUG(Network, "Successfully joined the room as a moderator");
        break;
    default:
        break;
    }
}

static void OnNetworkError(const Network::RoomMember::Error& error) {
    switch (error) {
    case Network::RoomMember::Error::LostConnection:
        LOG_DEBUG(Network, "Lost connection to the room");
        break;
    case Network::RoomMember::Error::CouldNotConnect:
        LOG_ERROR(Network, "Error: Could not connect");
        exit(1);
        break;
    case Network::RoomMember::Error::NameCollision:
        LOG_ERROR(
            Network,
            "You tried to use the same nickname as another user that is connected to the Room");
        exit(1);
        break;
    case Network::RoomMember::Error::IpCollision:
        LOG_ERROR(Network, "You tried to use the same fake IP-Address as another user that is "
                           "connected to the Room");
        exit(1);
        break;
    case Network::RoomMember::Error::WrongPassword:
        LOG_ERROR(Network, "Room replied with: Wrong password");
        exit(1);
        break;
    case Network::RoomMember::Error::WrongVersion:
        LOG_ERROR(Network,
                  "You are using a different version than the room you are trying to connect to");
        exit(1);
        break;
    case Network::RoomMember::Error::RoomIsFull:
        LOG_ERROR(Network, "The room is full");
        exit(1);
        break;
    case Network::RoomMember::Error::HostKicked:
        LOG_ERROR(Network, "You have been kicked by the host");
        break;
    case Network::RoomMember::Error::HostBanned:
        LOG_ERROR(Network, "You have been banned by the host");
        break;
    case Network::RoomMember::Error::UnknownError:
        LOG_ERROR(Network, "UnknownError");
        break;
    case Network::RoomMember::Error::PermissionDenied:
        LOG_ERROR(Network, "PermissionDenied");
        break;
    case Network::RoomMember::Error::NoSuchUser:
        LOG_ERROR(Network, "NoSuchUser");
        break;
    }
}

static void OnMessageReceived(const Network::ChatEntry& msg) {
    std::cout << std::endl << msg.nickname << ": " << msg.message << std::endl << std::endl;
}

static void OnStatusMessageReceived(const Network::StatusMessageEntry& msg) {
    std::string message = [&]() {
        switch (msg.type) {
        case Network::IdMemberJoin:
            return fmt::format("{} has joined", msg.nickname);
        case Network::IdMemberLeave:
            return fmt::format("{} has left", msg.nickname);
        case Network::IdMemberKicked:
            return fmt::format("{} has been kicked", msg.nickname);
        case Network::IdMemberBanned:
            return fmt::format("{} has been banned", msg.nickname);
        case Network::IdAddressUnbanned:
            return fmt::format("{} has been unbanned", msg.nickname);
        default:
            return std::string{};
        }
    }();
    if (!message.empty())
        std::cout << std::endl << "* " << message << std::endl << std::endl;
}

struct SdlState {
    Core::System system{};
    InputCommon::InputSubsystem input_subsystem{};
    std::unique_ptr<EmuWindow_SDL3> emu_window;
};

#ifdef _WIN32
// Diagnostic only: EDEN_CRASH_REPORT=1 writes a symbolized host stack and a minidump on fatal SEH.
static LONG WINAPI CrashReportFilter(EXCEPTION_POINTERS* info) {
    static volatile LONG entered = 0;
    if (InterlockedExchange(&entered, 1)) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(process, nullptr, TRUE);
    FILE* f = nullptr;
    fopen_s(&f, "crash_report.txt", "w");
    if (f) {
        fprintf(f, "exception=0x%08lx address=%p thread=%lu\n", code,
                info->ExceptionRecord->ExceptionAddress, GetCurrentThreadId());
        CONTEXT ctx = *info->ContextRecord;
        STACKFRAME64 frame{};
        frame.AddrPC.Offset = ctx.Rip;
        frame.AddrFrame.Offset = ctx.Rbp;
        frame.AddrStack.Offset = ctx.Rsp;
        frame.AddrPC.Mode = frame.AddrFrame.Mode = frame.AddrStack.Mode = AddrModeFlat;
        for (int i = 0; i < 64; ++i) {
            if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(), &frame, &ctx,
                             nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr) ||
                frame.AddrPC.Offset == 0) {
                break;
            }
            const DWORD64 pc = frame.AddrPC.Offset;
            const DWORD64 base = SymGetModuleBase64(process, pc);
            char buf[sizeof(SYMBOL_INFO) + 512]{};
            auto* sym = reinterpret_cast<SYMBOL_INFO*>(buf);
            sym->SizeOfStruct = sizeof(SYMBOL_INFO);
            sym->MaxNameLen = 511;
            DWORD64 disp = 0;
            char modname[MAX_PATH] = "?";
            if (base) {
                GetModuleFileNameA(reinterpret_cast<HMODULE>(base), modname, MAX_PATH);
            }
            if (SymFromAddr(process, pc, &disp, sym)) {
                IMAGEHLP_LINE64 line{};
                line.SizeOfStruct = sizeof(line);
                DWORD ldisp = 0;
                if (SymGetLineFromAddr64(process, pc, &ldisp, &line)) {
                    fprintf(f, "#%02d %s+0x%llx (%s:%lu) [%s +0x%llx]\n", i, sym->Name, disp,
                            line.FileName, line.LineNumber, modname, pc - base);
                } else {
                    fprintf(f, "#%02d %s+0x%llx [%s +0x%llx]\n", i, sym->Name, disp, modname,
                            pc - base);
                }
            } else {
                fprintf(f, "#%02d 0x%llx [%s +0x%llx]\n", i, pc, modname, pc - base);
            }
        }
        fclose(f);
    }
    HANDLE file = CreateFileA("crash_report.dmp", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei{GetCurrentThreadId(), info, FALSE};
        MiniDumpWriteDump(process, GetCurrentProcessId(), file, MiniDumpNormal, &mei, nullptr,
                          nullptr);
        CloseHandle(file);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

extern "C" SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv) {
#ifdef _WIN32
    if (const char* crash_report = std::getenv("EDEN_CRASH_REPORT"); crash_report && *crash_report == '1') {
        SetUnhandledExceptionFilter(CrashReportFilter);
    }
#endif
#ifdef _WIN32
#ifdef __MSVCRT__
    _setmaxstdio(2048);
#else
    _setmaxstdio(8192);
#endif
#endif

    SdlState* state = new SdlState();
    *appstate = state;
    // Emulation and presentation run on their own threads.
    SDL_SetHint(SDL_HINT_MAIN_CALLBACK_RATE, "240");

#ifdef _WIN32
#ifdef __MSVCRT__
    _setmaxstdio(2048);
#else
    _setmaxstdio(8192);
#endif
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen("CONOUT$", "wb", stdout);
        freopen("CONOUT$", "wb", stderr);
    }
#endif

    Common::Log::Initialize();
    Common::Log::SetColorConsoleBackendEnabled(true);
    Common::Log::Start();

    int option_index = 0;
#ifdef _WIN32
    int argc_w;
    auto argv_w = CommandLineToArgvW(GetCommandLineW(), &argc_w);
    if (argv_w == nullptr) {
        LOG_CRITICAL(Frontend, "Failed to get command line arguments");
        return SDL_APP_FAILURE;
    }
#else
    wchar_t **argv_w = nullptr;
#endif
    Core::LaunchParams lp = Core::ParseLaunchParams(state->system, argc, argv, argv_w);
#ifdef _WIN32
    LocalFree(argv_w);
#endif
    SdlConfig config{lp.config_path};

    Core::ApplyLaunchParams(lp);

    // Folder boots have no control data, so name the window after the folder that holds the
    // exefs (skipping base/updates/version folders). EDEN_WINDOW_TITLE wins when set.
    if (!std::getenv("EDEN_WINDOW_TITLE") && !lp.filepath.empty()) {
        std::filesystem::path dir = std::filesystem::path{lp.filepath}.parent_path();
        const auto skipped = [](const std::filesystem::path& part) {
            const std::string name = part.filename().string();
            return name.empty() || name == "exefs" || name == "base" || name == "updates" ||
                   std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
        };
        while (dir.has_filename() && skipped(dir)) {
            dir = dir.parent_path();
        }
        if (dir.has_filename()) {
#ifdef _WIN32
            _putenv_s("EDEN_WINDOW_TITLE", dir.filename().string().c_str());
#else
            setenv("EDEN_WINDOW_TITLE", dir.filename().string().c_str(), 0);
#endif
        }
    }

    // Scripted input for unattended smoke runs: user/tas/script0-N.txt, played at 60 Hz from launch.
    const bool tas_autostart = std::getenv("EDEN_TAS_AUTOSTART") != nullptr;
    if (tas_autostart) {
        Settings::values.tas_enable.SetValue(true);
        Settings::values.tas_loop.SetValue(false);
    }

    if (lp.filepath.empty()) {
        LOG_CRITICAL(Frontend, "Failed to load ROM: No ROM specified");
        return SDL_APP_FAILURE;
    }

    state->system.Initialize();

    // Apply the command line arguments
    state->system.ApplySettings();

    switch (Settings::values.renderer_backend.GetValue()) {
#ifdef HAS_OPENGL
    case Settings::RendererBackend::OpenGL_GLSL:
    case Settings::RendererBackend::OpenGL_GLASM:
    case Settings::RendererBackend::OpenGL_SPIRV:
        state->emu_window = std::make_unique<EmuWindow_SDL3_GL>(&state->input_subsystem, state->system, lp.fullscreen);
        break;
#endif
    case Settings::RendererBackend::Vulkan:
        state->emu_window = std::make_unique<EmuWindow_SDL3_VK>(&state->input_subsystem, state->system, lp.fullscreen);
        break;
    case Settings::RendererBackend::Null:
        state->emu_window = std::make_unique<EmuWindow_SDL3_Null>(&state->input_subsystem, state->system, lp.fullscreen);
        break;
    default:
        LOG_CRITICAL(Frontend, "Invalid renderer backend");
        return SDL_APP_FAILURE;
    }

#ifdef _WIN32
    Common::Windows::SetCurrentTimerResolutionToMaximum();
    state->system.CoreTiming().SetTimerResolutionNs(Common::Windows::GetCurrentTimerResolution());
#endif

    state->system.SetContentProvider(std::make_unique<FileSys::ContentProviderUnion>());
    state->system.SetFilesystem(std::make_shared<FileSys::RealVfsFilesystem>());
    state->system.GetFileSystemController().CreateFactories(*state->system.GetFilesystem());
    state->system.GetUserChannel().clear();

    Service::AM::FrontendAppletParameters load_parameters{
        .applet_id = Service::AM::AppletId::Application,
    };
    const Core::SystemResultStatus load_result = state->system.Load(*state->emu_window, lp.filepath, load_parameters);
    switch (load_result) {
    case Core::SystemResultStatus::Success:
        break; // Expected case
    case Core::SystemResultStatus::ErrorGetLoader:
        LOG_CRITICAL(Frontend, "Failed to obtain loader for {}!", lp.filepath);
        return SDL_APP_FAILURE;
    case Core::SystemResultStatus::ErrorLoader:
        LOG_CRITICAL(Frontend, "Failed to load ROM!");
        return SDL_APP_FAILURE;
    case Core::SystemResultStatus::ErrorNotInitialized:
        LOG_CRITICAL(Frontend, "CPUCore not initialized");
        return SDL_APP_FAILURE;
    case Core::SystemResultStatus::ErrorVideoCore:
        LOG_CRITICAL(Frontend, "Failed to initialize VideoCore!");
        return SDL_APP_FAILURE;
    default:
        const u16 loader_id = u16(Core::SystemResultStatus::ErrorLoader);
        const u16 error_id = u16(load_result) - loader_id;
        LOG_CRITICAL(Frontend,
            "While attempting to load the ROM requested, an error occurred. Please "
            "refer to the Eden wiki for more information or the Eden discord for "
            "additional help.\n\nError Code: {:04X}-{:04X}\nError Description: {}",
            loader_id, error_id, Loader::ResultStatus(error_id));
        return SDL_APP_FAILURE;
    }

    if (lp.use_multiplayer) {
        if (auto member = Network::GetRoomMember().lock()) {
            member->BindOnChatMessageReceived(OnMessageReceived);
            member->BindOnStatusMessageReceived(OnStatusMessageReceived);
            member->BindOnStateChanged(OnStateChanged);
            member->BindOnError(OnNetworkError);
            LOG_DEBUG(Network, "Start connection to {}:{} with nickname {}", lp.address, lp.port, lp.nickname);
            member->Join(lp.nickname, lp.address.c_str(), lp.port, 0, Network::NoPreferredIP, lp.password);
        } else {
            LOG_ERROR(Network, "Could not access RoomMember");
            return SDL_APP_FAILURE;
        }
    }

    // Core is loaded, start the GPU (makes the GPU contexts current to this thread)
    state->system.GPU().Start();
    state->system.GetCpuManager().OnGpuReady();

    if (Settings::values.use_disk_shader_cache.GetValue()) {
        state->system.Renderer().ReadRasterizer()->LoadDiskResources(
            state->system.GetApplicationProcessProgramID(), std::stop_token{},
            [](VideoCore::LoadCallbackStage, size_t value, size_t total) {});
    }

    // don't do anything, SDL3 already exists for us :D
    state->system.RegisterExitCallback([] {});

    // QLaunch launched applications (should) now reload shader cache.
    static std::mutex shader_cache_reload_mutex;
    state->system.RegisterApplicationChangedCallback([state](u64 changed_program_id) {
        if (!Settings::values.use_disk_shader_cache.GetValue()) {
            return;
        }

        std::scoped_lock lk{shader_cache_reload_mutex};

        LOG_INFO(Frontend, "Reloading disk shader cache for {:016X}", changed_program_id);

        state->system.Pause();
        state->system.GPU().WaitForIdle();
        state->system.GPU().ObtainContext();

        state->system.Renderer().ReadRasterizer()->LoadDiskResources(
            changed_program_id, std::stop_token{},
            [](VideoCore::LoadCallbackStage, size_t, size_t) {});

        state->system.GPU().ReleaseContext();
        state->system.Run();
    });

    void(state->system.Run());
    if (tas_autostart) {
        state->input_subsystem.GetTas()->StartStop();
        // The GUI drives Tas::UpdateThread once per rendered frame; the CLI has no such loop.
        std::thread([state] {
            for (;;) {
                std::this_thread::sleep_for(std::chrono::microseconds(16667));
                state->input_subsystem.GetTas()->UpdateThread();
            }
        }).detach();
    }
    if (state->system.DebuggerEnabled())
        state->system.InitializeDebugger();
    return SDL_APP_CONTINUE;
}
// Diagnostic only: EDEN_DUMP_THREADS_AT=<seconds> logs every guest thread of the application once,
// that many seconds after the first frontend iteration (the list is read unlocked): scheduler state, wait reason, wait address,
// saved registers and the frame-pointer chain. Used to see what a silent title is blocked on.
static void DumpGuestThreads(Core::System& system) {
    auto* process = system.ApplicationProcess();
    if (process == nullptr) {
        return;
    }
    auto& memory = system.ApplicationMemory();
    // Callee-saved registers x19..x28 plus the 32-bit words at x25 and at x21 + 8 and x21 + 0x18 (a wait loop
    // keeps its counter and wait object there): enough to map a polled counter to a memory address.
    const auto describe_callee_saved = [&memory](const auto& regs) {
        std::string text = " callee:";
        for (int i = 19; i <= 28; ++i) {
            text += fmt::format(" x{}={:#x}", i, regs[i]);
        }
        const auto word = [&memory](u64 address) -> std::string {
            if (address == 0 || (address & 3) != 0 || !memory.IsValidVirtualAddress(address + 3)) {
                return "?";
            }
            return fmt::format("{:#x}", memory.Read32(address));
        };
        text += fmt::format(" [x25]={} [x21+8]={} [x21+0x18]={}", word(regs[25]), word(regs[21] + 8),
                            word(regs[21] + 0x18));
        return text;
    };
    // Scheduler lock owner and what each physical core is running: a thread that is runnable
    // but never runs usually waits on the scheduler lock or on a core stuck in a handler.
    auto& kernel = system.Kernel();
    const auto& scheduler_lock = kernel.GlobalSchedulerContext().SchedulerLock();
    const auto* lock_owner = scheduler_lock.GetOwnerForDebugging();
    LOG_INFO(Frontend, "scheduler lock owner thread={} count={}",
             lock_owner != nullptr ? static_cast<s64>(lock_owner->GetThreadId()) : -1,
             scheduler_lock.GetLockCountForDebugging());
    for (std::size_t core = 0; core < Core::Hardware::NUM_CPU_CORES; ++core) {
        const auto* current = kernel.Scheduler(core).GetSchedulerCurrentThread();
        const auto* idle = kernel.Scheduler(core).GetIdleThread();
        LOG_INFO(Frontend, "core {} current thread={} idle={} disable_dispatch={}", core,
                 current != nullptr ? static_cast<s64>(current->GetThreadId()) : -1,
                 current == idle, current != nullptr ? current->GetDisableDispatchCount() : -1);
        // The saved context of a running thread is stale; read the live registers of busy cores.
        Kernel::Svc::ThreadContext live{};
        if (current != nullptr && current != idle &&
            kernel.PhysicalCore(core).GetLiveContextForDebugging(live)) {
            LOG_INFO(Frontend, "core {} live pc={:#x} lr={:#x} sp={:#x} x0={:#x} x1={:#x} x2={:#x}{}", core,
                     live.pc, live.lr, live.sp, live.r[0], live.r[1], live.r[2],
                     describe_callee_saved(live.r));
        }
    }
    // EDEN_DIAG_SVC=1 only: HLE service requests running or waiting for the service lock.
    if (const auto in_flight = Service::DescribeInFlightRequests(); !in_flight.empty()) {
        LOG_INFO(Frontend, "in-flight HLE requests:\n{}", in_flight);
    }
    for (auto& thread : process->GetThreadList()) {
        const auto& ctx = thread.GetContext();
        std::string frames;
        u64 fp = ctx.fp;
        for (int i = 0; i < 12 && fp != 0 && (fp & 7) == 0 && memory.IsValidVirtualAddress(fp + 15); ++i) {
            frames += fmt::format(" {:#x}", memory.Read64(fp + 8));
            const u64 next = memory.Read64(fp);
            if (next <= fp) {
                break;
            }
            fp = next;
        }
        LOG_INFO(Frontend,
                 "thread id={} prio={} state={:#x} wait={} key={:#x} pc={:#x} lr={:#x} sp={:#x} "
                 "x0={:#x} x1={:#x} x2={:#x} frames:{}{}",
                 thread.GetThreadId(), thread.GetPriority(), static_cast<u32>(thread.GetState()),
                 static_cast<u32>(thread.GetWaitReasonForDebugging()), GetInteger(thread.GetAddressKey()),
                 ctx.pc, ctx.lr, ctx.sp, ctx.r[0], ctx.r[1], ctx.r[2], frames,
                 Kernel::Svc::DescribeDiagSvc(thread.GetThreadId()) +
                     (static_cast<u32>(thread.GetState()) == 2 ? describe_callee_saved(ctx.r) : std::string{}));
    }
    // A stalled guest logs nothing afterwards and the log is cut when the process is killed, so pad
    // the tail to push the lines above out of the log buffer.
    for (int i = 0; i < 200; ++i) {
        LOG_INFO(Frontend, "dump end padding {:3} ------------------------------------------------------------------------------------------------------------------------------------", i);
    }
}

extern "C" SDL_AppResult SDL_AppIterate(void *appstate) {
    SdlState *state = (SdlState *)appstate;
    static const char *dump_at = std::getenv("EDEN_DUMP_THREADS_AT");
    static const auto dump_start = std::chrono::steady_clock::now();
    static bool dumped = dump_at == nullptr;
    if (!dumped && std::chrono::steady_clock::now() - dump_start >=
                       std::chrono::seconds{std::atoi(dump_at)}) {
        dumped = true;
        DumpGuestThreads(state->system);
    }
    return state->emu_window->IsOpen() ? SDL_APP_CONTINUE : SDL_APP_SUCCESS;
}
extern "C" SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    SdlState *state = (SdlState *)appstate;
    state->emu_window->OnEvent(*event);
    return SDL_APP_CONTINUE;
}
extern "C" void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    SdlState *state = (SdlState *)appstate;
    if (!state) {
        return;
    }
    state->system.DetachDebugger();
    void(state->system.Pause());
    state->system.ShutdownMainProcess();
    delete state;
}

#define VMA_IMPLEMENTATION
#include "video_core/vulkan_common/vma.h"
