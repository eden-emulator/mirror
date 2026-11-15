// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <vector>

#include "common/common_types.h"

/// Finds a value in the running game's memory and changes it (a live memory editor: nothing is
/// saved as a cheat). A first scan finds every place holding the value; the next scans keep the
/// places that hold the new value after it changed in the game, until a few are left.
/// Searches the game's readable and writable memory (heap, data, stacks). The places found
/// belong to one run of one game: they are dropped when another one runs.
class MemoryScanner {
public:
    enum class Type {
        U8,
        U16,
        U32,
        U64,
        F32,
    };

    /// What a next scan keeps: the places now holding the value, or whose value went up, went
    /// down, changed or stayed the same since the scan before
    enum class Compare {
        Equal,
        Increased,
        Decreased,
        Changed,
        Unchanged,
    };

    /// The most places kept from a scan (a common value like 0 or 1 is everywhere)
    static constexpr std::size_t MaxResults = 1'000'000;

    /// Drops the places found
    void Reset();
    /// Every place holding the value; returns how many (at most MaxResults kept)
    std::size_t FirstScan(Type type, double value);
    /// Keeps the places that compare as asked (Equal: to the value); returns how many are left
    std::size_t NextScan(Compare compare, double value);

    [[nodiscard]] bool HasResults() const {
        return scanned;
    }
    [[nodiscard]] const std::vector<u64>& Results() const {
        return results;
    }
    /// The first scan found more than MaxResults (only the first ones are kept)
    [[nodiscard]] bool Truncated() const {
        return truncated;
    }
    [[nodiscard]] Type ScanType() const {
        return type;
    }
    /// The value at a place found, now (0 when it can't be read)
    [[nodiscard]] double Read(u64 address) const;
    /// Writes the value at a place found; false when it can't
    bool Write(u64 address, double value) const;

    [[nodiscard]] static std::size_t Size(Type type);
    [[nodiscard]] static const char* Name(Type type);
    [[nodiscard]] static const char* Name(Compare compare);

private:
    /// Whether the places found belong to the game running now
    [[nodiscard]] bool SameRun() const;

    Type type = Type::U32;
    bool scanned = false;
    bool truncated = false;
    u64 process_id = 0;
    std::vector<u64> results;
    /// What each place held at the scan before (for increased, decreased, ...)
    std::vector<u64> previous;
};
