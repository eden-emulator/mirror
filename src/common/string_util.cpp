// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2013 Dolphin Emulator Project
// SPDX-FileCopyrightText: 2014 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cctype>
#include <codecvt>
#include <locale>
#include <sstream>
#include <string_view>

#include "common/string_util.h"

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef __ANDROID__
#include <common/fs/fs_android.h>
#endif

namespace Common {

/// Make a string lowercase
std::string ToLower(const std::string_view sv) {
    std::string str{sv};
    std::transform(str.begin(), str.end(), str.begin(),
                   [](auto const c) { return char(std::tolower(c)); });
    return str;
}

/// Make a string uppercase
std::string ToUpper(const std::string_view sv) {
    std::string str{sv};
    std::transform(str.begin(), str.end(), str.begin(),
                   [](auto const c) { return char(std::toupper(c)); });
    return str;
}

bool SplitPath(const std::string& full_path, std::string* _pPath, std::string* _pFilename,
               std::string* _pExtension) {
    if (full_path.empty())
        return false;

#ifdef __ANDROID__
    if (full_path[0] != '/') {
        *_pPath = Common::FS::Android::GetParentDirectory(full_path);
        *_pFilename = Common::FS::Android::GetFilename(full_path);
        return true;
    }
#endif

    std::size_t dir_end = full_path.find_last_of("/"
// windows needs the : included for something like just "C:" to be considered a directory
#ifdef _WIN32
                                                 "\\:"
#endif
    );
    if (std::string::npos == dir_end)
        dir_end = 0;
    else
        dir_end += 1;

    std::size_t fname_end = full_path.rfind('.');
    if (fname_end < dir_end || std::string::npos == fname_end)
        fname_end = full_path.size();

    if (_pPath)
        *_pPath = full_path.substr(0, dir_end);

    if (_pFilename)
        *_pFilename = full_path.substr(dir_end, fname_end - dir_end);

    if (_pExtension)
        *_pExtension = full_path.substr(fname_end);

    return true;
}

void SplitString(const std::string& str, const char delim, std::vector<std::string>& output) {
    std::istringstream iss(str);
    output.resize(1);

    while (std::getline(iss, *output.rbegin(), delim)) {
        output.emplace_back();
    }

    output.pop_back();
}

std::string TabsToSpaces(int tab_size, std::string in) {
    std::size_t i = 0;

    while ((i = in.find('\t')) != std::string::npos) {
        in.replace(i, 1, tab_size, ' ');
    }

    return in;
}

std::string ReplaceAll(std::string result, const std::string& src, const std::string& dest) {
    std::size_t pos = 0;

    if (src == dest)
        return result;

    while ((pos = result.find(src, pos)) != std::string::npos) {
        result.replace(pos, src.size(), dest);
        pos += dest.length();
    }

    return result;
}

std::string UTF16ToUTF8(std::u16string_view input) {
    std::string result;
    result.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        uint32_t codepoint = input[i];
        // Handle surrogate pairs
        if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
            if (i + 1 < input.size()) {
                uint32_t low = input[i + 1];
                if (low >= 0xDC00 && low <= 0xDFFF) {
                    codepoint = ((codepoint - 0xD800) << 10) + (low - 0xDC00) + 0x10000;
                    ++i;
                }
            }
        }
        if (codepoint <= 0x7F) {
            result.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7FF) {
            result.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
            result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        } else if (codepoint <= 0xFFFF) {
            result.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
            result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        } else {
            result.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
            result.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
    }
    return result;
}

