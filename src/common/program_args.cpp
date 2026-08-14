// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <regex>
#include "common/assert.h"
#include "common/program_args.h"
#include "common/logging.h"
#include "common/scm_rev.h"
#include "common/string_util.h"
#include "network/room.h"

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

namespace Common {

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

int ParseArguments(ProgramArguments& args, int argc, char *argv[]) {
    int option_index = 0;
#ifdef _WIN32
    int argc_w;
    auto argv_w = CommandLineToArgvW(GetCommandLineW(), &argc_w);
    if (argv_w == nullptr) {
        LOG_CRITICAL(Frontend, "Failed to get command line arguments");
        return -1;
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
        {"room", no_argument, 0, 0},
        {"hlaunch", no_argument, 0, 500},
        {"qlaunch", no_argument, 0, 'q'},
        {"setup", no_argument, 0, 502},
        {0, 0, 0, 0},
        // clang-format on
    };

    // Kept for compatibility!
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-hlaunch") == 0) {
            args.should_launch_hlaunch = true;
            return 0;
        } else if (strcmp(argv[i], "-qlaunch") == 0) {
            args.should_launch_qlaunch = true;
            return 0;
        } else if (strcmp(argv[i], "-setup") == 0) {
            args.should_launch_setup = true;
            return 0;
        }
    }

    // Preserves drag and drop functionality (i.e ./eden <game path>)
    [[maybe_unused]] char *endarg = nullptr;
    while (optind < argc) {
        int arg = getopt_long(argc, argv, "g:fhvcip::c:u:d:", long_options, &option_index);
        if (arg != -1) {
            switch (arg) {
            case 'd':
                args.override_gdb_port = uint16_t(atoi(optarg));
                break;
            case 'c':
                args.config_path = optarg;
                break;
            case 'f':
                args.fullscreen = true;
                LOG_INFO(Frontend, "Starting in fullscreen mode...");
                break;
            case 'h':
                PrintHelp(argv[0]);
                return 0;
            case 'g':
                args.filepath = std::string(optarg);
                break;
            case 'i': {
                args.input_profile = std::string(optarg);
                break;
            }
            case 'm': {
                args.use_multiplayer = true;
                const std::string str_arg(optarg);
                // regex to check if the format is nickname:password@ip:port
                // with optional :password
                const std::regex re("^([^:]+)(?::(.+))?@([^:]+)(?::([0-9]+))?$");
                if (!std::regex_match(str_arg, re)) {
                    LOG_ERROR(Frontend, "Wrong format for option --multiplayer");
                    return -1;
                } else {
                    std::smatch match;
                    std::regex_search(str_arg, match, re);
                    ASSERT(match.size() == 5);
                    args.nickname = match[1];
                    args.password = match[2];
                    args.address = match[3];
                    if (!match[4].str().empty()) {
                        args.port = u16(std::strtoul(match[4].str().c_str(), nullptr, 0));
                    }
                    std::regex nickname_re("^[a-zA-Z0-9._\\- ]+$");
                    if (!std::regex_match(args.nickname, nickname_re)) {
                        LOG_ERROR(Frontend, "Nickname is not valid. Must be 4 to 20 alphanumeric characters");
                        return -1;
                    } else {
                        if (args.address.empty()) {
                            LOG_ERROR(Frontend, "Address to room must not be empty");
                            return -1;
                        }
                    }
                }
                break;
            }
            case 'p':
                args.program_args.assign(optarg);
                break;
            case 'u':
                args.selected_user = atoi(optarg);
                break;
            case 'v':
                PrintVersion();
                break;
            case 'n':
                args.force_null_render = true;
                break;
            case 's':
                args.force_single_core = true;
                break;
            case 'x':
                args.log_filter.emplace(optarg);
                break;
            // shared
            case 'l':
                args.log_file.assign(optarg);
                break;
            case 'H':
                args.headless.emplace(true);
                break;
            // room
            case 'N':
                args.room_name.assign(optarg);
                break;
            case 'D':
                args.room_description.assign(optarg);
                break;
            case 'S':
                args.bind_address.assign(optarg);
                break;
            case 'P': {
                auto const value = strtoul(optarg, &endarg, 0);
                if (value <= USHRT_MAX) {
                    args.port = value;
                } else {
                    LOG_ERROR(Frontend, "port must be between 0-{}", USHRT_MAX);
                }
                break;
            }
            case 'M': {
                auto const value = strtoul(optarg, &endarg, 0);
                if (value >= 2 && value <= Network::MaxConcurrentConnections) {
                    args.max_members = value;
                } else {
                    LOG_ERROR(Frontend, "max members must be between 2-{}", value, Network::MaxConcurrentConnections);
                }
                break;
            }
            case 'W':
                args.password.assign(optarg);
                break;
            case 'G':
                args.preferred_game.assign(optarg);
                break;
            case 'I':
                args.preferred_game_id = strtoull(optarg, &endarg, 16);
                break;
            case 'U':
                args.nickname.assign(optarg);
                break;
            case 'T':
                args.token.assign(optarg);
                break;
            case 'A':
                args.web_api_url.assign(optarg);
                break;
            case 'B':
                args.ban_list_file.assign(optarg);
                break;
            }
        } else {
#ifdef _WIN32
            args.filepath = Common::UTF16ToUTF8(argv_w[optind]);
#else
            args.filepath = argv[optind];
#endif
            optind++;
        }
    }
#ifdef _WIN32
    LocalFree(argv_w);
#endif
    return 0;
}

}
