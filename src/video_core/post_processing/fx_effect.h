// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "common/common_types.h"

namespace VideoCore {

enum class FxUniformKind {
    Bool,
    Int,
    Float,
};

enum class FxUiType {
    Hidden,
    Slider,
    Drag,
    Combo,
    Radio,
    CheckBox,
    Color,
    InputBox,
};

struct FxUniformDesc {
    std::string name;
    std::string label;
    std::string tooltip;
    std::string category;
    FxUniformKind kind{FxUniformKind::Float};
    u32 components{1};
    FxUiType ui_type{FxUiType::Hidden};
    f32 ui_min{0.0f};
    f32 ui_max{1.0f};
    f32 ui_step{0.01f};
    std::vector<std::string> items;
    std::array<f32, 4> default_value{};
};

struct FxEffectDesc {
    std::string file;
    std::string name;
    std::vector<std::string> techniques;
    std::vector<FxUniformDesc> uniforms;
    std::string error;

    bool Valid() const {
        return error.empty() && !techniques.empty();
    }
};

std::filesystem::path GetFxRootDirectory();

std::vector<std::filesystem::path> GetFxIncludePaths(const std::filesystem::path& effect_path);

std::filesystem::path ResolveFxTexturePath(const std::filesystem::path& effect_path,
                                           std::string_view source);

void ReloadFxCatalog();

const std::vector<FxEffectDesc>& GetFxCatalog();

const FxEffectDesc* FindFxEffect(std::string_view file);

const FxUniformDesc* FindFxUniform(const FxEffectDesc& effect, std::string_view name);

} // namespace VideoCore
