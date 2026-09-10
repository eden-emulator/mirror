// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace VideoCore {

struct FxPresetDesc {
    std::string file;
    std::string name;
    std::string description;
    std::string chain;
    bool bundled{};
};

std::filesystem::path GetFxPresetDirectory();

void ReloadFxPresetCatalog();

const std::vector<FxPresetDesc>& GetFxPresetCatalog();

const FxPresetDesc* FindFxPreset(std::string_view name);

bool ApplyFxPreset(std::string_view name);

bool SaveFxPreset(std::string_view name, std::string_view description);

bool DeleteFxPreset(std::string_view name);

bool IsFxPresetName(std::string_view name);

std::string GetActiveFxPreset();

void SetActiveFxPreset(std::string_view name);

bool IsActiveFxPresetModified();

} // namespace VideoCore
