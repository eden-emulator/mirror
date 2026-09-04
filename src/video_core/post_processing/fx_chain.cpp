// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstdlib>

#include <fmt/format.h>

#include "common/settings.h"
#include "video_core/post_processing/fx_chain.h"
#include "video_core/post_processing/fx_effect.h"

namespace VideoCore {

namespace {

std::vector<std::string_view> Split(std::string_view value, char separator) {
    std::vector<std::string_view> out;
    size_t start = 0;
    while (start <= value.size()) {
        size_t end = value.find(separator, start);
        if (end == std::string_view::npos) {
            end = value.size();
        }
        out.push_back(value.substr(start, end - start));
        start = end + 1;
    }
    return out;
}

bool IsSerializableName(std::string_view value) {
    return value.find_first_of(";|,=") == std::string_view::npos;
}

} // Anonymous namespace

std::vector<FxChainEntry> ParseFxChain(std::string_view value) {
    std::vector<FxChainEntry> parsed;

    for (const std::string_view record : Split(value, ';')) {
        if (record.empty()) {
            continue;
        }

        const auto fields = Split(record, '|');
        if (fields.size() < 2 || fields[0].empty() || fields[1].empty()) {
            continue;
        }

        FxChainEntry entry;
        entry.file = std::string(fields[0]);
        entry.technique = std::string(fields[1]);

        if (fields.size() >= 3) {
            for (const std::string_view assignment : Split(fields[2], ',')) {
                const size_t equals = assignment.find('=');
                if (equals == std::string_view::npos) {
                    continue;
                }
                const std::string name(assignment.substr(0, equals));
                if (name.empty()) {
                    continue;
                }

                std::array<f32, 4> components{};
                size_t index = 0;
                for (const std::string_view piece : Split(assignment.substr(equals + 1), '/')) {
                    if (index >= components.size()) {
                        break;
                    }
                    const std::string text(piece);
                    if (!text.empty()) {
                        components[index] = std::strtof(text.c_str(), nullptr);
                    }
                    ++index;
                }
                entry.values.emplace(name, components);
            }
        }

        parsed.push_back(std::move(entry));
    }

    return parsed;
}

std::string SerializeFxChain(std::span<const FxChainEntry> entries) {
    std::string out;

    for (const auto& entry : entries) {
        if (!IsSerializableName(entry.file) || !IsSerializableName(entry.technique)) {
            continue;
        }
        if (!out.empty()) {
            out += ';';
        }
        out += entry.file;
        out += '|';
        out += entry.technique;
        out += '|';

        bool first = true;
        for (const auto& [name, value] : entry.values) {
            if (!IsSerializableName(name)) {
                continue;
            }
            if (!first) {
                out += ',';
            }
            first = false;
            out += name;
            out += '=';
            for (size_t i = 0; i < value.size(); ++i) {
                if (i > 0) {
                    out += '/';
                }
                out += fmt::format("{}", value[i]);
            }
        }
    }

    return out;
}

FxChain& FxChain::Instance() {
    static FxChain instance;
    return instance;
}

FxChainSnapshot FxChain::Snapshot() const {
    std::scoped_lock lock{mutex};
    return FxChainSnapshot{
        .entries = entries,
        .generation = generation.load(std::memory_order_relaxed),
    };
}

std::vector<FxChainEntry> FxChain::Entries() const {
    std::scoped_lock lock{mutex};
    return entries;
}

size_t FxChain::Size() const {
    std::scoped_lock lock{mutex};
    return entries.size();
}

void FxChain::Append(std::string_view file, std::string_view technique) {
    {
        std::scoped_lock lock{mutex};
        FxChainEntry entry;
        entry.file = std::string(file);
        entry.technique = std::string(technique);
        entries.push_back(std::move(entry));
    }
    generation.fetch_add(1, std::memory_order_relaxed);
}

void FxChain::Replace(size_t index, std::string_view file, std::string_view technique) {
    {
        std::scoped_lock lock{mutex};
        if (index >= entries.size()) {
            return;
        }
        if (entries[index].file == file && entries[index].technique == technique) {
            return;
        }
        FxChainEntry entry;
        entry.file = std::string(file);
        entry.technique = std::string(technique);
        entries[index] = std::move(entry);
    }
    generation.fetch_add(1, std::memory_order_relaxed);
}

void FxChain::Remove(size_t index) {
    {
        std::scoped_lock lock{mutex};
        if (index >= entries.size()) {
            return;
        }
        entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(index));
    }
    generation.fetch_add(1, std::memory_order_relaxed);
}

void FxChain::Move(size_t index, int delta) {
    {
        std::scoped_lock lock{mutex};
        if (index >= entries.size()) {
            return;
        }
        const std::ptrdiff_t target = static_cast<std::ptrdiff_t>(index) + delta;
        if (target < 0 || target >= static_cast<std::ptrdiff_t>(entries.size())) {
            return;
        }
        std::swap(entries[index], entries[static_cast<size_t>(target)]);
    }
    generation.fetch_add(1, std::memory_order_relaxed);
}

void FxChain::Clear() {
    {
        std::scoped_lock lock{mutex};
        entries.clear();
    }
    generation.fetch_add(1, std::memory_order_relaxed);
}

void FxChain::SetValue(size_t index, std::string_view uniform, const std::array<f32, 4>& value) {
    std::scoped_lock lock{mutex};
    if (index >= entries.size()) {
        return;
    }
    entries[index].values[std::string(uniform)] = value;
}

std::array<f32, 4> FxChain::GetValue(size_t index, std::string_view uniform) const {
    std::scoped_lock lock{mutex};
    if (index >= entries.size()) {
        return {};
    }
    const auto it = entries[index].values.find(std::string(uniform));
    if (it == entries[index].values.end()) {
        return {};
    }
    return it->second;
}

std::map<std::string, std::array<f32, 4>> FxChain::EntryValues(size_t index) const {
    std::scoped_lock lock{mutex};
    if (index >= entries.size()) {
        return {};
    }
    return entries[index].values;
}

bool FxChain::HasValue(size_t index, std::string_view uniform) const {
    std::scoped_lock lock{mutex};
    if (index >= entries.size()) {
        return false;
    }
    return entries[index].values.contains(std::string(uniform));
}

void FxChain::ResetValues(size_t index) {
    std::scoped_lock lock{mutex};
    if (index >= entries.size()) {
        return;
    }
    entries[index].values.clear();
}

void FxChain::LoadFromSettings() {
    auto loaded = ParseFxChain(Settings::values.post_shader_chain.GetValue());

    std::scoped_lock lock{mutex};
    entries = std::move(loaded);
    generation.fetch_add(1, std::memory_order_relaxed);
}

void FxChain::StoreToSettings() const {
    std::string serialized;
    {
        std::scoped_lock lock{mutex};
        serialized = SerializeFxChain(entries);
    }
    Settings::values.post_shader_chain.SetValue(serialized);
}

void FxChain::DropUnknownEntries() {
    bool changed = false;
    {
        std::scoped_lock lock{mutex};
        const auto removed = std::remove_if(entries.begin(), entries.end(), [](const FxChainEntry& entry) {
            const FxEffectDesc* effect = FindFxEffect(entry.file);
            if (effect == nullptr || !effect->Valid()) {
                return true;
            }
            return std::find(effect->techniques.begin(), effect->techniques.end(),
                             entry.technique) == effect->techniques.end();
        });
        if (removed != entries.end()) {
            entries.erase(removed, entries.end());
            changed = true;
        }
    }
    if (changed) {
        generation.fetch_add(1, std::memory_order_relaxed);
    }
}

} // namespace VideoCore
