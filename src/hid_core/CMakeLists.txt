# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

# SPDX-FileCopyrightText: 2018 yuzu Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

add_library(hid_core STATIC
    frontend/emulated_console.cpp
    frontend/emulated_console.h
    frontend/emulated_controller.cpp
    frontend/emulated_controller.h
    frontend/emulated_devices.cpp
    frontend/emulated_devices.h
    frontend/input_converter.cpp
    frontend/input_converter.h
    frontend/motion_input.cpp
    frontend/motion_input.h
    hid_core.cpp
    hid_core.h
    hid_result.h
    hid_types.h
    hid_util.h
)


if (MSVC)
    target_compile_options(hid_core PRIVATE
        /we4242 # 'identifier': conversion from 'type1' to 'type2', possible loss of data
        /we4244 # 'conversion': conversion from 'type1' to 'type2', possible loss of data
        /we4245 # 'conversion': conversion from 'type1' to 'type2', signed/unsigned mismatch
        /we4254 # 'operator': conversion from 'type1:field_bits' to 'type2:field_bits', possible loss of data
        /we4800 # Implicit conversion from 'type' to bool. Possible information loss
    )
else()
    target_compile_options(hid_core PRIVATE
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wconversion>
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-sign-conversion>
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-cast-function-type>
        $<$<CXX_COMPILER_ID:Clang>:-fsized-deallocation>)
endif()

create_target_directory_groups(hid_core)
target_link_libraries(hid_core PUBLIC common)
