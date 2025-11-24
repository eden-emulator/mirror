# SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(powah INTERFACE)
target_include_directories(powah INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(powah INTERFACE -Wno-unused-parameter)

add_executable(powah_tests tests.cpp)
create_target_directory_groups(powah_tests)
target_link_libraries(powah_tests PRIVATE Catch2::Catch2WithMain)
