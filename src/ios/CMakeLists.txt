# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

enable_language(Swift OBJCXX)
add_executable(eden-ios
    AppUI-Bridging-Header.h
    AppUI.swift
    AppUIGameInformation.h
    AppUIGameInformation.mm
    AppUIObjC.h
    AppUIObjC.mm
    Config.h
    Config.mm
    EmulationSession.h
    EmulationSession.mm
    EmulationWindow.h
    EmulationWindow.mm
    VMA.cpp
)
# Keep bundle identifier as-is, for compatibility sake
set_target_properties(eden-ios PROPERTIES
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.yuzu-emu.yuzu"
    XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "YUZU1234"
    XCODE_ATTRIBUTE_SWIFT_OBJC_BRIDGING_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/AppUI-Bridging-Header.h"
    XCODE_ATTRIBUTE_SWIFT_OBJC_INTERFACE_HEADER_NAME "eden-ios-Swift.h"
    XCODE_ATTRIBUTE_DERIVED_FILE_DIR "${CMAKE_CURRENT_BINARY_DIR}"
)
target_link_libraries(eden-ios PRIVATE common core input_common frontend_common video_core sirit::sirit)
target_link_libraries(eden-ios PRIVATE ${PLATFORM_LIBRARIES} Threads::Threads)
target_link_libraries(eden-ios PRIVATE SDL2::SDL2 glad stb::headers)
create_target_directory_groups(eden-ios)
