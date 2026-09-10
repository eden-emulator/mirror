// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include "bundled_fx_presets.h"
#include "common/fs/file.h"
#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "common/settings.h"
#include "video_core/post_processing/fx_chain.h"
#include "video_core/post_processing/fx_preset.h"

namespace VideoCore {

namespace {

std::vector<FxPresetDesc> preset_catalog;
bool preset_catalog_scanned = false;

std::string Trim(std::string_view value) {
    size_t begin = 0;
    while (begin < value.size() && (value[begin] == ' ' || value[begin] == '\t')) {
        ++begin;
    }
    size_t end = value.size();
    while (end > begin) {
        const char c = value[end - 1];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            break;
        }
        --end;
    }
    return std::string(value.substr(begin, end - begin));
}

FxPresetDesc ParsePreset(std::string_view text) {
    FxPresetDesc desc;

    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        const std::string_view line = text.substr(start, end - start);
        start = end + 1;

        const size_t equals = line.find('=');
        if (equals == std::string_view::npos) {
            continue;
        }

        const std::string key = Trim(line.substr(0, equals));
        const std::string value = Trim(line.substr(equals + 1));

        if (key == "name") {
            desc.name = value;
        } else if (key == "description") {
            desc.description = value;
        } else if (key == "chain") {
            desc.chain = value;
        }
    }

    return desc;
}

std::string PresetFileName(std::string_view name) {
    std::string out;
    for (const char c : name) {
        if (c == ' ') {
            out += '_';
            continue;
        }
        const bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                          (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (keep) {
            out += c;
        }
    }
    if (out.empty()) {
        out = "preset";
    }
    return out + ".fxp";
}

std::string ComposePreset(const FxPresetDesc& desc) {
    std::string out;
    out += "name=";
    out += desc.name;
    out += '\n';
    out += "description=";
    out += desc.description;
    out += '\n';
    out += "chain=";
    out += desc.chain;
    out += '\n';
    return out;
}

} // Anonymous namespace

std::filesystem::path GetFxPresetDirectory() {
    return Common::FS::GetEdenPath(Common::FS::EdenPath::PostPresetDir);
}

void ReloadFxPresetCatalog() {
    preset_catalog.clear();
    preset_catalog_scanned = true;

    const auto root = GetFxPresetDirectory();
    if (!Common::FS::Exists(root) && !Common::FS::CreateDirs(root)) {
        return;
    }

    std::vector<std::string> bundled_names;
    for (const auto& bundled : BUNDLED_FX_PRESETS) {
        const auto path = root / bundled.name;
        bundled_names.emplace_back(bundled.name);
        if (Common::FS::Exists(path)) {
            continue;
        }
        void(Common::FS::WriteStringToFile(path, Common::FS::FileType::TextFile, bundled.source));
    }

    std::vector<std::filesystem::path> preset_files;
    Common::FS::IterateDirEntriesRecursively(
        root,
        [&](const std::filesystem::directory_entry& entry) {
            if (entry.path().extension() == ".fxp") {
                preset_files.push_back(entry.path());
            }
            return true;
        },
        Common::FS::DirEntryFilter::File);

    std::sort(preset_files.begin(), preset_files.end());

    for (const auto& file : preset_files) {
        const std::string text =
            Common::FS::ReadStringFromFile(file, Common::FS::FileType::TextFile);

        FxPresetDesc desc = ParsePreset(text);
        if (desc.name.empty() || desc.chain.empty()) {
            continue;
        }

        desc.file = Common::FS::PathToUTF8String(file.filename());
        desc.bundled = std::find(bundled_names.begin(), bundled_names.end(), desc.file) !=
                       bundled_names.end();
        preset_catalog.push_back(std::move(desc));
    }
}

const std::vector<FxPresetDesc>& GetFxPresetCatalog() {
    if (!preset_catalog_scanned) {
        ReloadFxPresetCatalog();
    }
    return preset_catalog;
}

const FxPresetDesc* FindFxPreset(std::string_view name) {
    const auto& presets = GetFxPresetCatalog();
    const auto it = std::find_if(presets.begin(), presets.end(),
                                 [&](const FxPresetDesc& p) { return p.name == name; });
    if (it == presets.end()) {
        return nullptr;
    }
    return &*it;
}

bool ApplyFxPreset(std::string_view name) {
    const FxPresetDesc* preset = FindFxPreset(name);
    if (preset == nullptr) {
        return false;
    }

    FxChain::Instance().SetEntries(ParseFxChain(preset->chain));
    FxChain::Instance().DropUnknownEntries();
    FxChain::Instance().StoreToSettings();
    SetActiveFxPreset(preset->name);
    return true;
}

bool SaveFxPreset(std::string_view name, std::string_view description) {
    if (!IsFxPresetName(name)) {
        return false;
    }

    const auto root = GetFxPresetDirectory();
    if (!Common::FS::Exists(root) && !Common::FS::CreateDirs(root)) {
        return false;
    }

    FxPresetDesc desc;
    desc.name = std::string(name);
    desc.description = std::string(description);
    desc.chain = SerializeFxChain(FxChain::Instance().Entries());

    const std::string body = ComposePreset(desc);
    const std::string file = PresetFileName(name);
    if (Common::FS::WriteStringToFile(root / file, Common::FS::FileType::TextFile, body) !=
        body.size()) {
        return false;
    }

    ReloadFxPresetCatalog();
    SetActiveFxPreset(name);
    return true;
}

bool DeleteFxPreset(std::string_view name) {
    const FxPresetDesc* preset = FindFxPreset(name);
    if (preset == nullptr || preset->bundled) {
        return false;
    }

    const auto path = GetFxPresetDirectory() / preset->file;
    if (!Common::FS::RemoveFile(path)) {
        return false;
    }

    if (GetActiveFxPreset() == name) {
        SetActiveFxPreset(std::string_view());
    }
    ReloadFxPresetCatalog();
    return true;
}

bool IsFxPresetName(std::string_view name) {
    if (name.empty()) {
        return false;
    }
    return name.find_first_of("\r\n=") == std::string_view::npos;
}

std::string GetActiveFxPreset() {
    return Settings::values.post_shader_preset.GetValue();
}

void SetActiveFxPreset(std::string_view name) {
    Settings::values.post_shader_preset.SetValue(std::string(name));
}

bool IsActiveFxPresetModified() {
    const std::string active = GetActiveFxPreset();
    if (active.empty()) {
        return false;
    }

    const FxPresetDesc* preset = FindFxPreset(active);
    if (preset == nullptr) {
        return true;
    }

    return SerializeFxChain(FxChain::Instance().Entries()) !=
           SerializeFxChain(ParseFxChain(preset->chain));
}

} // namespace VideoCore
