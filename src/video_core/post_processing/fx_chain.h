// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <map>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "common/common_types.h"

namespace VideoCore {

struct FxChainEntry {
    std::string file;
    std::string technique;
    std::map<std::string, std::array<f32, 4>> values;
};

struct FxChainSnapshot {
    std::vector<FxChainEntry> entries;
    u64 generation{};
};

std::vector<FxChainEntry> ParseFxChain(std::string_view value);

std::string SerializeFxChain(std::span<const FxChainEntry> entries);

class FxChain {
public:
    static FxChain& Instance();

    FxChainSnapshot Snapshot() const;

    std::vector<FxChainEntry> Entries() const;

    size_t Size() const;

    void Append(std::string_view file, std::string_view technique);

    void Replace(size_t index, std::string_view file, std::string_view technique);

    void Remove(size_t index);

    void Move(size_t index, int delta);

    void Clear();

    void SetValue(size_t index, std::string_view uniform, const std::array<f32, 4>& value);

    std::array<f32, 4> GetValue(size_t index, std::string_view uniform) const;

    std::map<std::string, std::array<f32, 4>> EntryValues(size_t index) const;

    bool HasValue(size_t index, std::string_view uniform) const;

    void ResetValues(size_t index);

    void LoadFromSettings();

    void StoreToSettings() const;

    void DropUnknownEntries();

private:
    FxChain() = default;

    mutable std::mutex mutex;
    std::vector<FxChainEntry> entries;
    std::atomic<u64> generation{1};
};

} // namespace VideoCore
