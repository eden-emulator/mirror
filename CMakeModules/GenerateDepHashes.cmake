# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(pkg ${CPMUTIL_PACKAGES})
    list(APPEND NAMES "${pkg}")

    set(sys ${CPMUTIL_PACKAGE_${pkg}_SYSTEM})
    set(url ${CPMUTIL_PACKAGE_${pkg}_URL})
    set(version ${CPMUTIL_PACKAGE_${pkg}_VERSION})

    if(sys)
        set(version "${version} (system)")
    endif()

    list(APPEND VERSIONS "${version}")
    list(APPEND URLS "${url}")
endforeach()

list(LENGTH NAMES DEPS_LENGTH)

list(JOIN NAMES "\",\n\t\"" DEP_NAME_DIRTY)
set(DEP_NAMES "\t\"${DEP_NAME_DIRTY}\"")

list(JOIN SHAS "\",\n\t\"" DEP_SHAS_DIRTY)
set(DEP_SHAS "\t\"${DEP_SHAS_DIRTY}\"")

list(JOIN URLS "\",\n\t\"" DEP_URLS_DIRTY)
set(DEP_URLS "\t\"${DEP_URLS_DIRTY}\"")

configure_file(dep_hashes.h.in dep_hashes.h @ONLY)
target_sources(common PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/dep_hashes.h)
target_include_directories(common PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
