// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2013 Dolphin Emulator Project
// SPDX-FileCopyrightText: 2014 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
#include <vector>
#include "common/common_types.h"

namespace Common {

[[nodiscard]] inline std::string StringFromBuffer(std::span<const u8> data) noexcept {
    return std::string(data.begin(), std::find(data.begin(), data.end(), '\0'));
}
[[nodiscard]] inline std::string StringFromBuffer(std::span<const char> data) noexcept {
    return std::string(data.begin(), std::find(data.begin(), data.end(), '\0'));
}

// "C:/Windows/winhelp.exe" to "C:/Windows/", "winhelp", ".exe"
bool SplitPath(const std::string& full_path, std::string* _pPath, std::string* _pFilename, std::string* _pExtension);

[[nodiscard]] std::string UTF16ToUTF8(std::u16string_view input);
[[nodiscard]] std::u16string UTF8ToUTF16(std::string_view input);
[[nodiscard]] std::u32string UTF8ToUTF32(std::string_view input);

#ifdef _WIN32
[[nodiscard]] std::string UTF16ToUTF8(std::wstring_view input);
[[nodiscard]] std::wstring UTF8ToUTF16W(std::string_view str);

#endif

[[nodiscard]] std::u16string U16StringFromBuffer(const u16* input, std::size_t length);

/// Creates a std::string from a fixed-size NUL-terminated char buffer. If the buffer isn't
/// NUL-terminated then the string ends at max_len characters.
[[nodiscard]] std::string StringFromFixedZeroTerminatedBuffer(std::string_view buffer, std::size_t max_len);

/// Creates a UTF-16 std::u16string from a fixed-size NUL-terminated char buffer. If the buffer isn't
/// null-terminated, then the string ends at the greatest multiple of two less then or equal to
/// max_len_bytes.
[[nodiscard]] std::u16string UTF16StringFromFixedZeroTerminatedBuffer(std::u16string_view buffer, std::size_t max_len);

} // namespace Common
