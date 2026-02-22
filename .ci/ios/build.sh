#!/bin/sh -ex

# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

WORK_DIR="$PWD"
if [ -z "$NPROC" ]; then
    NPROC="$(nproc)"
fi

[ ! -z "$IOS_SDK" ]

cmake -G Xcode -B build \
    -DCMAKE_TOOLCHAIN_FILE="$WORK_DIR/.ci/ios/ios-toolchain.cmake" \
    -DPLATFORM=OS64 \
    -DCOCOA_LIBRARY="$IOS_SDK/System/Library/Frameworks/Cocoa.framework" \
    -DENABLE_LIBUSB=OFF \
    -DENABLE_UPDATE_CHECKER=OFF \
    -DENABLE_QT=OFF \
    -DENABLE_OPENSSL=OFF \
    -DENABLE_WEB_SERVICE=OFF \
    -DENABLE_CUBEB=OFF \
    -DYUZU_ROOM=OFF \
    -DYUZU_ROOM_STANDALONE=OFF \
    -DYUZU_CMD=OFF \
    -DUSE_DISCORD_PRESENCE=OFF \
    -DYUZU_USE_EXTERNAL_FFMPEG=ON \
    -DYUZU_USE_CPM=ON \
    -DYUZU_USE_EXTERNAL_SDL2=ON \
    -DCPMUTIL_FORCE_BUNDLED=ON \
    -DCMAKE_BUILD_TYPE=Release

cmake --build build -- -j${NPROC}
