// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include "common/fs/fs.h"
#include "common/fs/fs_util.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "video_core/post_processing/fx_compile.h"
#include "video_core/post_processing/fx_effect.h"

namespace VideoCore {

namespace {

constexpr u32 CATALOG_PROBE_WIDTH = 1280;
constexpr u32 CATALOG_PROBE_HEIGHT = 720;
constexpr u32 CATALOG_PROBE_DEPTH = 8;

std::vector<FxEffectDesc> catalog;

const reshadefx::annotation* FindAnnotation(const std::vector<reshadefx::annotation>& annotations,
                                            std::string_view name) {
    const auto it = std::find_if(annotations.begin(), annotations.end(),
                                 [&](const reshadefx::annotation& a) { return a.name == name; });
    if (it == annotations.end()) {
        return nullptr;
    }
    return &*it;
}

std::string AnnotationString(const std::vector<reshadefx::annotation>& annotations,
                             std::string_view name) {
    const reshadefx::annotation* a = FindAnnotation(annotations, name);
    if (a == nullptr) {
        return std::string();
    }
    return a->value.string_data;
}

bool AnnotationFloat(const std::vector<reshadefx::annotation>& annotations, std::string_view name,
                     f32& out) {
    const reshadefx::annotation* a = FindAnnotation(annotations, name);
    if (a == nullptr) {
        return false;
    }
    if (a->type.is_floating_point()) {
        out = a->value.as_float[0];
        return true;
    }
    if (a->type.is_integral()) {
        out = static_cast<f32>(a->value.as_int[0]);
        return true;
    }
    return false;
}

FxUiType ParseUiType(std::string_view value) {
    if (value == "slider") {
        return FxUiType::Slider;
    }
    if (value == "drag") {
        return FxUiType::Drag;
    }
    if (value == "combo") {
        return FxUiType::Combo;
    }
    if (value == "radio") {
        return FxUiType::Radio;
    }
    if (value == "check" || value == "checkbox") {
        return FxUiType::CheckBox;
    }
    if (value == "color") {
        return FxUiType::Color;
    }
    if (value == "input") {
        return FxUiType::InputBox;
    }
    return FxUiType::Hidden;
}

std::vector<std::string> SplitItems(const std::string& items) {
    std::vector<std::string> out;
    std::string current;
    for (const char c : items) {
        if (c == '\0') {
            out.push_back(current);
            current.clear();
            continue;
        }
        current += c;
    }
    if (!current.empty()) {
        out.push_back(current);
    }
    return out;
}

FxUniformDesc DescribeUniform(const reshadefx::uniform& info) {
    FxUniformDesc desc;
    desc.name = info.name;
    desc.components = std::min<u32>(info.type.components(), 4);

    if (info.type.is_boolean()) {
        desc.kind = FxUniformKind::Bool;
    } else if (info.type.is_integral()) {
        desc.kind = FxUniformKind::Int;
    } else {
        desc.kind = FxUniformKind::Float;
    }

    desc.label = AnnotationString(info.annotations, "ui_label");
    if (desc.label.empty()) {
        desc.label = info.name;
    }
    desc.tooltip = AnnotationString(info.annotations, "ui_tooltip");
    desc.category = AnnotationString(info.annotations, "ui_category");
    desc.ui_type = ParseUiType(AnnotationString(info.annotations, "ui_type"));
    desc.items = SplitItems(AnnotationString(info.annotations, "ui_items"));

    if (desc.kind == FxUniformKind::Bool) {
        desc.ui_min = 0.0f;
        desc.ui_max = 1.0f;
        desc.ui_step = 1.0f;
    } else if (desc.kind == FxUniformKind::Int) {
        desc.ui_min = 0.0f;
        desc.ui_max = 100.0f;
        desc.ui_step = 1.0f;
    }

    void(AnnotationFloat(info.annotations, "ui_min", desc.ui_min));
    void(AnnotationFloat(info.annotations, "ui_max", desc.ui_max));
    void(AnnotationFloat(info.annotations, "ui_step", desc.ui_step));

    if (desc.ui_step <= 0.0f) {
        desc.ui_step = 0.01f;
        if (desc.kind != FxUniformKind::Float) {
            desc.ui_step = 1.0f;
        }
    }
    if (desc.ui_max < desc.ui_min) {
        std::swap(desc.ui_min, desc.ui_max);
    }

    if (info.has_initializer_value) {
        for (u32 i = 0; i < desc.components; ++i) {
            if (desc.kind == FxUniformKind::Float) {
                desc.default_value[i] = info.initializer_value.as_float[i];
            } else {
                desc.default_value[i] = static_cast<f32>(info.initializer_value.as_int[i]);
            }
        }
    }

    return desc;
}

FxEffectDesc DescribeEffect(const std::filesystem::path& path, const std::filesystem::path& root) {
    FxEffectDesc desc;
    desc.file = Common::FS::PathToUTF8String(std::filesystem::relative(path, root));
    desc.name = Common::FS::PathToUTF8String(path.stem());

    const auto compiled =
        CompileFxEffect(path, CATALOG_PROBE_WIDTH, CATALOG_PROBE_HEIGHT, CATALOG_PROBE_DEPTH);
    if (!compiled.Succeeded()) {
        desc.error = compiled.error;
        return desc;
    }

    for (const auto& technique : compiled.module.techniques) {
        desc.techniques.push_back(technique.name);
    }

    for (const auto& uniform : compiled.module.uniforms) {
        FxUniformDesc uniform_desc = DescribeUniform(uniform);
        if (uniform_desc.ui_type == FxUiType::Hidden) {
            continue;
        }
        desc.uniforms.push_back(std::move(uniform_desc));
    }

    return desc;
}

} // Anonymous namespace

std::filesystem::path GetFxRootDirectory() {
    return Common::FS::GetEdenPath(Common::FS::EdenPath::PostShaderDir);
}

std::vector<std::filesystem::path> GetFxIncludePaths(const std::filesystem::path& effect_path) {
    const auto root = GetFxRootDirectory();
    std::vector<std::filesystem::path> paths;
    paths.push_back(effect_path.parent_path());
    paths.push_back(root);
    paths.push_back(root / "Shaders");

    const auto last = std::unique(paths.begin(), paths.end());
    paths.erase(last, paths.end());
    return paths;
}

std::filesystem::path ResolveFxTexturePath(const std::filesystem::path& effect_path,
                                           std::string_view source) {
    const auto root = GetFxRootDirectory();
    const std::filesystem::path name{source};

    const std::array candidates{
        effect_path.parent_path() / name,
        root / "Textures" / name,
        root / name,
    };

    for (const auto& candidate : candidates) {
        if (Common::FS::Exists(candidate)) {
            return candidate;
        }
    }

    return std::filesystem::path();
}

void ReloadFxCatalog() {
    catalog.clear();

    const auto root = GetFxRootDirectory();
    if (!Common::FS::Exists(root)) {
        void(Common::FS::CreateDirs(root));
        return;
    }

    std::vector<std::filesystem::path> effect_files;
    Common::FS::IterateDirEntriesRecursively(
        root,
        [&](const std::filesystem::directory_entry& entry) {
            if (entry.path().extension() == ".fx") {
                effect_files.push_back(entry.path());
            }
            return true;
        },
        Common::FS::DirEntryFilter::File);

    std::sort(effect_files.begin(), effect_files.end());

    for (const auto& file : effect_files) {
        FxEffectDesc desc = DescribeEffect(file, root);
        if (!desc.error.empty()) {
            LOG_WARNING(Render, "Post-processing effect '{}' failed to compile:\n{}", desc.file,
                        desc.error);
        }
        catalog.push_back(std::move(desc));
    }

    const size_t usable = std::count_if(catalog.begin(), catalog.end(),
                                        [](const FxEffectDesc& d) { return d.Valid(); });
    LOG_INFO(Render, "Loaded {} post-processing effects ({} usable)", catalog.size(), usable);
}

const std::vector<FxEffectDesc>& GetFxCatalog() {
    return catalog;
}

const FxEffectDesc* FindFxEffect(std::string_view file) {
    const auto it = std::find_if(catalog.begin(), catalog.end(),
                                 [&](const FxEffectDesc& d) { return d.file == file; });
    if (it == catalog.end()) {
        return nullptr;
    }
    return &*it;
}

const FxUniformDesc* FindFxUniform(const FxEffectDesc& effect, std::string_view name) {
    const auto it = std::find_if(effect.uniforms.begin(), effect.uniforms.end(),
                                 [&](const FxUniformDesc& u) { return u.name == name; });
    if (it == effect.uniforms.end()) {
        return nullptr;
    }
    return &*it;
}

} // namespace VideoCore
