#!/usr/local/bin/bash -ex

# SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

[ -f "ps4-toolchain.cmake" ] || cat << EOF >"ps4-toolchain.cmake"
set(CMAKE_SYSROOT "$OO_PS4_TOOLCHAIN")
set(CMAKE_STAGING_PREFIX "$OO_PS4_TOOLCHAIN")
set(CMAKE_SYSTEM_NAME "OpenOrbis")

set(CMAKE_C_FLAGS " -D__OPENORBIS__ -D_LIBCPP_HAS_MUSL_LIBC=1 -D_GNU_SOURCE=1 --target=x86_64-pc-freebsd12-elf -mtune=x86-64 -march=x86-64 -fPIC -funwind-tables")
set(CMAKE_CXX_FLAGS " -D__OPENORBIS__ -D_LIBCPP_HAS_MUSL_LIBC=1 -D_GNU_SOURCE=1 --target=x86_64-pc-freebsd12-elf -mtune=x86-64 -march=x86-64 -fPIC -funwind-tables")

set(CMAKE_EXE_LINKER_FLAGS "-m elf_x86_64 -pie -T $OO_PS4_TOOLCHAIN/link.x --eh-frame-hdr -L$OO_PS4_TOOLCHAIN/lib")
set(CMAKE_C_LINK_FLAGS "-m elf_x86_64 -pie -T $OO_PS4_TOOLCHAIN/link.x --eh-frame-hdr -L$OO_PS4_TOOLCHAIN/lib")
set(CMAKE_CXX_LINK_FLAGS "-m elf_x86_64 -pie -T $OO_PS4_TOOLCHAIN/link.x --eh-frame-hdr -L$OO_PS4_TOOLCHAIN/lib")

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_LINKER ld.lld)
set(CMAKE_C_LINK_EXECUTABLE "<CMAKE_LINKER> <CMAKE_C_LINK_FLAGS> <OBJECTS> -o <TARGET> -lc -lkernel -lSceUserService  -lSceSysmodule -lSceNet $OO_PS4_TOOLCHAIN/lib/crt1.o <LINK_LIBRARIES>")
set(CMAKE_CXX_LINK_EXECUTABLE "<CMAKE_LINKER> <CMAKE_CXX_LINK_FLAGS> <OBJECTS> -o <TARGET> -lc -lkernel -lc++ -lSceUserService -lSceSysmodule -lSceNet $OO_PS4_TOOLCHAIN/lib/crt1.o <LINK_LIBRARIES>")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# TODO: Why does cmake not set this?
set(CMAKE_SIZEOF_VOID_P 8)
EOF

# Normally a platform has a package manager
# PS4 does not, atleast not in the normal sense
export EXTRA_CMAKE_FLAGS=("${EXTRA_CMAKE_FLAGS[@]}" $@)
cmake -S . -B build -G "Unix Makefiles" \
    -DCMAKE_TOOLCHAIN_FILE="ps4-toolchain.cmake" \
    -DENABLE_QT_TRANSLATION=OFF \
    -DENABLE_CUBEB=OFF \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS="$ARCH_FLAGS" \
    -DCMAKE_C_FLAGS="$ARCH_FLAGS" \
    -DENABLE_SDL2=ON \
    -DENABLE_LIBUSB=OFF \
    -DENABLE_UPDATE_CHECKER=OFF \
    -DENABLE_QT=OFF \
    -DENABLE_OPENSSL=OFF \
    -DENABLE_WEB_SERVICE=OFF \
    -DUSE_DISCORD_PRESENCE=OFF \
    -DCPMUTIL_FORCE_BUNDLED=ON \
    -DYUZU_USE_EXTERNAL_FFMPEG=ON \
    -DYUZU_USE_CPM=ON \
    "${EXTRA_CMAKE_FLAGS[@]}" || exit
cmake --build build -t yuzu-cmd_pkg -- -j8
