#!/usr/bin/env -S cmake -P

# SPDX-FileCopyrightText: Copyright 2026 crueter
# SPDX-License-Identifier: LGPL-3.0-or-later

cmake_minimum_required(VERSION 3.31)

include(${CMAKE_CURRENT_LIST_DIR}/../ScriptUtils.cmake)

function(usage)
    echo([=[
Usage: cpmutil.sh package hash [-a|--all] [PACKAGE]...

Check the hash of a specific package or packages.
If a hash mismatch occurs, this script will update the package's hash.

Options:
    -a, --all       Operate on all packages in this project.

Note that this procedure will usually take a long time
depending on the number and size of dependencies.
]=])
endfunction()

set(NO_CI TRUE)
parse_script_args(args)

get_cpmfile_content(cpmfile)
foreach(key ${args})
    parse_key("${key}")
    echo("-- ${key}")

    if (ci)
        set(ARTIFACT_NAME ${artifact})
        set(ARTIFACT_VERSION ${version})
        set(ARTIFACT_EXT ${extension})
        set(ARTIFACT_GIT_HOST ${git_host})
        set(ARTIFACT_REPO ${repo})

        ci_collect_all_hashes(collected)

        string(JSON len LENGTH "${collected}")
        math(EXPR last_index "${len} - 1")

        foreach(i RANGE ${last_index})
            string(JSON pkgname MEMBER "${collected}" "${i}")
            string(JSON new_hash GET "${collected}" "${pkgname}")
            string(JSON old_hash GET "${hashes}" "${pkgname}")
            if(NOT new_hash STREQUAL old_hash)
                echo_error("Hash mismatch: ${key} [${pkgname}]")
                string(JSON hashes SET "${hashes}" "${pkgname}" "\"${new_hash}\"")
            endif()
        endforeach()

        string(JSON object SET "${object}" hashes "${hashes}")

        # update cached cpmfile content
        string(JSON cpmfile SET "${cpmfile}" "${key}" "${object}")
    else()
        get_package_url_object(pkg_url)
        get_package_hash("${pkg_url}" pkg_hash)

        if (pkg_hash STREQUAL hash)
            echo("Hashes match")
        else()
            echo_error("Hash mismatch")
            echo_error("Expected: ${hash}")
            echo_error("Got:      ${pkg_hash}")

            modify_package("${object}" "${version}" "${pkg_hash}" new_object)

            # update cached cpmfile content
            string(JSON cpmfile SET "${cpmfile}" "${key}" "${new_object}")

            echo("Corrected hash for ${key}")
        endif()
    endif()
endforeach()

# write cached cpmfile
get_cpmfile_path(file)
file(WRITE ${file} "${cpmfile}")
format_cpmfile()
