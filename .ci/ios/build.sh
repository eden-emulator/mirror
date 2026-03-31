#!/bin/sh -ex

# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

WORK_DIR="$PWD"
xcrun --sdk iphoneos --show-sdk-path

# TODO: support iphonesimulator sdk

cmake -G Xcode -B build/ios \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=16.0 \
    -DCMAKE_OSX_SYSROOT=iphoneos \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_ARCHITECTURES="arm64" \
    -DCMAKE_BUILD_TYPE=Release \
    -DYUZU_USE_EXTERNAL_SDL2=ON \
    "$@"

cmake --build build/ios -t eden-ios --config Release
