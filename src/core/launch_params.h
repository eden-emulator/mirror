// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <optional>
#include "common/common_types.h"
#include "network/room.h"

namespace Core {

class System;
struct LaunchParams {
    std::string argv0{};
    std::string filepath{};
    std::string nickname{};
    std::string password{};
    std::string address{};
    std::string input_profile{};
    std::string program_args{};
    std::optional<std::string> config_path{};
    std::optional<int> selected_user{};
    std::optional<u16> override_gdb_port{};
    std::optional<std::string> log_filter{};
    u16 port = Network::DefaultRoomPort;
    bool use_multiplayer = false;
    bool fullscreen = false;
    bool force_null_render = false;
    bool force_single_core = false;
    // print
    bool print_version = false;
    bool print_help = false;
    // qt
    bool launch_hlaunch = false;
    bool launch_qlaunch = false;
    bool launch_setup = false;
};

LaunchParams ParseLaunchParams(Core::System& system, int argc, char *argv[], wchar_t *argv_w[]) noexcept;
void ApplyLaunchParams(LaunchParams const& lp) noexcept;

}
