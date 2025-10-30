// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/fs/file.h"
#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "common/logging/log.h"
#include "common/settings.h"
#include "common/thread.h"
#include "core/hle/service/acc/profile_manager.h"
#include "play_time_manager.h"

#include <algorithm>
#include <fmt/format.h>

namespace PlayTime {

namespace {

struct PlayTimeElement {
    ProgramId program_id;
    PlayTime play_time;
};

std::optional<std::filesystem::path> GetCurrentUserPlayTimePath() {
    return Common::FS::GetEdenPath(Common::FS::EdenPath::PlayTimeDir) / "playtime.bin";
}

[[nodiscard]] bool ReadPlayTimeFile(PlayTimeDatabase& out_play_time_db) {
    const auto filename = GetCurrentUserPlayTimePath();

    if (!filename.has_value()) {
        LOG_ERROR(Frontend, "Failed to get current user path");
        return false;
    }

    out_play_time_db.clear();

    if (Common::FS::Exists(filename.value())) {
        Common::FS::IOFile file{filename.value(), Common::FS::FileAccessMode::Read,
                                Common::FS::FileType::BinaryFile};
        if (!file.IsOpen()) {
            LOG_ERROR(Frontend, "Failed to open play time file: {}",
                      Common::FS::PathToUTF8String(filename.value()));
            return false;
        }

        const size_t num_elements = file.GetSize() / sizeof(PlayTimeElement);
        std::vector<PlayTimeElement> elements(num_elements);

        if (file.ReadSpan<PlayTimeElement>(elements) != num_elements) {
            return false;
        }

        for (const auto& [program_id, play_time] : elements) {
            if (program_id != 0) {
                out_play_time_db[program_id] = play_time;
            }
        }
    }

    return true;
}

[[nodiscard]] bool WritePlayTimeFile(const PlayTimeDatabase& play_time_db) {
    const auto filename = GetCurrentUserPlayTimePath();

    if (!filename.has_value()) {
        LOG_ERROR(Frontend, "Failed to get current user path");
        return false;
    }

    Common::FS::IOFile file{filename.value(), Common::FS::FileAccessMode::Write,
                            Common::FS::FileType::BinaryFile};
    if (!file.IsOpen()) {
        LOG_ERROR(Frontend, "Failed to open play time file: {}",
                  Common::FS::PathToUTF8String(filename.value()));
        return false;
    }

    std::vector<PlayTimeElement> elements;
    elements.reserve(play_time_db.size());

    for (auto& [program_id, play_time] : play_time_db) {
        if (program_id != 0) {
            elements.push_back(PlayTimeElement{program_id, play_time});
        }
    }

    return file.WriteSpan<PlayTimeElement>(elements) == elements.size();
}

} // namespace

PlayTimeManager::PlayTimeManager() : running_program_id() {
    if (!ReadPlayTimeFile(database)) {
        LOG_ERROR(Frontend, "Failed to read play time database! Resetting to default.");
    }
}

PlayTimeManager::~PlayTimeManager() {
    Save();
}

void PlayTimeManager::SetProgramId(u64 program_id) {
    running_program_id = program_id;
}

void PlayTimeManager::Start() {
    play_time_thread = std::jthread([&](std::stop_token stop_token) { AutoTimestamp(stop_token); });
}

void PlayTimeManager::Stop() {
    play_time_thread = {};
}

void PlayTimeManager::AutoTimestamp(std::stop_token stop_token) {
    Common::SetCurrentThreadName("PlayTimeReport");

    using namespace std::literals::chrono_literals;
    using std::chrono::seconds;
    using std::chrono::steady_clock;

    auto timestamp = steady_clock::now();

    const auto GetDuration = [&]() -> u64 {
        const auto last_timestamp = std::exchange(timestamp, steady_clock::now());
        const auto duration = std::chrono::duration_cast<seconds>(timestamp - last_timestamp);
        return static_cast<u64>(duration.count());
    };

    while (!stop_token.stop_requested()) {
        Common::StoppableTimedWait(stop_token, 30s);

        database[running_program_id] += GetDuration();
        Save();
    }
}

void PlayTimeManager::Save() {
    if (!WritePlayTimeFile(database)) {
        LOG_ERROR(Frontend, "Failed to update play time database!");
    }
}

u64 PlayTimeManager::GetPlayTime(u64 program_id) const {
    auto it = database.find(program_id);
    if (it != database.end()) {
        return it->second;
    } else {
        return 0;
    }
}

void PlayTimeManager::SetPlayTime(u64 program_id, u64 play_time) {
    database[program_id] = play_time;
    Save();
}

void PlayTimeManager::ResetProgramPlayTime(u64 program_id) {
    database.erase(program_id);
    Save();
}

std::string PlayTimeManager::GetReadablePlayTime(u64 time_seconds) {
    if (time_seconds == 0) {
        return {};
    }

    const auto time_minutes = std::max(static_cast<double>(time_seconds) / 60.0, 1.0);
    const auto time_hours = static_cast<double>(time_seconds) / 3600.0;
    const bool is_minutes = time_minutes < 60.0;

    if (is_minutes) {
        return fmt::format("{:.0f} m", time_minutes);
    } else {
        const bool has_remainder = time_seconds % 60 != 0;
        if (has_remainder) {
            return fmt::format("{:.1f} h", time_hours);
        } else {
            return fmt::format("{:.0f} h", time_hours);
        }
    }
}

std::string PlayTimeManager::GetPlayTimeHours(u64 time_seconds) {
    return fmt::format("{}", time_seconds / 3600);
}

std::string PlayTimeManager::GetPlayTimeMinutes(u64 time_seconds) {
    return fmt::format("{}", (time_seconds % 3600) / 60);
}

std::string PlayTimeManager::GetPlayTimeSeconds(u64 time_seconds) {
    return fmt::format("{}", time_seconds % 60);
}

} // namespace PlayTime