std::u16string UTF8ToUTF16(std::string_view input) {
    std::u16string result;
    size_t i = 0;
    while (i < input.size()) {
        uint32_t codepoint = 0;
        unsigned char c = input[i];
        size_t extra = 0;
        if ((c & 0x80) == 0) {
            codepoint = c;
            extra = 0;
        } else if ((c & 0xE0) == 0xC0) {
            codepoint = c & 0x1F;
            extra = 1;
        } else if ((c & 0xF0) == 0xE0) {
            codepoint = c & 0x0F;
            extra = 2;
        } else if ((c & 0xF8) == 0xF0) {
            codepoint = c & 0x07;
            extra = 3;
        } else {
            // Invalid UTF-8
            ++i;
            continue;
        }
        if (i + extra >= input.size()) break;
        for (size_t j = 1; j <= extra; ++j) {
            if ((input[i + j] & 0xC0) != 0x80) {
                codepoint = 0xFFFD;
                break;
            }
            codepoint = (codepoint << 6) | (input[i + j] & 0x3F);
        }
        if (codepoint <= 0xFFFF) {
            result.push_back(static_cast<char16_t>(codepoint));
        } else {
            codepoint -= 0x10000;
            result.push_back(static_cast<char16_t>(0xD800 + (codepoint >> 10)));
            result.push_back(static_cast<char16_t>(0xDC00 + (codepoint & 0x3FF)));
        }
        i += extra + 1;
    }
    return result;
}

std::u32string UTF8ToUTF32(std::string_view input) {
    std::u32string result;
    size_t i = 0;
    while (i < input.size()) {
        uint32_t codepoint = 0;
        unsigned char c = input[i];
        size_t extra = 0;
        if ((c & 0x80) == 0) {
            codepoint = c;
            extra = 0;
        } else if ((c & 0xE0) == 0xC0) {
            codepoint = c & 0x1F;
            extra = 1;
        } else if ((c & 0xF0) == 0xE0) {
            codepoint = c & 0x0F;
            extra = 2;
        } else if ((c & 0xF8) == 0xF0) {
            codepoint = c & 0x07;
            extra = 3;
        } else {
            // Invalid UTF-8
            ++i;
            continue;
        }
        if (i + extra >= input.size()) break;
        for (size_t j = 1; j <= extra; ++j) {
            if ((input[i + j] & 0xC0) != 0x80) {
                codepoint = 0xFFFD;
                break;
            }
            codepoint = (codepoint << 6) | (input[i + j] & 0x3F);
        }
        result.push_back(codepoint);
        i += extra + 1;
    }
    return result;
}

#ifdef _WIN32
static std::wstring CPToUTF16(u32 code_page, std::string_view input) {
    const auto size =
        MultiByteToWideChar(code_page, 0, input.data(), static_cast<int>(input.size()), nullptr, 0);

    if (size == 0) {
        return {};
    }

    std::wstring output(size, L'\0');

    if (size != MultiByteToWideChar(code_page, 0, input.data(), static_cast<int>(input.size()),
                                    &output[0], static_cast<int>(output.size()))) {
        output.clear();
    }

    return output;
}

std::string UTF16ToUTF8(std::wstring_view input) {
    const auto size = WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.size()),
                                          nullptr, 0, nullptr, nullptr);
    if (size == 0) {
        return {};
    }

    std::string output(size, '\0');

    if (size != WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.size()),
                                    &output[0], static_cast<int>(output.size()), nullptr,
                                    nullptr)) {
        output.clear();
    }

    return output;
}

std::wstring UTF8ToUTF16W(std::string_view input) {
    return CPToUTF16(CP_UTF8, input);
}

#endif

std::u16string U16StringFromBuffer(const u16* input, std::size_t length) {
    return std::u16string(reinterpret_cast<const char16_t*>(input), length);
}

std::string StringFromFixedZeroTerminatedBuffer(std::string_view buffer, std::size_t max_len) {
    std::size_t len = 0;
    while (len < buffer.length() && len < max_len && buffer[len] != '\0') {
        ++len;
    }
    return std::string(buffer.begin(), buffer.begin() + len);
}

std::u16string UTF16StringFromFixedZeroTerminatedBuffer(std::u16string_view buffer,
                                                        std::size_t max_len) {
    std::size_t len = 0;
    while (len < buffer.length() && len < max_len && buffer[len] != '\0') {
        ++len;
    }
    return std::u16string(buffer.begin(), buffer.begin() + len);
}

} // namespace Common
