// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "common/common_types.h"
#include "effect_module.hpp"

namespace VideoCore {

struct FxCompileResult {
    reshadefx::effect_module module;
    std::map<std::string, std::vector<u32>> entry_points;
    std::string error;

    bool Succeeded() const {
        return error.empty() && !entry_points.empty();
    }
};

FxCompileResult CompileFxEffect(const std::filesystem::path& path, u32 width, u32 height,
                                u32 color_bit_depth);

} // namespace VideoCore
