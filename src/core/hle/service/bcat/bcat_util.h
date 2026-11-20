// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <iterator>

#include "core/hle/service/bcat/bcat_result.h"
#include "core/hle/service/bcat/bcat_types.h"

namespace Service::BCAT {

// For a name to be valid it must be non-empty, must have a null terminating character as the final
// char, can only contain numbers, letters, underscores and a hyphen if directory and a period if
// file.
constexpr Result VerifyNameValidInternal(std::array<char, 0x20> name, char match_char) {
    // Only the bytes before the first NUL are part of the name; games may leave stale bytes
    // after the terminator.
    const auto first_null = std::find(name.begin(), name.end(), '\0');
    const bool has_terminator = first_null != name.end();
    const auto length = static_cast<size_t>(std::distance(name.begin(), first_null));
    const auto bad_chars = std::count_if(name.begin(), first_null, [match_char](char c) {
        return !std::isalnum(static_cast<u8>(c)) && c != '_' && c != match_char;
    });
    if (!has_terminator || length == 0 || bad_chars != 0) {
        LOG_ERROR(Service_BCAT, "Name passed was invalid!");
        return ResultInvalidArgument;
    }

    return ResultSuccess;
}

constexpr Result VerifyNameValidDir(DirectoryName name) {
    return VerifyNameValidInternal(name, '-');
}

constexpr Result VerifyNameValidFile(FileName name) {
    return VerifyNameValidInternal(name, '.');
}

} // namespace Service::BCAT
