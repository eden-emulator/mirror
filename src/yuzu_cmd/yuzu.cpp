// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2014 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <iostream>
#include <memory>
#include <regex>
#include <string>
#include <SDL3/SDL_init.h>
#include <openssl/evp.h>
#include "common/fs/file.h"
#include "common/settings_enums.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL_main.h>

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
#include "core/hle/service/am/applet_manager.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/loader/loader.h"
#include "frontend_common/config.h"
#include "input_common/main.h"
#include "network/announce_multiplayer_session.h"
#include "network/network.h"
#include "network/room.h"
#include "network/verify_user.h"
#ifdef ENABLE_WEB_SERVICE
#include "web_service/verify_user_jwt.h"
#endif

#include "yuzu_cmd/sdl_config.h"
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
#include "common/windows/timer_resolution.h"
#endif

#undef _UNICODE
#include <getopt.h>
#ifndef _MSC_VER
#include <unistd.h>
#endif

#ifdef _WIN32
extern "C" {
// tells Nvidia and AMD drivers to use the dedicated GPU by default on laptops with switchable
// graphics
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

static void PrintHelp(const char* argv0) {
    LOG_INFO(Frontend, "Usage: {} [options] <filename>\n"
        "Core options:\n"
        "-c, --config            Load the specified configuration file\n"
        "-f, --fullscreen        Start in fullscreen mode\n"
        "-g, --game              File path of the game to load\n"
        "-h, --help              Display this help and exit\n"
        "-m, --multiplayer=nick:password@address:port Nickname, password, address and port for multiplayer\n"
        "-p, --program           Pass following string as arguments to executable\n"
        "-u, --user              Select a specific user profile from 0 to 7\n"
        "-d, --debug             Run the GDB stub on a port from 1 to 65535\n"
        "-i, --input-profile     Specifies input profile name to use (for player #0 only)\n"
        "-n, --null-render       Forces the usage of the \"Null\" render backend irrespective of settings\n"
        "-x, --filter            Sets the debug log filter irrespective of settings\n"
        "-s, --singlecore        Forces single-core regardless of settings\n"
        "Shared options:\n"
        "-l, --log-file          The file for storing the room log\n"
        "-H, --headless          Force headless mode (no GUI). Currently only used for rooms\n"
        "Room options:\n"
        "-N, --name              The name of the room\n"
        "-D, --description       The room description\n"
        "-S, --bind-address      The bind address for the room\n"
        "-P, --port              The port used for the room\n"
        "-M, --max-members       The maximum number of players for this room\n"
        "-W, --password          The password for the room\n"
        "-G, --preferred-game    The preferred game for this room\n"
        "-I, --preferred-game-id The preferred game-id for this room\n"
        "-U, --username          The username used for announce\n"
        "-T, --token             The token used for announce\n"
        "-A, --web-api-url       yuzu Web API url\n"
        "-B, --ban-list-file     The file for storing the room ban list\n"
        "Misc. options:\n"
        "-h, --help              Display this help and exit\n"
        "-v, --version           Output version information and exit\n",
        argv0);
}

static void PrintVersion() {
    LOG_INFO(Frontend, "Eden {} {}, Libnetwork: {}", Common::g_scm_branch, Common::g_scm_desc, Network::network_version);
}

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
    std::unique_ptr<EmuWindow_SDL3> emu_window;
    // settings
    struct {
        std::optional<std::string> config_path{};
        std::optional<std::string> log_filter{};
        std::string nickname{};
        std::string password{};
        std::string address{};
        std::string input_profile{};
        std::string filepath{};
        std::string program_args{};
        std::string room_name{};
        std::string room_description{};
        std::string preferred_game{};
        std::string username{};
        std::string token{};
        std::string web_api_url{};
        std::string ban_list_file{};
        std::string log_file = "eden-room.log";
        std::string bind_address{};
        std::optional<int> selected_user{};
        std::optional<u16> override_gdb_port{};
        std::optional<bool> headless{};
        u64 preferred_game_id = 0;
        u32 max_members = 16;
        u16 port = Network::DefaultRoomPort;
        bool use_multiplayer = false;
        bool fullscreen = false;
        bool force_null_render = false;
        bool force_single_core = false;
    } opts = {};
};

