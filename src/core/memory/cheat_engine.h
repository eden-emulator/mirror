// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <vector>
#include "common/common_types.h"
#include "core/memory/cheat_parser.h"
#include "core/memory/dmnt_cheat_types.h"
#include "core/memory/dmnt_cheat_vm.h"

namespace Core {
class System;
}

namespace Kernel {
class KProcess;
}

namespace Core::Memory {

class StandardVmCallbacks : public DmntCheatVm::Callbacks {
public:
    StandardVmCallbacks(System& system_, Kernel::KProcess& process_, const CheatProcessMetadata& metadata_);
    ~StandardVmCallbacks() override;

    void MemoryReadUnsafe(VAddr address, void* data, u64 size) override;
    void MemoryWriteUnsafe(VAddr address, const void* data, u64 size) override;
    u64 HidKeysDown() override;
    void PauseProcess() override;
    void ResumeProcess() override;
    void DebugLog(u8 id, u64 value) override;
    void CommandLog(std::string_view data) override;
    [[nodiscard]] bool IsCommandLogEnabled() const override;

private:
    bool IsAddressInRange(VAddr address) const;

    const CheatProcessMetadata& metadata;
    Core::System& system;
    Kernel::KProcess& process;
};

// Runs a CheatList against the process that loaded it. Not thread-safe: System serializes every
// call and drives Execute from CoreTiming.
class CheatEngine final {
public:
    CheatEngine(System& system_, Kernel::KProcess& process_, CheatList cheats_,
                const std::array<u8, 0x20>& build_id_, VAddr main_region_begin,
                u64 main_region_size);

    [[nodiscard]] std::vector<RuntimeCheatInfo> GetCheats() const;
    // Accepts a cheat, a submenu or CHEAT_MENU_ROOT; groups toggle every regular cheat inside.
    bool SetCheatEnabled(u32 id, bool enabled);
    // Runs one cheat frame against the process, if it is running.
    void Execute();

private:
    DmntCheatVm vm;
    CheatProcessMetadata metadata;

    std::vector<CheatEntry> cheats;
    // Read-only GUI metadata indexed by menu id; enabled states are tracked in `cheats` only.
    std::vector<RuntimeCheatInfo> menu;
    bool is_pending_reload{true};

    Kernel::KProcess& process;
};

} // namespace Core::Memory
