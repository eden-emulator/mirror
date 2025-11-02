#!/bin/sh -e
# SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

die() {
	echo "$@" >&2
	exit 1
}
help() {
    cat << EOF
--arch <name>       Specify the target architecture (default: ppc64)
--os <name>         Specify the OS sysroot to use (default: freebsd)
--sysroot <path>    Specify sysroot to use
EOF
}

TARGET_ARCH="ppc64"
TARGET_OS="freebsd"
TARGET_CMAKE="$TARGET_ARCH-pc-$TARGET_OS"
BASE_DIR="$PWD"

while true; do
	case "$1" in
    --arch) shift; TARGET_ARCH=$1; [ -z "$TARGET_ARCH" ] && die "Expected argument";;
    --os) shift; TARGET_OS=$1; [ -z "$TARGET_OS" ] && die "Expected argument";;
    --sysroot) shift; SYSROOT=$1; [ -z "$SYSROOT" ] && die "Expected argument";;
	--help) help "$@";;
	--*) die "Invalid option $1" ;;
	"$0" | "") break;;
	esac
	shift
done

[ -z "$SYSROOT" ] && SYSROOT="$HOME/opt/$TARGET_ARCH-$TARGET_OS/sysroot"
mkdir -p "$SYSROOT" && cd "$SYSROOT"
case "$TARGET_OS" in
freebsd*)
    case "$TARGET_ARCH" in
        ppc64pe) URL="https://download.freebsd.org/ftp/releases/powerpc/powerpc64pe/14.3-RELEASE/base.txz" break;;
        ppc64le) URL="https://download.freebsd.org/ftp/releases/powerpc/powerpc64le/14.3-RELEASE/base.txz" break;;
        ppc64) URL="https://download.freebsd.org/ftp/releases/powerpc/powerpc64/14.3-RELEASE/base.txz" break;;
        ppc) URL="https://download.freebsd.org/ftp/releases/powerpc/powerpc/14.3-RELEASE/base.txz" break;;
        amd64 | x86_64) URL="https://download.freebsd.org/ftp/releases/amd64/14.3-RELEASE/base.txz" break;;
        arm64*) URL="https://download.freebsd.org/ftp/releases/arm64/$TARGET_ARCH/14.3-RELEASE/base.txz" break;;
        arm*) URL="https://download.freebsd.org/ftp/releases/arm/$TARGET_ARCH/14.3-RELEASE/base.txz" break;;
        *) die "Unknown arch $TARGET_ARCH" break;;
    esac
    [ -z "$TARGET_CMAKE" ] && TARGET_CMAKE="$TARGET_ARCH-pc-$TARGET_OS"
    [ -f "base.txz" ] || fetch "$URL" || die "Can't download"
    tar -xvzf base.txz
    break;;
*) die "Unknown OS $TARGET_OS" break;;
esac

[ -z "$CC" ] && CC=$(which clang)
[ -z "$CXX" ] && CXX=$(which clang++)

TOOLCHAIN_FILE="$BASE_DIR/$TARGET_CMAKE-toolchain.cmake"
cat << EOF >"$TOOLCHAIN_FILE"
# Script to generate .cmake toolchain files :)
# See https://man.freebsd.org/cgi/man.cgi?query=cmake-toolchains&sektion=7&manpath=FreeBSD+13.2-RELEASE+and+Ports

set(CMAKE_SYSROOT "$SYSROOT")
set(CMAKE_STAGING_PREFIX "$SYSROOT")

set(CMAKE_C_COMPILER $CC)
set(CMAKE_CXX_COMPILER $CXX)
set(CMAKE_C_FLAGS "--target=$TARGET_CMAKE")
set(CMAKE_CXX_FLAGS "--target=$TARGET_CMAKE")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
EOF

# cmake \
#     -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
#     -DCMAKE_BUILD_TYPE=Release \
#     -B "build-$TARGET_CMAKE" \
#     -DDYNARMIC_TESTS=ON \
#     -DENABLE_QT=OFF \
#     -DENABLE_SDL2=OFF \
#     -DYUZU_USE_CPM=ON \
#     -DYUZU_USE_EXTERNAL_FFMPEG=ON
#cmake --build "build-$TARGET_CMAKE" dynarmic_tests -- -j8
