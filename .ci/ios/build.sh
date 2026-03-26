#!/bin/sh -ex

# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

WORK_DIR="$PWD"
xcrun --sdk iphoneos --show-sdk-path

# TODO: support iphonesimulator sdk

cmake -G Xcode -B build/ios \
    -DCMAKE_TOOLCHAIN_FILE="$WORK_DIR/.ci/ios/ios-toolchain.cmake" \
    -DPLATFORM=OS64 \
    -DARCHS="arm64" \
    -DDEPLOYMENT_TARGET=16.0 \
    -DCMAKE_C_COMPILER="$(xcrun --sdk iphoneos --find clang)" \
    -DCMAKE_CXX_COMPILER="$(xcrun --sdk iphoneos --find clang++)" \
    -DENABLE_LIBUSB=OFF \
    -DENABLE_QT=OFF \
    -DENABLE_WEB_SERVICE=OFF \
    -DENABLE_CUBEB=OFF \
    -DCMAKE_BUILD_TYPE=Release \
    "$@"

cmake --build build/ios -t eden-ios --config Release
