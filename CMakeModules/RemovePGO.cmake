# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

# remove -fprofile-use from flags
# use on targets that are not perf critical to prevent unsavory codegen

macro(RemovePGO target)
    get_target_property(opts ${target} COMPILE_OPTIONS)
    list(FILTER opts EXCLUDE REGEX "^-fprofile-use=")
    set_target_properties(${target} PROPERTIES COMPILE_OPTIONS "${opts}")
endmacro()
