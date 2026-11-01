# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.12)

set(dynarmic_VERSION_MAJOR 7)
set(dynarmic_VERSION_MINOR 0)
set(dynarmic_VERSION_PATCH 0)

set(dynarmic_VERSION ${dynarmic_VERSION_MAJOR}.${dynarmic_VERSION_MINOR}.${dynarmic_VERSION_PATCH})

project(dynarmic LANGUAGES C CXX ASM VERSION ${dynarmic_VERSION})

if (OPENBSD OR DRAGONFLY OR NETBSD)
    set(REQUIRE_WX ON)
else()
    set(REQUIRE_WX OFF)
endif()
option(DYNARMIC_ENABLE_NO_EXECUTE_SUPPORT "Enables support for systems that require W^X" ${REQUIRE_WX})
option(DYNARMIC_TESTS_USE_UNICORN "Enable fuzzing tests against unicorn" OFF)
CMAKE_DEPENDENT_OPTION(DYNARMIC_USE_LLVM "Support disassembly of jitted x86_64 code using LLVM" OFF "NOT YUZU_DISABLE_LLVM" OFF)

# Set hard requirements for C++
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Disable in-source builds
# set(CMAKE_DISABLE_SOURCE_CHANGES ON)
# set(CMAKE_DISABLE_IN_SOURCE_BUILD ON)
if ("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
    message(SEND_ERROR "In-source builds are not allowed.")
endif()

# Add the module directory to the list of paths
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMakeModules")

# Compiler flags
if (MSVC)
    set(DYNARMIC_CXX_FLAGS
        /experimental:external
        /external:W0
        /external:anglebrackets
        /W4
        /w44263 # Non-virtual member function hides base class virtual function
        /w44265 # Class has virtual functions, but destructor is not virtual
        /w44456 # Declaration of 'var' hides previous local declaration
        /w44457 # Declaration of 'var' hides function parameter
        /w44458 # Declaration of 'var' hides class member
        /w44459 # Declaration of 'var' hides global definition
        /w44946 # Reinterpret-cast between related types
        /wd4592 # Symbol will be dynamically initialized (implementation limitation)
        /permissive- # Stricter C++ standards conformance
        /MP
        /Zo
        /EHsc
        /Zc:externConstexpr # Allows external linkage for variables declared "extern constexpr", as the standard permits.
        /Zc:inline          # Omits inline functions from object-file output.
        /Zc:throwingNew     # Assumes new (without std::nothrow) never returns null.
        /volatile:iso       # Use strict standard-abiding volatile semantics
        /bigobj             # Increase number of sections in .obj files
        /DNOMINMAX
        /GR-
    )

    if (CXX_CLANG)
        list(APPEND DYNARMIC_CXX_FLAGS
             -Qunused-arguments
             -Wno-missing-braces)
    endif()
else()
    list(APPEND DYNARMIC_CXX_FLAGS
        -Wall
        -Wextra
        -Wcast-qual
        -pedantic
        -Wno-missing-braces
        -fno-rtti
        #-fno-exceptions
    )
    if (CXX_GCC)
        # GCC produces bogus -Warray-bounds warnings from xbyak headers for code paths that are not
        # actually reachable.  Specifically, it happens in cases where some code casts an Operand&
        # to Address& after first checking isMEM(), and that code is inlined in a situation where
        # GCC knows that the variable is actually a Reg64.  isMEM() will never return true for a
        # Reg64, but GCC doesn't know that.
        list(APPEND DYNARMIC_CXX_FLAGS
            -Wno-array-bounds
            -Wstack-usage=4096)
    endif()
    if (CXX_CLANG)
        list(APPEND DYNARMIC_CXX_FLAGS
            # Bracket depth determines maximum size of a fold expression in Clang since 9c9974c3ccb6.
            # And this in turns limits the size of a std::array.
            -fbracket-depth=1024
            # Clang mistakenly blames CMake for using unused arguments during compilation
            -Wno-unused-command-line-argument)
    endif()
endif()

if (NOT Boost_FOUND)
    find_package(Boost 1.57 REQUIRED)
endif()
find_package(fmt 8 CONFIG)

if (ARCHITECTURE_arm64)
    find_package(oaknut 2.0.1 CONFIG)
endif()
if (ARCHITECTURE_riscv64)
    find_package(biscuit 0.9.1 REQUIRED)
endif()
if (ARCHITECTURE_loongarch64)
    find_package(lagoon REQUIRED)
endif()
if (ARCHITECTURE_x86_64)
    find_package(xbyak 7 CONFIG)
endif()

if (DYNARMIC_USE_LLVM)
    find_package(LLVM REQUIRED)
    separate_arguments(LLVM_DEFINITIONS)
endif()

# Dynarmic project files
add_subdirectory(src/dynarmic)
if (DYNARMIC_TESTS)
    find_package(Catch2 3 CONFIG)
    if (DYNARMIC_TESTS_USE_UNICORN)
        find_package(Unicorn REQUIRED)
    endif()
    add_subdirectory(tests)
endif()
