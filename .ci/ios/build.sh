#!/bin/sh -ex

# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

WORK_DIR="$PWD"
export IOS_SDK="$(xcrun --sdk iphoneos --show-sdk-path)"

[ ! -z "$IOS_SDK" ]

cmake -G Xcode -B build \
    -DCMAKE_TOOLCHAIN_FILE="$WORK_DIR/.ci/ios/ios-toolchain.cmake" \
    -DPLATFORM=OS64 \
    -DARCHS="arm64" \
    -DDEPLOYMENT_TARGET=16.0 \
    -DCOCOA_LIBRARY="$IOS_SDK/System/Library/Frameworks/Cocoa.framework" \
    -DCMAKE_C_COMPILER="$(xcrun --sdk iphoneos clang -arch arm64)" \
    -DCMAKE_CXX_COMPILER="$(xcrun --sdk iphoneos clang++ -arch arm64)" \
    -DENABLE_LIBUSB=OFF \
    -DENABLE_UPDATE_CHECKER=OFF \
    -DENABLE_QT=OFF \
    -DENABLE_OPENSSL=OFF \
    -DHTTPLIB_USE_OPENSSL=OFF \
    -DCPPHTTPLIB_USE_OPENSSL=OFF \
    -DHTTPLIB_USE_OPENSSL_IF_AVAILABLE=OFF \
    -DENABLE_WEB_SERVICE=OFF \
    -DENABLE_CUBEB=OFF \
    -DYUZU_ROOM=OFF \
    -DYUZU_ROOM_STANDALONE=OFF \
    -DYUZU_STATIC_ROOM=OFF \
    -DYUZU_CMD=OFF \
    -DUSE_DISCORD_PRESENCE=OFF \
    -DYUZU_USE_EXTERNAL_FFMPEG=ON \
    -DYUZU_USE_EXTERNAL_SDL2=ON \
    -DCPMUTIL_FORCE_BUNDLED=ON \
    -DYUZU_USE_BUNDLED_SIRIT=OFF \
    -DCMAKE_BUILD_TYPE=Release

cmake --build build -t eden-ios
