// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <regex>
#include <cctype>

#include "common/assert.h"
#include "common/logging.h"
#include "common/settings.h"
#include "common/string_util.h"
#include "common/scm_rev.h"
#include "core/core.h"
#include "core/hle/service/acc/profile_manager.h"
#include "core/launch_params.h"

#undef _UNICODE
#include <getopt.h>
#ifndef _MSC_VER
#include <unistd.h>
#endif

namespace Core {
LaunchParams ParseLaunchParams(Core::System& system, int argc, char *argv[], wchar_t *argv_w[]) noexcept {
    LaunchParams p{};

    int option_index = 0;
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
        {0, 0, 0, 0},
        // clang-format on
    };

    while (optind < argc) {
        if (int arg = getopt_long(argc, argv, "dc:fhg:m:p:u:vinsx", long_options, &option_index); arg != -1) {
            switch (char(arg)) {
            case 'd':
                p.override_gdb_port = uint16_t(atoi(optarg));
                break;
            case 'c':
                p.config_path = optarg;
                break;
            case 'f':
                p.fullscreen = true;
                LOG_INFO(Frontend, "Starting in fullscreen mode...");
                break;
            case 'h':
                p.print_help = true;
                break;
            case 'g':
                p.filepath = std::string(optarg);
                break;
            case 'i': {
                p.input_profile = std::string(optarg);
                break;
            }
            case 'm': {
                p.use_multiplayer = true;
                const std::string str_arg(optarg);
                // regex to check if the format is nickname:password@ip:port
                // with optional :password
                const std::regex re("^([^:]+)(?::(.+))?@([^:]+)(?::([0-9]+))?$");
                if (std::regex_match(str_arg, re)) {
                    std::smatch match;
                    std::regex_search(str_arg, match, re);
                    ASSERT(match.size() == 5);
                    p.nickname = match[1];
                    p.password = match[2];
                    p.address = match[3];
                    if (!match[4].str().empty()) {
                        p.port = u16(std::strtoul(match[4].str().c_str(), nullptr, 0));
                    }
                    std::regex nickname_re("^[a-zA-Z0-9._\\- ]+$");
                    ASSERT(std::regex_match(p.nickname, nickname_re) && "Nickname is not valid. Must be 4 to 20 alphanumeric characters");
                    ASSERT(!p.address.empty() && "Address is empty");
                }
                break;
            }
            case 'p':
                p.program_args = argv[optind];
                ++optind;
                break;
            case 'u': {
                // Launch game with a specific user
                bool argument_ok = isdigit(optarg[0]);
                p.selected_user = atoi(optarg);
                if (!argument_ok) {
                    // try to look it up by username, only finds the first username that matches.
                    auto const user_idx = system.GetProfileManager().GetUserIndex(optarg);
                    if (user_idx != std::nullopt) {
                        p.selected_user = user_idx.value();
                    } else {
                        LOG_ERROR(Frontend, "Invalid user argument '{}'", optarg);
                        break;
                    }
                }
                if (system.GetProfileManager().UserExistsIndex(*p.selected_user)) {
                    Settings::values.current_user = s32(*p.selected_user);
                } else {
                    LOG_ERROR(Frontend, "Selected user {} doesn't exist", p.selected_user);
                }
                break;
            }
            case 'v':
                p.print_version = true;
                break;
            case 'n':
                p.force_null_render = true;
                break;
            case 's':
                p.force_single_core = true;
                break;
            case 'x':
                p.log_filter = argv[optind];
                ++optind;
                break;
            }
        } else {
            // only kept due to shortcuts made by Qt frontend which use "-qlaunch"
            if (strcmp(argv[optind], "-hlaunch") == 0) {
                p.launch_hlaunch = true;
            } else if (strcmp(argv[optind], "-qlaunch") == 0) {
                p.launch_qlaunch = true;
            } else if (strcmp(argv[optind], "-setup") == 0) {
                p.launch_setup = true;
            } else {
                // qt feeds utf8 data, sdl frontend feeds raw utf16 data
#ifdef _WIN32
                p.filepath = argv_w != nullptr
                    ? Common::UTF16ToUTF8(argv_w[optind])
                    : argv[optind];
#else
                p.filepath = argv[optind];
#endif
            }
            optind++;
        }
    }
    p.argv0 = argv[0];
    return p;
}

void ApplyLaunchParams(LaunchParams const& lp) noexcept {
    // apply the log_filter setting
    // the logger was initialized before and doesn't pick up the filter on its own
    Common::Log::Filter filter{};
    filter.ParseFilterString(lp.log_filter.value_or(Settings::values.log_filter.GetValue()));
    Common::Log::SetGlobalFilter(filter);

    if (!lp.program_args.empty()) {
        Settings::values.program_args = lp.program_args;
    }
    if (!lp.input_profile.empty()) {
        auto& players = Settings::values.players.GetValue();
        players[0].profile_name = lp.input_profile;
    }
    if (lp.selected_user.has_value()) {
        Settings::values.current_user = std::clamp(*lp.selected_user, 0, 7);
    }
    if (lp.override_gdb_port.has_value()) {
        Settings::values.use_gdbstub = true;
        Settings::values.gdbstub_port = *lp.override_gdb_port;
    }
    if (lp.force_single_core) {
        Settings::values.use_multi_core = false;
    }
    if (lp.force_null_render) {
        Settings::values.renderer_backend = Settings::RendererBackend::Null;
    }

    if (lp.print_version) {
        LOG_INFO(Frontend, "Eden {} {}", Common::g_scm_branch, Common::g_scm_desc);
    }
    if (lp.print_help) {
        LOG_INFO(Frontend,
            "Usage: {}"
            " [options] <filename>\n"
            "-c, --config          Load the specified configuration file\n"
            "-f, --fullscreen      Start in fullscreen mode\n"
            "-g, --game            File path of the game to load\n"
            "-h, --help            Display this help and exit\n"
            "-m, --multiplayer=nick:password@address:port"
            " Nickname, password, address and port for multiplayer\n"
            "-p, --program         Pass following string as arguments to executable\n"
            "-u, --user            Select a specific user profile from 0 to 7\n"
            "-d, --debug           Run the GDB stub on a port from 1 to 65535\n"
            "-v, --version         Output version information and exit\n",
            lp.argv0
        );
    }
}

}
