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

    PomeloApp.swift
    ContentView.swift
)

set(MACOSX_BUNDLE_GUI_IDENTIFIER "dev.eden-emu.eden")
set(MACOSX_BUNDLE_BUNDLE_NAME "Eden")
set(MACOSX_BUNDLE_INFO_STRING "Eden: A high-performance Nintendo Switch emulator")

# TODO(crueter): Copyright, and versioning

# Keep bundle identifier as-is, for compatibility sake
set_target_properties(eden-ios PROPERTIES
    XCODE_ATTRIBUTE_SWIFT_OBJC_BRIDGING_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/AppUI-Bridging-Header.h"
    XCODE_ATTRIBUTE_SWIFT_OBJC_INTERFACE_HEADER_NAME "eden-ios-Swift.h"
    XCODE_ATTRIBUTE_DERIVED_FILE_DIR "${CMAKE_CURRENT_BINARY_DIR}")

target_link_libraries(eden-ios PRIVATE common core input_common frontend_common video_core sirit::sirit)
target_link_libraries(eden-ios PRIVATE ${PLATFORM_LIBRARIES} Threads::Threads)
target_link_libraries(eden-ios PRIVATE SDL2::SDL2 glad stb::headers)
create_target_directory_groups(eden-ios)

# FIXME(crueter): This should /all/ be in a module of some kind!

# Xcode will automatically generate the Assets.car and icns file for us.
set(_dist "${CMAKE_SOURCE_DIR}/dist")
if (CMAKE_GENERATOR MATCHES "Xcode")
    set(_icons "${_dist}/eden.icon")

    set_target_properties(eden-ios PROPERTIES
        XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME eden
        MACOSX_BUNDLE_ICON_FILE eden
        # Also force xcode to manage signing for us.
        XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED ON
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED ON
        XCODE_ATTRIBUTE_CODE_SIGN_STYLE Automatic)
# Otherwise, we'll use our own.
else()
    set(_icons "${_dist}/eden.icns" "${_dist}/Assets.car")
endif()

set_source_files_properties(${_icons} PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources)
target_sources(eden-ios PRIVATE ${_icons})

set_target_properties(eden-ios PROPERTIES MACOSX_BUNDLE TRUE)

set(CMAKE_FIND_LIBRARY_SUFFIXES ".dylib")
find_library(MOLTENVK_LIBRARY MoltenVK REQUIRED)
message(STATUS "Using MoltenVK at ${MOLTENVK_LIBRARY}.")

set_source_files_properties(${MOLTENVK_LIBRARY} PROPERTIES
    MACOSX_PACKAGE_LOCATION Frameworks
    XCODE_FILE_ATTRIBUTES "CodeSignOnCopy")
target_sources(eden-ios PRIVATE ${MOLTENVK_LIBRARY})
