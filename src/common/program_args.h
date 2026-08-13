// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <optional>
#include "common/common_types.h"

namespace Common {

inline constexpr u16 DEFAULT_ROOM_PORT = 24872;
struct ProgramArguments {
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
    u16 port = DEFAULT_ROOM_PORT;
    bool use_multiplayer = false;
    bool fullscreen = false;
    bool force_null_render = false;
    bool force_single_core = false;

    bool should_launch_qlaunch = false;
    bool should_launch_hlaunch = false;
    bool should_launch_setup = false;
};

int ParseArguments(ProgramArguments& args, int argc, char *argv[]);

}