static SDL_AppResult ExecuteWithGUI(SdlState& state) {
    SdlConfig config{state.opts.config_path};

    // apply the log_filter setting
    // the logger was initialized before and doesn't pick up the filter on its own
    Common::Log::Filter filter;
    filter.ParseFilterString(state.opts.log_filter.value_or(Settings::values.log_filter.GetValue()));
    Common::Log::SetGlobalFilter(filter);

    if (!state.opts.program_args.empty()) {
        Settings::values.program_args = state.opts.program_args;
    }

    if (!state.opts.input_profile.empty()) {
        auto& players = Settings::values.players.GetValue();
        players[0].profile_name = state.opts.input_profile;
    }

    if (state.opts.selected_user.has_value()) {
        Settings::values.current_user = std::clamp(*state.opts.selected_user, 0, 7);
    }

    if (state.opts.override_gdb_port.has_value()) {
        Settings::values.use_gdbstub = true;
        Settings::values.gdbstub_port = *state.opts.override_gdb_port;
    }

    if (state.opts.force_single_core) {
        Settings::values.use_multi_core = false;
    }

    if (state.opts.force_null_render) {
        Settings::values.renderer_backend = Settings::RendererBackend::Null;
    }

#ifdef _WIN32
    LocalFree(argv_w);
#endif

    if (state.opts.filepath.empty()) {
        LOG_CRITICAL(Frontend, "Failed to load ROM: No ROM specified");
        return SDL_APP_FAILURE;
    }

    state.system.Initialize();

    InputCommon::InputSubsystem input_subsystem{};

    // Apply the command line arguments
    state.system.ApplySettings();

    switch (Settings::values.renderer_backend.GetValue()) {
#ifdef HAS_OPENGL
    case Settings::RendererBackend::OpenGL_GLSL:
    case Settings::RendererBackend::OpenGL_GLASM:
    case Settings::RendererBackend::OpenGL_SPIRV:
        state.emu_window = std::make_unique<EmuWindow_SDL3_GL>(&input_subsystem, state.system, state.opts.fullscreen);
        break;
#endif
    case Settings::RendererBackend::Vulkan:
        state.emu_window = std::make_unique<EmuWindow_SDL3_VK>(&input_subsystem, state.system, state.opts.fullscreen);
        break;
    case Settings::RendererBackend::Null:
        state.emu_window = std::make_unique<EmuWindow_SDL3_Null>(&input_subsystem, state.system, state.opts.fullscreen);
        break;
    default:
        LOG_CRITICAL(Frontend, "Invalid renderer backend");
        return SDL_APP_FAILURE;
    }

#ifdef _WIN32
    Common::Windows::SetCurrentTimerResolutionToMaximum();
    state.system.CoreTiming().SetTimerResolutionNs(Common::Windows::GetCurrentTimerResolution());
#endif

    state.system.SetContentProvider(std::make_unique<FileSys::ContentProviderUnion>());
    state.system.SetFilesystem(std::make_shared<FileSys::RealVfsFilesystem>());
    state.system.GetFileSystemController().CreateFactories(*state.system.GetFilesystem());
    state.system.GetUserChannel().clear();

    Service::AM::FrontendAppletParameters load_parameters{
        .applet_id = Service::AM::AppletId::Application,
    };
    const Core::SystemResultStatus load_result = state.system.Load(*state.emu_window, state.opts.filepath, load_parameters);
    switch (load_result) {
    case Core::SystemResultStatus::Success:
        break; // Expected case
    case Core::SystemResultStatus::ErrorGetLoader:
        LOG_CRITICAL(Frontend, "Failed to obtain loader for {}!", state.opts.filepath);
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

    if (state.opts.use_multiplayer) {
        if (auto member = Network::GetRoomMember().lock()) {
            member->BindOnChatMessageReceived(OnMessageReceived);
            member->BindOnStatusMessageReceived(OnStatusMessageReceived);
            member->BindOnStateChanged(OnStateChanged);
            member->BindOnError(OnNetworkError);
            LOG_DEBUG(Network, "Start connection to {}:{} with nickname {}", state.opts.address, state.opts.port, state.opts.nickname);
            member->Join(state.opts.nickname, state.opts.address.c_str(), state.opts.port, 0, Network::NoPreferredIP, state.opts.password);
        } else {
            LOG_ERROR(Network, "Could not access RoomMember");
            return SDL_APP_FAILURE;
        }
    }

    // Core is loaded, start the GPU (makes the GPU contexts current to this thread)
    state.system.GPU().Start();
    state.system.GetCpuManager().OnGpuReady();

    if (Settings::values.use_disk_shader_cache.GetValue()) {
        state.system.Renderer().ReadRasterizer()->LoadDiskResources(
            state.system.GetApplicationProcessProgramID(), std::stop_token{},
            [](VideoCore::LoadCallbackStage, size_t value, size_t total) {});
    }

    // don't do anything, SDL3 already exists for us :D
    state.system.RegisterExitCallback([] {});
    void(state.system.Run());
    if (state.system.DebuggerEnabled())
        state.system.InitializeDebugger();
    return SDL_APP_SUCCESS;
}

/// The magic text at the beginning of a yuzu-room ban list file.
static constexpr char BAN_LIST_MAGIC[] = "YuzuRoom-BanList-1";
static constexpr char TOKEN_DELIMITER{':'};

static void PadToken(std::string& token) {
    std::array<unsigned char, 512> output{};
    std::array<unsigned char, 2048> roundtrip{};
    for (size_t i = 0; i < 3; i++) {
        EVP_DecodeBlock(output.data(), reinterpret_cast<const unsigned char*>(token.c_str()), token.size());
        EVP_EncodeBlock(output.data(), roundtrip.data(), roundtrip.size());
        if (memcmp(roundtrip.data(), token.data(), token.size()) == 0) {
            break;
        }
        token.push_back('=');
    }
}

static std::string UsernameFromDisplayToken(const std::string& display_token) {
    std::size_t outlen = 4 * ((display_token.length() + 2) / 3);
    std::array<unsigned char, 512> output{};
    EVP_DecodeBlock(output.data(), reinterpret_cast<const unsigned char*>(display_token.c_str()), display_token.length());
    std::string decoded_display_token(reinterpret_cast<char*>(&output), outlen);
    return decoded_display_token.substr(0, decoded_display_token.find(TOKEN_DELIMITER));
}

static std::string TokenFromDisplayToken(const std::string& display_token) {
    std::size_t outlen = 4 * ((display_token.length() + 2) / 3);
    std::array<unsigned char, 512> output{};
    EVP_DecodeBlock(output.data(), reinterpret_cast<const unsigned char*>(display_token.c_str()), display_token.length());
    std::string decoded_display_token(reinterpret_cast<char*>(&output), outlen);
    return decoded_display_token.substr(decoded_display_token.find(TOKEN_DELIMITER) + 1);
}

static Network::Room::BanList LoadBanList(const std::string& path) {
    std::ifstream file;
    Common::FS::OpenFileStream(file, path, std::ios_base::in);
    if (!file || file.eof()) {
        LOG_ERROR(Network, "Could not open ban list!");
        return {};
    }
    std::string magic;
    std::getline(file, magic);
    if (magic != BAN_LIST_MAGIC) {
        LOG_ERROR(Network, "Ban list is not valid!");
        return {};
    }

    // false = username ban list, true = ip ban list
    bool ban_list_type = false;
    Network::Room::UsernameBanList username_ban_list;
    Network::Room::IPBanList ip_ban_list;
    while (!file.eof()) {
        std::string line;
        std::getline(file, line);
        line.erase(std::remove(line.begin(), line.end(), '\0'), line.end());
        line = Common::StripSpaces(line);
        if (line.empty()) {
            // An empty line marks start of the IP ban list
            ban_list_type = true;
            continue;
        }
        if (ban_list_type) {
            ip_ban_list.emplace_back(line);
        } else {
            username_ban_list.emplace_back(line);
        }
    }

    return {username_ban_list, ip_ban_list};
}

static void SaveBanList(const Network::Room::BanList& ban_list, const std::string& path) {
    std::ofstream file;
    Common::FS::OpenFileStream(file, path, std::ios_base::out);
    if (!file) {
        LOG_ERROR(Network, "Could not save ban list!");
        return;
    }

    file << BAN_LIST_MAGIC << "\n";

    // Username ban list
    for (const auto& username : ban_list.first) {
        file << username << "\n";
    }
    file << "\n";

    // IP ban list
    for (const auto& ip : ban_list.second) {
        file << ip << "\n";
    }
}

static SDL_AppResult ExecuteHeadlessRoom(SdlState& state) {
    if (state.opts.room_name.empty()) {
        LOG_ERROR(Network, "Room name is empty!");
        return SDL_APP_FAILURE;
    }
    if (state.opts.preferred_game.empty()) {
        LOG_ERROR(Network, "Preferred game is empty!");
        return SDL_APP_FAILURE;
    }
    if (state.opts.preferred_game_id == 0) {
        LOG_WARNING(Network,
            "preferred-game-id not set!\n"
            "This should get set to allow users to find your room.\n"
            "Set with --preferred-game-id id");
    }
    if (state.opts.bind_address.empty()) {
        LOG_INFO(Network, "Bind address is empty: defaulting to 0.0.0.0");
    }
    if (state.opts.ban_list_file.empty()) {
        LOG_WARNING(Network,
            "Ban list file not set!\n"
            "This should get set to load and save room ban list.\n"
            "Set with --ban-list-file <file>");
    }
    bool announce = true;
    if (state.opts.token.empty() && announce) {
        announce = false;
        LOG_INFO(Network, "Token is empty: Hosting a private room");
    }
    if (state.opts.web_api_url.empty() && announce) {
        announce = false;
        LOG_INFO(Network, "Endpoint url is empty: Hosting a private room");
    }
    if (announce) {
        if (state.opts.username.empty()) {
            LOG_INFO(Network, "Hosting a public room");
            Settings::values.web_api_url = state.opts.web_api_url;
            PadToken(state.opts.token);
            Settings::values.eden_username = UsernameFromDisplayToken(state.opts.token);
            state.opts.username = Settings::values.eden_username.GetValue();
            Settings::values.eden_token = TokenFromDisplayToken(state.opts.token);
        } else {
            LOG_INFO(Network, "Hosting a public room");
            Settings::values.web_api_url = state.opts.web_api_url;
            Settings::values.eden_username = state.opts.username;
            Settings::values.eden_token = state.opts.token;
        }
    }

    // Load the ban list
    Network::Room::BanList ban_list;
    if (!state.opts.ban_list_file.empty()) {
        ban_list = LoadBanList(state.opts.ban_list_file);
    }

    std::unique_ptr<Network::VerifyUser::Backend> verify_backend;
    if (announce) {
#ifdef ENABLE_WEB_SERVICE
        verify_backend =
            std::make_unique<WebService::VerifyUserJWT>(Settings::values.web_api_url.GetValue());
#else
        LOG_INFO(Network,
                 "Eden Web Services is not available with this build: validation is disabled.");
        verify_backend = std::make_unique<Network::VerifyUser::NullBackend>();
#endif
    } else {
        verify_backend = std::make_unique<Network::VerifyUser::NullBackend>();
    }

    Network::Init();
    if (auto room = Network::GetRoom().lock()) {
        AnnounceMultiplayerRoom::GameInfo preferred_game_info{
            .name = state.opts.preferred_game,
            .id = state.opts.preferred_game_id
        };
        if (!room->Create(state.opts.room_name, state.opts.room_description, state.opts.bind_address, u16(state.opts.port),
                          state.opts.password, state.opts.max_members, state.opts.username, preferred_game_info,
                          std::move(verify_backend), ban_list)) {
            LOG_INFO(Network, "Failed to create room: ");
            std::exit(-1);
        }
        LOG_INFO(Network, "Room is open. Close with Q+Enter...");
        auto announce_session = std::make_unique<Core::AnnounceMultiplayerSession>();
        if (announce) {
            announce_session->Start();
        }
        while (room->GetState() == Network::Room::State::Open) {
            std::string in;
            std::cin >> in;
            if (in.size() > 0) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (announce) {
            announce_session->Stop();
        }
        announce_session.reset();
        // Save the ban list
        if (!state.opts.ban_list_file.empty()) {
            SaveBanList(room->GetBanList(), state.opts.ban_list_file);
        }
        room->Destroy();
    }
    Network::Shutdown();
    return SDL_APP_FAILURE;
}

extern "C" SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv) {
    SdlState* state = new SdlState();

#ifdef _WIN32
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
#endif

    static struct option long_options[] = {
        // clang-format off
        {"debug", no_argument, 0, 'd'},
        {"config", required_argument, 0, 'c'},
        {"fullscreen", no_argument, 0, 'f'},
        {"help", no_argument, 0, 'h'},
        {"game", required_argument, 0, 'g'},
        {"multiplayer", required_argument, 0, 'm'},
        {"program", optional_argument, 0, 'p'},
        {"user", required_argument, 0, 'u'},
        {"version", no_argument, 0, 'v'},
        {"input-profile", no_argument, 0, 'i'},
        {"null-render", no_argument, 0, 'n'},
        {"singlecore", no_argument, 0, 's'},
        {"filter", no_argument, 0, 'x'},

        {"log-file", required_argument, 0, 'l'},
        {"headless", required_argument, 0, 'H'},

        {"room-name", required_argument, 0, 'N'},
        {"room-description", required_argument, 0, 'D'},
        {"bind-address", required_argument, 0, 'S'},
        {"port", required_argument, 0, 'P'},
        {"max-members", required_argument, 0, 'M'},
        {"password", required_argument, 0, 'W'},
        {"preferred-game", required_argument, 0, 'G'},
        {"preferred-game-id", required_argument, 0, 'I'},
        {"username", optional_argument, 0, 'U'},
        {"token", required_argument, 0, 'T'},
        {"web-api-url", required_argument, 0, 'A'},
        {"ban-list-file", required_argument, 0, 'B'},
        // Entry option
        {"room", 0, 0, 0},
        {0, 0, 0, 0},
        // clang-format on
    };

    char *endarg = nullptr;
    while (optind < argc) {
        int arg = getopt_long(argc, argv, "g:fhvcip::c:u:d:", long_options, &option_index);
        if (arg != -1) {
            switch (char(arg)) {
            case 'd':
                state->opts.override_gdb_port = uint16_t(atoi(optarg));
                break;
            case 'c':
                state->opts.config_path = optarg;
                break;
            case 'f':
                state->opts.fullscreen = true;
                LOG_INFO(Frontend, "Starting in fullscreen mode...");
                break;
            case 'h':
                PrintHelp(argv[0]);
                return SDL_APP_FAILURE;
            case 'g':
                state->opts.filepath = std::string(optarg);
                break;
            case 'i': {
                state->opts.input_profile = std::string(optarg);
                break;
            }
            case 'm': {
                state->opts.use_multiplayer = true;
                const std::string str_arg(optarg);
                // regex to check if the format is nickname:password@ip:port
                // with optional :password
                const std::regex re("^([^:]+)(?::(.+))?@([^:]+)(?::([0-9]+))?$");
                if (!std::regex_match(str_arg, re)) {
                    std::cout << "Wrong format for option --multiplayer\n";
                    PrintHelp(argv[0]);
                    return SDL_APP_FAILURE;
                }

                std::smatch match;
                std::regex_search(str_arg, match, re);
                ASSERT(match.size() == 5);
                state->opts.nickname = match[1];
                state->opts.password = match[2];
                state->opts.address = match[3];
                if (!match[4].str().empty()) {
                    state->opts.port = u16(std::strtoul(match[4].str().c_str(), nullptr, 0));
                }
                std::regex nickname_re("^[a-zA-Z0-9._\\- ]+$");
                if (!std::regex_match(state->opts.nickname, nickname_re)) {
                    LOG_ERROR(Frontend, "Nickname is not valid. Must be 4 to 20 alphanumeric characters");
                    return SDL_APP_FAILURE;
                }
                if (state->opts.address.empty()) {
                    LOG_ERROR(Frontend, "Address to room must not be empty");
                    return SDL_APP_FAILURE;
                }
                break;
            }
            case 'p':
                state->opts.program_args.assign(optarg);
                break;
            case 'u':
                state->opts.selected_user = atoi(optarg);
                break;
            case 'v':
                PrintVersion();
                return SDL_APP_FAILURE;
            case 'n':
                state->opts.force_null_render = true;
                break;
            case 's':
                state->opts.force_single_core = true;
                break;
            case 'x':
                state->opts.log_filter.emplace(optarg);
                break;
            // shared
            case 'l':
                state->opts.log_file.assign(optarg);
                break;
            case 'H':
                state->opts.headless.emplace(true);
                break;
            // room
            case 'N':
                state->opts.room_name.assign(optarg);
                break;
            case 'D':
                state->opts.room_description.assign(optarg);
                break;
            case 'S':
                state->opts.bind_address.assign(optarg);
                break;
            case 'P': {
                auto const value = strtoul(optarg, &endarg, 0);
                if (value <= USHRT_MAX) {
                    state->opts.port = value;
                } else {
                    LOG_ERROR(Frontend, "port must be between 0-{}", USHRT_MAX);
                }
                break;
            }
            case 'M': {
                auto const value = strtoul(optarg, &endarg, 0);
                if (value >= 2 && value <= Network::MaxConcurrentConnections) {
                    state->opts.max_members = value;
                } else {
                    LOG_ERROR(Frontend, "max members must be between 2-{}", value, Network::MaxConcurrentConnections);
                }
                break;
            }
            case 'W':
                state->opts.password.assign(optarg);
                break;
            case 'G':
                state->opts.preferred_game.assign(optarg);
                break;
            case 'I':
                state->opts.preferred_game_id = strtoull(optarg, &endarg, 16);
                break;
            case 'U':
                state->opts.nickname.assign(optarg);
                break;
            case 'T':
                state->opts.token.assign(optarg);
                break;
            case 'A':
                state->opts.web_api_url.assign(optarg);
                break;
            case 'B':
                state->opts.ban_list_file.assign(optarg);
                break;
            }
        } else {
#ifdef _WIN32
            state->opts.filepath = Common::UTF16ToUTF8(argv_w[optind]);
#else
            state->opts.filepath = argv[optind];
#endif
            optind++;
        }
    }

    if (!state->opts.room_name.empty() || !state->opts.room_description.empty()) {
        LOG_INFO(Frontend, "Assuming (headless) room mode");
        return ExecuteHeadlessRoom(*state);
    }
    return ExecuteWithGUI(*state);
}
extern "C" SDL_AppResult SDL_AppIterate(void *appstate) {
    SdlState *state = (SdlState *)appstate;
    return state->emu_window->IsOpen() ? SDL_APP_CONTINUE : SDL_APP_SUCCESS;
}
extern "C" SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    SdlState *state = (SdlState *)appstate;
    state->emu_window->OnEvent(*event);
    return SDL_APP_SUCCESS;
}
extern "C" void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    SdlState *state = (SdlState *)appstate;
    state->system.DetachDebugger();
    void(state->system.Pause());
    state->system.ShutdownMainProcess();
    delete state;
}

#define VMA_IMPLEMENTATION
#include "video_core/vulkan_common/vma.h"
