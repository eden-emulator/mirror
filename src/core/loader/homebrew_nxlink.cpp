// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/loader/homebrew_nxlink.h"

#include <cctype>

namespace Loader::HomebrewNxlink {
namespace {

std::optional<std::string> GetLastArgvToken(std::string_view argv_string) {
    while (!argv_string.empty() && argv_string.back() == '\0') {
        argv_string.remove_suffix(1);
    }

    std::optional<std::string_view> last_token;
    bool in_token = false;
    bool quoted = false;
    size_t token_begin = 0;
    size_t token_size = 0;

    for (size_t i = 0; i <= argv_string.size(); i++) {
        const char c = i < argv_string.size() ? argv_string[i] : '\0';

        if (!in_token) {
            if (c == '\0' || std::isspace(static_cast<unsigned char>(c))) {
                continue;
            }

            in_token = true;
            token_size = 0;
            if (c == '"') {
                quoted = true;
                token_begin = i + 1;
            } else {
                quoted = false;
                token_begin = i;
                token_size = 1;
            }
            continue;
        }

        const bool token_end =
            quoted ? c == '"' || c == '\0'
                   : c == '\0' || std::isspace(static_cast<unsigned char>(c));
        if (token_end) {
            if (token_size != 0) {
                last_token = argv_string.substr(token_begin, token_size);
            }
            in_token = false;
            quoted = false;
            token_size = 0;
            continue;
        }

        token_size++;
    }

    if (!last_token) {
        return std::nullopt;
    }
    return std::string{*last_token};
}

void AppendArgvToken(std::string& argv_string, std::string_view token) {
    while (!argv_string.empty() && argv_string.back() == '\0') {
        argv_string.pop_back();
    }
    if (!argv_string.empty()) {
        argv_string.push_back(' ');
    }
    argv_string.append(token);
}

} // namespace

bool IsArgvMarker(std::string_view token) {
    if (token.size() != ArgvMarkerSize || token.substr(8) != ArgvMarkerSuffix) {
        return false;
    }

    for (size_t i = 0; i < 8; i++) {
        if (!std::isxdigit(static_cast<unsigned char>(token[i]))) {
            return false;
        }
    }

    return true;
}

std::optional<std::string> GetArgvMarker(std::string_view argv_string) {
    auto last_token = GetLastArgvToken(argv_string);
    if (!last_token || !IsArgvMarker(*last_token)) {
        return std::nullopt;
    }
    return last_token;
}

std::optional<std::string> PrepareArgv(std::string& argv_string,
                                       std::string_view inherited_marker) {
    auto marker = GetArgvMarker(argv_string);
    if (!marker && IsArgvMarker(inherited_marker)) {
        AppendArgvToken(argv_string, inherited_marker);
        marker = std::string{inherited_marker};
    }
    return marker;
}

} // namespace Loader::HomebrewNxlink
