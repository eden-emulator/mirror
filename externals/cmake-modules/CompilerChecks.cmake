# SPDX-FileCopyrightText: Copyright 2026 crueter
# SPDX-License-Identifier: MIT

# Vendored from Jacinth:
# https://github.com/crueter/jacinth/blob/master/cmake/CompilerChecks.cmake

set(ver ${CMAKE_CXX_COMPILER_VERSION})
set(id ${CMAKE_CXX_COMPILER_ID})

# must use a supported generator
if (NOT (CMAKE_GENERATOR MATCHES "^Visual Studio" OR
    CMAKE_GENERATOR MATCHES "Ninja"))
    message(FATAL_ERROR "You must use the Visual Studio or Ninja generator "
        "for C++20 module support.")
endif()

# AppleClang is entirely unsupported
if (id MATCHES "AppleClang")
    message(FATAL_ERROR "AppleClang is unsupported. Use Homebrew's Clang "
        "(brew install llvm) instead.")
endif()

# GCC must be 16+
if (id MATCHES "GNU" AND ver VERSION_LESS 16)
    message(FATAL_ERROR "Unsupported GCC version ${ver}. Only 16 and up "
        "are supported.")
endif()

# Clang must be 19+
if (id MATCHES "Clang")
    if(ver VERSION_LESS 19)
        message(FATAL_ERROR "Unsupported Clang version ${ver}. Only 19 and "
            "up are supported.")
    endif()

    # clang-cl
    # https://cmake.org/cmake/help/v4.4/release/4.4.html
    if (MSVC)
        # doesn't support Visual Studio
        if (CMAKE_GENERATOR MATCHES "^Visual Studio")
            message(FATAL_ERROR "clang-cl does not support the Visual Studio "
                "generator when C++20 modules are enabled. Use Ninja instead.")
        endif()

        # requires CMake 4.4 to scan for modules
        if (CMAKE_VERSION VERSION_LESS 4.4)
            message(FATAL_ERROR "clang-cl requires CMake 4.4 or newer for "
                "C++20 module support")
        endif()
    endif()
endif()

# MSVC must be 2026+ (19.50+)
if (id MATCHES "MSVC" AND ver VERSION_LESS 19.50)
    message(FATAL_ERROR "Unsupported MSVC version ${ver}. Only 2026 and "
        "up (19.50+) are supported.")
endif()