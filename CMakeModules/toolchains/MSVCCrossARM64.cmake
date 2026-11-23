# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

# aarch64 MSVC cross-comp thru clang-cl + xwin
# TODO for future: automatically fetch and install xwin

#[[
xwin --accept-license --arch aarch64 --manifest-version 18 \
  splat --preserve-ms-arch-notation --include-debug-libs --output xwin/arm
]]

# basic setup
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR arm64)

set(CMAKE_C_COMPILER clang-cl)
set(CMAKE_CXX_COMPILER clang-cl)
set(CMAKE_EXE_LINKER clang-cl)

# you should define an XWIN_PATH
if (NOT DEFINED CMAKE_SYSROOT AND NOT DEFINED XWIN_PATH)
    message(FATAL_ERROR "Set XWIN_PATH to an installed xwin splat of version 18 or greater.")
endif()

set(XWIN_PATH "${XWIN_PATH}" CACHE STRING "" FORCE)

# set arm target
set(CMAKE_C_COMPILER_TARGET aarch64-pc-windows-msvc)
set(CMAKE_CXX_COMPILER_TARGET aarch64-pc-windows-msvc)
set(CMAKE_ASM_COMPILER_TARGET aarch64-pc-windows-msvc)

if (NOT DEFINED CMAKE_SYSROOT)
    set(CMAKE_SYSROOT "${XWIN_PATH}" CACHE STRING "" FORCE)
endif()

# idk why the linker uses different syntax
set(CMAKE_EXE_LINKER_FLAGS_INIT "/vctoolsdir:${XWIN_PATH}/crt /winsdkdir:${XWIN_PATH}/sdk")

# Update this as old compatibility code breaks
# 1952 => 19.52 => VS 2026
set(msc_ver 1952)

# winsysroot mode isn't supported
# yes the -I is necessary, otherwise clang-cl decides to pull in system C headers
set(flags "-vctoolsdir ${XWIN_PATH}/crt -winsdkdir ${XWIN_PATH}/sdk -I${XWIN_PATH}/sdk/include/ucrt -fmsc-version=${msc_ver}")

set(CMAKE_C_FLAGS_INIT "${flags}")
set(CMAKE_CXX_FLAGS_INIT "${flags}")
set(CMAKE_RC_FLAGS_INIT "${flags}")

# clang's built in assembler is not masm compatible
# luckily, boost.context provides armclang files compatible with clang-cl
# of course, since this is a Windows target, we naturally still masquerade as MARMASM
set(CMAKE_ASM_MARMASM_COMPILER clang-cl)
set(BOOST_CONTEXT_ASSEMBLER armclang CACHE STRING "" FORCE)
set(BOOST_CONTEXT_ASM_SUFFIX .S CACHE STRING "" FORCE)

# needed for boost context patch
# some hot nonsense is going on over in masm land
# ASM_MASM provides -Fo for some reason while clang-cl expects /Fo
set(CLANG_CL_CROSSCOMPILING TRUE)

# don't let find_library etc. use host libs/incdirs
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
