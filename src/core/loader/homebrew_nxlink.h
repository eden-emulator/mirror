// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "common/settings_enums.h"

namespace Loader::HomebrewNxlink {

constexpr size_t ArgvMarkerSize = 16;
constexpr std::string_view ArgvMarkerSuffix = "_NXLINK_";
constexpr std::string_view LoopbackArgvMarker = "0100007F_NXLINK_";

bool IsArgvMarker(std::string_view token);
bool IsLoopbackArgvMarker(std::string_view token);
std::optional<std::string> GetArgvMarker(std::string_view argv_string);
std::optional<std::string> PrepareArgv(std::string& argv_string,
                                       std::string_view inherited_marker,
                                       bool append_loopback_marker = false);

void ApplyServerMode(Settings::HomebrewNxlinkServerMode mode,
                     const std::optional<std::string>& active_marker);
void StopServer();

} // namespace Loader::HomebrewNxlink
