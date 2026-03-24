# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

# SPDX-FileCopyrightText: 2018 yuzu Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

add_library(web_service STATIC
    announce_room_json.cpp
    announce_room_json.h
    verify_login.cpp
    verify_login.h
    verify_user_jwt.cpp
    verify_user_jwt.h
    web_backend.cpp
    web_backend.h
    web_result.h
)

create_target_directory_groups(web_service)
target_include_directories(web_service PUBLIC ${cpp-jwt_SOURCE_DIR}/include)
target_link_libraries(web_service PRIVATE common network nlohmann_json::nlohmann_json httplib::httplib cpp-jwt::cpp-jwt)

find_package(OpenSSL REQUIRED)
target_link_libraries(web_service PRIVATE OpenSSL::SSL OpenSSL::Crypto)
if (IOS)
    # TODO: this is horrific
    find_package(OpenSSL 3 QUIET)
    if (OpenSSL_FOUND)
        target_link_libraries(web_service PRIVATE OpenSSL::SSL OpenSSL::Crypto)
        if (NOT IOS)
            target_compile_definitions(web_service PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
        endif()
    else()
        find_package(OpenSSL 3 QUIET PATHS /usr/local/opt/openssl /opt/homebrew/opt/openssl NO_DEFAULT_PATH)
        if (OpenSSL_FOUND)
            target_link_libraries(web_service PRIVATE OpenSSL::SSL OpenSSL::Crypto)
            if (NOT IOS)
                target_compile_definitions(web_service PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
            endif()
        else()
            message(WARNING "OpenSSL not found or version is less than 3. Some features in web_service may be disabled.")
        endif()
    endif()
else()
    find_package(OpenSSL REQUIRED)
    target_link_libraries(web_service PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    target_compile_definitions(web_service PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
endif()
