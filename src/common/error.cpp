// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2013 Dolphin Emulator Project
// SPDX-FileCopyrightText: 2014 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstddef>
#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#endif

#include "common/error.h"

namespace Common {

// glibc, mlibc, musl, and newlib all define their own variants of strerror_r
// We don't need to use the preprocessor, we can just select depending on return type
template<typename T> std::string HandleStrerrorR(T r, char *err_str);
template<> std::string HandleStrerrorR(char* r, char *) { return std::string{r}; }
template<> std::string HandleStrerrorR(const char* r, char *) { return std::string{r}; }
template<> std::string HandleStrerrorR(int r, char *err_str) {
    return std::string{r != 0
        ? "(strerror_r failed to format error)"
        : err_str};
}

std::string NativeErrorToString(int e) {
#ifdef _WIN32
    LPSTR err_str;
    DWORD res = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                   FORMAT_MESSAGE_IGNORE_INSERTS,
                               nullptr, e, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                               LPSTR(&err_str), 1, nullptr);
    if (res) {
        std::string ret(err_str);
        LocalFree(err_str);
        return ret;
    }
    return "(FormatMessageA failed to format error)";
#else
    char err_str[255];
    return HandleStrerrorR(strerror_r(e, err_str, sizeof(err_str)), err_str);
#endif // _WIN32
}

std::string GetLastErrorMsg() {
#ifdef _WIN32
    return NativeErrorToString(GetLastError());
#else
    return NativeErrorToString(errno);
#endif
}

} // namespace Common
