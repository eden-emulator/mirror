# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

set(EFFECT_DIR ${CMAKE_ARGV3})
set(HEADER_FILE ${CMAKE_ARGV4})

file(GLOB EFFECT_FILES ${EFFECT_DIR}/*.fx)
list(SORT EFFECT_FILES)

set(ENTRIES "")
foreach(EFFECT_FILE IN LISTS EFFECT_FILES)
    get_filename_component(EFFECT_NAME ${EFFECT_FILE} NAME)
    file(READ ${EFFECT_FILE} EFFECT_BODY)

    string(REGEX REPLACE ";" "{{SEMICOLON}}" EFFECT_BODY "${EFFECT_BODY}")
    string(REGEX REPLACE "\n" ";" EFFECT_BODY "${EFFECT_BODY}")

    set(EFFECT_TEXT "")
    foreach(LINE IN LISTS EFFECT_BODY)
        string(CONCAT EFFECT_TEXT "${EFFECT_TEXT}" "        R\"(${LINE}\n)\"\n")
    endforeach()
    string(REGEX REPLACE "{{SEMICOLON}}" ";" EFFECT_TEXT "${EFFECT_TEXT}")

    string(CONCAT ENTRIES "${ENTRIES}"
        "    {\n        \"${EFFECT_NAME}\",\n${EFFECT_TEXT}    },\n")
endforeach()

get_filename_component(OUTPUT_DIR ${HEADER_FILE} DIRECTORY)
make_directory(${OUTPUT_DIR})

file(WRITE ${HEADER_FILE}
"// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string_view>

namespace VideoCore {

struct BundledFxEffect {
    std::string_view name;
    std::string_view source;
};

constexpr BundledFxEffect BUNDLED_FX_EFFECTS[]{
${ENTRIES}};

} // namespace VideoCore
")
