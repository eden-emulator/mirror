// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace Loader::HomebrewNxlink {

constexpr size_t ArgvMarkerSize = 16;
constexpr std::string_view ArgvMarkerSuffix = "_NXLINK_";

// This module handles only the libnx argv marker. Client stream handling should be an
// explicit byte-sink API; the marker alone must not imply logging.
bool IsArgvMarker(std::string_view token);
std::optional<std::string> GetArgvMarker(std::string_view argv_string);
std::optional<std::string> PrepareArgv(std::string& argv_string,
                                       std::string_view inherited_marker);

} // namespace Loader::HomebrewNxlink
