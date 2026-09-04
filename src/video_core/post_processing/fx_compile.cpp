// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstring>
#include <memory>
#include <set>

#include "effect_codegen.hpp"
#include "effect_parser.hpp"
#include "effect_preprocessor.hpp"

#include "common/fs/fs.h"
#include "common/fs/fs_util.h"
#include "video_core/post_processing/fx_compile.h"
#include "video_core/post_processing/fx_effect.h"

namespace VideoCore {

FxCompileResult CompileFxEffect(const std::filesystem::path& path, u32 width, u32 height,
                                u32 color_bit_depth) {
    FxCompileResult result;

    if (!Common::FS::Exists(path)) {
        result.error = "Effect file not found: " + Common::FS::PathToUTF8String(path);
        return result;
    }

    reshadefx::preprocessor preprocessor;
    preprocessor.add_macro_definition("__RESHADE__", "50000");
    preprocessor.add_macro_definition("__RESHADE_PERFORMANCE_MODE__", "1");
    preprocessor.add_macro_definition("__RENDERER__", "0x20000");
    preprocessor.add_macro_definition("__VENDOR__", "0");
    preprocessor.add_macro_definition("__DEVICE__", "0");
    preprocessor.add_macro_definition("__APPLICATION__", "0");
    preprocessor.add_macro_definition("BUFFER_WIDTH", std::to_string(width));
    preprocessor.add_macro_definition("BUFFER_HEIGHT", std::to_string(height));
    preprocessor.add_macro_definition("BUFFER_RCP_WIDTH", "(1.0 / BUFFER_WIDTH)");
    preprocessor.add_macro_definition("BUFFER_RCP_HEIGHT", "(1.0 / BUFFER_HEIGHT)");
    preprocessor.add_macro_definition("BUFFER_COLOR_DEPTH", std::to_string(color_bit_depth));
    preprocessor.add_macro_definition("BUFFER_COLOR_BIT_DEPTH", std::to_string(color_bit_depth));

    for (const auto& include : GetFxIncludePaths(path)) {
        preprocessor.add_include_path(include);
    }

    if (!preprocessor.append_file(path)) {
        result.error = preprocessor.errors();
        if (result.error.empty()) {
            result.error = "Failed to preprocess " + Common::FS::PathToUTF8String(path);
        }
        return result;
    }

    std::unique_ptr<reshadefx::codegen> backend(
        reshadefx::create_codegen_spirv(true, false, false, false, false));

    reshadefx::parser parser;
    if (!parser.parse(preprocessor.output(), backend.get())) {
        result.error = parser.errors();
        if (result.error.empty()) {
            result.error = "Failed to parse " + Common::FS::PathToUTF8String(path);
        }
        return result;
    }

    result.module = backend->module();

    std::set<std::string> wanted;
    for (const auto& technique : result.module.techniques) {
        for (const auto& pass : technique.passes) {
            if (!pass.vs_entry_point.empty()) {
                wanted.insert(pass.vs_entry_point);
            }
            if (!pass.ps_entry_point.empty()) {
                wanted.insert(pass.ps_entry_point);
            }
        }
    }

    for (const auto& name : wanted) {
        std::string binary;
        std::string assembly;
        std::string errors;
        if (!backend->assemble_code_for_entry_point(name, binary, assembly, errors)) {
            result.error = "Failed to assemble entry point '" + name + "': " + errors;
            return result;
        }
        if (binary.size() % sizeof(u32) != 0) {
            result.error = "Entry point '" + name + "' produced a malformed SPIR-V module";
            return result;
        }

        std::vector<u32> words(binary.size() / sizeof(u32));
        std::memcpy(words.data(), binary.data(), binary.size());
        result.entry_points.emplace(name, std::move(words));
    }

    if (result.entry_points.empty()) {
        result.error = "Effect declares no usable entry points";
    }

    return result;
}

} // namespace VideoCore
