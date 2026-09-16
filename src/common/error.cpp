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

std::string NativeErrorToString(int e) {
#ifdef _WIN32
    LPSTR err_str;

    DWORD res = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                   FORMAT_MESSAGE_IGNORE_INSERTS,
                               nullptr, e, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                               LPSTR(&err_str), 1, nullptr);
    if (!res) {
        return "(FormatMessageA failed to format error)";
    }
    std::string ret(err_str);
    LocalFree(err_str);
    return ret;
#else
    char err_str[256];
    // See https://github.com/llvm/llvm-project/blob/c8fdb5f8b93c3e1da5e2ff3ba8b18627d6147b51/openmp/runtime/src/kmp_i18n.cpp#L711
    // musl doesn't provide a macro gate but defines strerror_r() even if _GNU_SOURCE is defined
#if defined(__managarm__) || (defined(__GLIBC__) || defined(__BIONIC__)) || defined(_GNU_SOURCE)
    // Thread safe (GNU-specific)
    const char* str = strerror_r(e, err_str, sizeof(err_str));
    return std::string(str);
#else
    // Thread safe (XSI-compliant)
    int second_err = strerror_r(e, err_str, sizeof(err_str));
    if (second_err != 0) {
        return "(strerror_r failed to format error)";
    }
    return std::string(err_str);
#endif // GLIBC etc.
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
