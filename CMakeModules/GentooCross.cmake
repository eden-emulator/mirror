# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

set(CROSS_TARGET "powerpc64le" CACHE STRING "Cross-compilation target (aarch64, powerpc64le, riscv64, etc)")

set(CMAKE_SYSROOT /usr/${CROSS_TARGET}-unknown-linux-gnu)

set(CMAKE_C_COMPILER ${CROSS_TARGET}-unknown-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER ${CROSS_TARGET}-unknown-linux-gnu-g++)

# search programs in the host environment
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)

# search headers and libraries in the target environment
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

set(CMAKE_FIND_ROOT_PATH /usr/${CROSS_TARGET}-unknown-linux-gnu)
