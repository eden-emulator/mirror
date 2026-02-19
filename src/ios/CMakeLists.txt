# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

enable_language(Swift OBJC)

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

target_link_libraries(eden-ios PRIVATE common core input_common frontend_common video_core glad)
target_link_libraries(eden-ios PRIVATE ${PLATFORM_LIBRARIES} Threads::Threads)
target_link_libraries(eden-ios PRIVATE SDL2::SDL2)
create_target_directory_groups(eden-ios)
target_compile_options(eden-ios PRIVATE
    -Wno-conversion
    -Wno-unused-variable
    -Wno-unused-parameter
    -Wno-missing-field-initializers)
