#!/bin/sh -ex

# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

NUM_JOBS=$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)

: "${CCACHE:=false}"
RETURN=0

usage() {
    cat <<EOF
Usage: $0 [-b|--build-type BUILD_TYPE] [-o|--outdir OUTPUT_DIRECTORY]

Build script for Emscripten (using wasm64).

Options:
    --build-type    Set the CMake build type (Release|RelWithDebInfo|MinSizeRel|Debug)
                    Default: Release
    --outdir        Set the output directory
                    Default: build

EOF
    exit "$RETURN"
}

die() {
	echo "-- ! $*" >&2
	RETURN=1 usage
}

type() {
    [ -z "$1" ] && die "You must specify a valid type."
    TYPE="$1"
}

outdir() {
    [ -z "$1" ] && die "You must specify a valid output directory."
    OUTDIR="$1"
}

while true; do
	case "$1" in
		-r|--release) DEVEL=false ;;
		-b|--build-type) type "$2"; shift ;;
		-o|--outdir) outdir "$2"; shift ;;
		-h|--help) usage ;;
		*) break ;;
	esac
	shift
done

: "${TYPE:=Release}"
: "${DEVEL:=true}"
: "${OUTDIR:=build}"

# -sMEMORY64 must be specified twice, see below
# The CMake toolchain file will match against MEMORY64 but will fail to match if:
# - it's either -sMEMORY64
# - or it's either -sMEMORY64=1
# The line in question:
# if (CMAKE_C_FLAGS MATCHES "MEMORY64")
# However why need to specify -sMEMORY64=1 then? Oh that's because if you didn't set
# the =1, it would assume you meant =0, which equates to not specifying it at all
# This seems to be fixed in later versions but occurs atleast on 4.0.3-git and below.
emcmake cmake -B "$OUTDIR" -G "Unix Makefiles" \
    -DCMAKE_BUILD_TYPE=${TYPE} \
    -DENABLE_OPENGL=OFF \
    -DENABLE_LTO=OFF \
    -DENABLE_QT=OFF \
    -DENABLE_UNITY_BUILD=OFF \
    -DENABLE_QT_TRANSLATION=OFF \
    -DENABLE_CUBEB=OFF \
    -DENABLE_LIBUSB=OFF \
    -DENABLE_UPDATE_CHECKER=OFF \
    -DENABLE_WEB_SERVICE=OFF \
    -DUSE_DISCORD_PRESENCE=OFF \
    -DENABLE_WIFI_SCAN=OFF \
    -DUSE_FASTER_LINKER=ON \
    -DYUZU_STATIC_BUILD=ON \
    -DYUZU_USE_BUNDLED_OPENSSL=OFF \
    -DYUZU_USE_EXTERNAL_FFMPEG=ON \
    -Dzstd_FORCE_BUNDLED=ON \
    -DOpenSSL_FORCE_BUNDLED=ON \
    -DEMSCRIPTEN_SYSTEM_PROCESSOR=wasm \
    -DCMAKE_C_FLAGS="-s MEMORY64 -m64 -pipe -sMEMORY64=1" \
    -DCMAKE_CXX_FLAGS="-s MEMORY64 -m64 -pipe -sMEMORY64=1" \
    -DCMAKE_EXE_LINKER_FLAGS="-sMEMORY64=1 -m64 -Wl,-mwasm64 -sASYNCIFY=1" \
    -DCMAKE_C_LINK_FLAGS="-sMEMORY64=1 -m64 -Wl,-mwasm64 -sASYNCIFY=1" \
    -DCMAKE_CXX_LINK_FLAGS="-sMEMORY64=1 -m64 -Wl,-mwasm64 -sASYNCIFY=1"

cmake --build "$OUTDIR" -- -j$NUM_JOBS
