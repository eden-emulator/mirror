# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

set(PRESET_DIR ${CMAKE_ARGV3})
set(HEADER_FILE ${CMAKE_ARGV4})

file(GLOB PRESET_FILES ${PRESET_DIR}/*.fxp)
list(SORT PRESET_FILES)

set(ENTRIES "")
foreach(PRESET_FILE IN LISTS PRESET_FILES)
    get_filename_component(PRESET_NAME ${PRESET_FILE} NAME)
    file(READ ${PRESET_FILE} PRESET_BODY)

    string(REGEX REPLACE ";" "{{SEMICOLON}}" PRESET_BODY "${PRESET_BODY}")
    string(REGEX REPLACE "\n" ";" PRESET_BODY "${PRESET_BODY}")

    set(PRESET_TEXT "")
    foreach(LINE IN LISTS PRESET_BODY)
        string(CONCAT PRESET_TEXT "${PRESET_TEXT}" "        R\"(${LINE}\n)\"\n")
    endforeach()
    string(REGEX REPLACE "{{SEMICOLON}}" ";" PRESET_TEXT "${PRESET_TEXT}")

    string(CONCAT ENTRIES "${ENTRIES}"
        "    {\n        \"${PRESET_NAME}\",\n${PRESET_TEXT}    },\n")
endforeach()

get_filename_component(OUTPUT_DIR ${HEADER_FILE} DIRECTORY)
make_directory(${OUTPUT_DIR})

file(WRITE ${HEADER_FILE}
"// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string_view>

namespace VideoCore {

struct BundledFxPreset {
    std::string_view name;
    std::string_view source;
};

constexpr BundledFxPreset BUNDLED_FX_PRESETS[]{
${ENTRIES}};

} // namespace VideoCore
")
