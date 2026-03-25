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
)

# Keep bundle identifier as-is, for compatibility sake
set_target_properties(eden-ios PROPERTIES
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.yuzu-emu.yuzu"
    XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "YUZU1234"
    XCODE_ATTRIBUTE_SWIFT_OBJC_BRIDGING_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/AppUI-Bridging-Header.h"
)

target_link_libraries(eden-ios PRIVATE common core input_common frontend_common video_core glad)
target_link_libraries(eden-ios PRIVATE ${PLATFORM_LIBRARIES} Threads::Threads)
target_link_libraries(eden-ios PRIVATE SDL2::SDL2)
create_target_directory_groups(eden-ios)
target_compile_options(eden-ios PRIVATE
    $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-conversion>
    $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-unused-variable>
    $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-unused-parameter>
    $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-missing-field-initializers>
)
