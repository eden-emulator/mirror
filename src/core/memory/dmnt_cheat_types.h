// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <vector>

#include "common/common_types.h"

namespace Core::Memory {

struct MemoryRegionExtents {
    u64 base{};
    u64 size{};
};

struct CheatProcessMetadata {
    u64 process_id{};
    u64 title_id{};
    MemoryRegionExtents main_nso_extents{};
    MemoryRegionExtents heap_extents{};
    MemoryRegionExtents alias_extents{};
    MemoryRegionExtents aslr_extents{};
    std::array<u8, 0x20> main_nso_build_id{};
};

struct CheatDefinition {
    std::array<char, 0x40> readable_name{};
    std::vector<u32> opcodes;
};

struct CheatEntry {
    bool enabled{};
    bool is_master{};
    u32 cheat_id{};
    CheatDefinition definition{};
};

struct RuntimeCheatInfo {
    u32 id{};
    std::string name;
    bool enabled{};
    bool is_master{};
};

} // namespace Core::Memory
