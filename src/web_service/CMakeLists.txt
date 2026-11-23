# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

# SPDX-FileCopyrightText: 2018 yuzu Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

add_library(web_service STATIC
    announce_room_json.cpp
    announce_room_json.h
    web_backend.cpp
    web_backend.h
)

create_target_directory_groups(web_service)
target_link_libraries(web_service PRIVATE common hydro::carbon jacinth::jacinth)

find_package(OpenSSL REQUIRED)
target_link_libraries(web_service PRIVATE OpenSSL::SSL OpenSSL::Crypto)
