// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "common/settings.h"

namespace Common {

/// State shared between the console-mode engine (in the main window) and the Controllers tab:
/// the UI reads and requests, the engine applies on its next tick.
struct ControllerConsoleState {
    std::mutex mutex;
    /// Live pads as (display name, identity): possible handheld controls first
    std::vector<std::pair<std::string, std::string>> pads;
    /// How many of `pads` could be a handheld's controls (the first ones)
    std::size_t builtin_pads = 0;
    /// Set by the Identify button: capture the next pad that has a button pressed
    bool identify_armed = false;
    std::chrono::steady_clock::time_point identify_armed_at{};

    /// One assigned player, as the Controllers tab lists it
    struct PlayerRow {
        int player; ///< 0-based player index
        std::string name;
        bool active;  ///< the pad has an input held (or was just tapped)
        bool leaving; ///< the controller left and nothing took its place: shown red, then gone
        int flash;    ///< 0 none, 1 green (a controller took the player), 2 red (one left)
        long long flash_since; ///< ms since epoch; the flash blinks for 1.5 seconds from here
        std::string profile;   ///< the input profile the player's controller carries ("" none)
        /// Turn Off: 0 not Bluetooth, 1 not yet (identical ones not told apart), 2 yes
        int turn_off = 0;
        /// The player its controller started as (-1: this one); a reconnect's guessed player
        int origin = -1;
        bool guessed = false;
        /// The name given to its controller ("" none): `name` is then "Name (Xbox)"
        std::string custom_name;
        /// Battery percent (-1 unknown), charging, wired
        int battery = -1;
        bool charging = false;
        bool wired = false;
    };
    /// True while an automatic assignment mode runs (the engine owns the players)
    bool engine_active = false;
    /// The players the engine assigned, in player order
    std::vector<PlayerRow> players;
    /// The handheld's own pad: its name ("" when not identified) and whether it holds a player
    std::string handheld_name;
    bool handheld_has_player = false;
    /// The Controllers tab asked to remove the handheld's controller (its ✕)
    bool builtin_remove_request = false;
    /// The removed controller auto-detect leaves alone, by name ("" when not connected)
    std::string handheld_blocked_name;
    /// Swaps requested from the settings and the overlay, for this session only
    std::vector<std::pair<int, int>> swap_requests;
    /// Players whose Bluetooth controller is to be turned off; the player is freed
    std::vector<int> remove_requests;
    /// Names given to players' controllers from the Controllers tab ("" takes it off)
    std::vector<std::pair<int, std::string>> rename_requests;
    /// Players whose controller the Controllers tab asked to rumble (a click on its row)
    std::vector<int> rumble_requests;
    /// An input profile picked for a player, put on its controller's set. No name takes it off
    /// (`restore`: the set's mappings from before this session's first profile come back).
    struct ProfileRequest {
        int player;
        std::string name;
        bool restore = false;
    };
    std::vector<ProfileRequest> profile_requests;
    /// Bypass Steam Input took effect at startup: SDL opens the real controllers
    std::atomic<bool> steam_bypassed{false};
    /// Whether Steam's virtual pads are around, and the models Steam hid that Eden opened again
    /// for their motion (vendor << 16 | product)
    std::atomic<bool> steam_pads_present{false};
    std::set<unsigned> steam_hidden_models;
    /// Bumped when the engine reloads the emulated controllers: the player tabs refresh
    unsigned long long generation = 0;
    /// Use Any Controller as Joy-Cons: each player playing as a single Joy-Con (0 no, 1 held
    /// sideways, 2 upright). Its mapping is the engine's then: the player tab can't edit it.
    std::array<int, 10> joycon_held{};
};

[[nodiscard]] inline ControllerConsoleState& GetControllerConsoleState() {
    static ControllerConsoleState instance;
    return instance;
}

/// True when Eden was launched through Steam: Steam Input then wraps the controllers in
/// virtual ones
[[nodiscard]] inline bool RunningUnderSteam() {
    static const bool launched =
        std::getenv("SteamGameId") != nullptr || std::getenv("SteamAppId") != nullptr;
    return launched;
}

/// True when Steam Input's virtual pads are the players: under Steam, not bypassed, with
/// Steam's pads around. Otherwise the real controllers are, as outside Steam.
[[nodiscard]] inline bool SteamPadsInUse() {
    const auto& state = GetControllerConsoleState();
    return RunningUnderSteam() && !state.steam_bypassed && state.steam_pads_present;
}

/// True for a controller model Steam hid that Eden opened again for its motion (never a player)
[[nodiscard]] inline bool SteamHidModel(unsigned vendor, unsigned product) {
    auto& state = GetControllerConsoleState();
    std::scoped_lock lock{state.mutex};
    return state.steam_hidden_models.contains(vendor << 16 | product);
}

/// Debug Probes: detailed controller traces in the log, for bug reports
[[nodiscard]] inline bool ControllerProbes() {
    return Settings::values.console_debug_probes.GetValue();
}

} // namespace Common
