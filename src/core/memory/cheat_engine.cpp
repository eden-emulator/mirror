// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstring>
#include <utility>
#include "common/logging.h"
#include "common/swap.h"
#include "core/arm/debug.h"
#include "core/core.h"
#include "core/hle/kernel/k_page_table.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/k_process_page_table.h"
#include "core/hle/kernel/svc_types.h"
#include "core/hle/service/hid/hid_server.h"
#include "core/hle/service/sm/sm.h"
#include "core/memory.h"
#include "core/memory/cheat_engine.h"
#include "core/hle/service/hid/resource_manager.h"
#include "core/hle/service/hid/resources/npad/npad.h"

namespace Core::Memory {
namespace {
// Whether the menu entry is the group or nested inside it; every entry is inside the root.
bool IsInGroup(const std::vector<RuntimeCheatInfo>& menu, u32 id, u32 group) {
    while (id != group && id != CHEAT_MENU_ROOT) {
        id = menu[id].parent_id;
    }
    return id == group;
}
} // Anonymous namespace

StandardVmCallbacks::StandardVmCallbacks(System& system_, Kernel::KProcess& process_,
                                         const CheatProcessMetadata& metadata_)
    : metadata{metadata_}, system{system_}, process{process_} {
}

StandardVmCallbacks::~StandardVmCallbacks() = default;

void StandardVmCallbacks::MemoryReadUnsafe(VAddr address, void* data, u64 size) {
    // Return zero on invalid address
    if (!IsAddressInRange(address) || !process.GetMemory().IsValidVirtualAddress(address)) {
        std::memset(data, 0, size);
        return;
    }

    process.GetMemory().ReadBlock(address, data, size);
}

void StandardVmCallbacks::MemoryWriteUnsafe(VAddr address, const void* data, u64 size) {
    // Skip invalid memory write address
    if (!IsAddressInRange(address) || !process.GetMemory().IsValidVirtualAddress(address)) {
        return;
    }

    // Cheats write the same values over and over (12 times a second): a write that changes nothing
    // has nothing to write or to invalidate
    std::array<u8, 16> current{};
    const bool have_current =
        size <= current.size() && process.GetMemory().ReadBlock(address, current.data(), size);
    if (have_current && std::memcmp(current.data(), data, size) == 0) {
        return;
    }
    const auto is_executable = [this](VAddr page_address) {
        Kernel::KMemoryInfo info{};
        Kernel::Svc::PageInfo page_info{};
        if (process.GetPageTable().QueryInfo(&info, &page_info, page_address) != ResultSuccess) {
            return true;
        }
        return True(info.GetPermission() & Kernel::KMemoryPermission::UserExecute);
    };
    const bool code = is_executable(address) || is_executable(address + size - 1);
    // A change to code is kept as it was first, to put it back when the cheat is turned off
    // (cheat writes are 1 to 8 bytes, so the bytes read above are the originals)
    if (code && have_current) {
        for (u64 i = 0; i < size; ++i) {
            original_code.try_emplace(address + i, current[i]);
        }
    }
    if (!process.GetMemory().WriteBlock(address, data, size)) {
        return;
    }
    // Only code needs the translated code dropped. Invalidating on every data write halted every
    // emulated core and searched its translated blocks, at a cost that grows as more code is
    // translated (Tears of the Kingdom with cheats: ~480 invalidations a second, the frame rate
    // falling over the session).
    if (code) {
        Core::InvalidateInstructionCacheRange(&process, address, size);
    }
}

void StandardVmCallbacks::RestoreOriginalCode() {
    std::size_t restored = 0;
    for (auto it = original_code.begin(); it != original_code.end();) {
        // One run of neighbouring bytes at a time
        const VAddr start = it->first;
        std::vector<u8> run;
        for (VAddr next = start; it != original_code.end() && it->first == next; ++it, ++next) {
            run.push_back(it->second);
        }
        if (process.GetMemory().IsValidVirtualAddress(start) &&
            process.GetMemory().WriteBlock(start, run.data(), run.size())) {
            Core::InvalidateInstructionCacheRange(&process, start, run.size());
            restored += run.size();
        }
    }
    original_code.clear();
    LOG_INFO(CheatEngine, "A cheat was turned off: put back {} bytes of the game's code", restored);
}

u64 StandardVmCallbacks::HidKeysDown() {
    const auto hid = system.ServiceManager().GetService<Service::HID::IHidServer>("hid");
    if (hid == nullptr) {
        LOG_WARNING(CheatEngine, "Attempted to read input state, but hid is not initialized!");
        return 0;
    }

    const auto applet_resource = hid->GetResourceManager();
    if (applet_resource == nullptr || applet_resource->GetNpad() == nullptr) {
        LOG_WARNING(CheatEngine,
                    "Attempted to read input state, but applet resource is not initialized!");
        return 0;
    }

    const auto press_state = applet_resource->GetNpad()->GetAndResetPressState();
    return static_cast<u64>(press_state & HID::NpadButton::All);
}

void StandardVmCallbacks::PauseProcess() {
    if (!process.IsSuspended()) {
        process.SetActivity(system.Kernel(), Kernel::Svc::ProcessActivity::Paused);
    }
}

void StandardVmCallbacks::ResumeProcess() {
    if (process.IsSuspended()) {
        process.SetActivity(system.Kernel(), Kernel::Svc::ProcessActivity::Runnable);
    }
}

void StandardVmCallbacks::DebugLog(u8 id, u64 value) {
    LOG_INFO(CheatEngine, "Cheat triggered DebugLog: ID '{:01X}' Value '{:016X}'", id, value);
}

void StandardVmCallbacks::CommandLog(std::string_view data) {
    LOG_DEBUG(CheatEngine, "[DmntCheatVm]: {}",
              data.back() == '\n' ? data.substr(0, data.size() - 1) : data);
}

bool StandardVmCallbacks::IsCommandLogEnabled() const {
    return Common::Log::IsMessageEnabled(Common::Log::Class::CheatEngine,
                                         Common::Log::Level::Debug);
}

bool StandardVmCallbacks::IsAddressInRange(VAddr in) const {
    if ((in < metadata.main_nso_extents.base ||
         in >= metadata.main_nso_extents.base + metadata.main_nso_extents.size) &&
        (in < metadata.heap_extents.base ||
         in >= metadata.heap_extents.base + metadata.heap_extents.size) &&
        (in < metadata.alias_extents.base ||
         in >= metadata.alias_extents.base + metadata.alias_extents.size) &&
        (in < metadata.aslr_extents.base ||
         in >= metadata.aslr_extents.base + metadata.aslr_extents.size)) {
        LOG_DEBUG(CheatEngine,
                  "Cheat attempting to access memory at invalid address={:016X}, if this "
                  "persists, "
                  "the cheat may be incorrect. However, this may be normal early in execution if "
                  "the game has not properly set up yet.",
                  in);
        return false; ///< Invalid addresses will hard crash
    }

    return true;
}

CheatEngine::CheatEngine(System& system_, Kernel::KProcess& process_, CheatList cheats_,
                         const std::array<u8, 0x20>& build_id_, VAddr main_region_begin,
                         u64 main_region_size)
    : vm{std::make_unique<StandardVmCallbacks>(system_, process_, metadata)},
      cheats(std::move(cheats_.cheats)), menu(std::move(cheats_.menu)), process{process_} {
    metadata.main_nso_build_id = build_id_;
    for (auto& cheat : cheats) {
        cheat.enabled = cheat.is_master || cheat.enabled;
    }

    metadata.process_id = process.GetProcessId();
    metadata.title_id = process.GetProgramId();
    metadata.main_nso_extents = {
        .base = main_region_begin,
        .size = main_region_size,
    };

    const auto& page_table = process.GetPageTable();
    metadata.heap_extents = {
        .base = GetInteger(page_table.GetHeapRegionStart()),
        .size = page_table.GetHeapRegionSize(),
    };
    metadata.aslr_extents = {
        .base = GetInteger(page_table.GetAliasCodeRegionStart()),
        .size = page_table.GetAliasCodeRegionSize(),
    };
    metadata.alias_extents = {
        .base = GetInteger(page_table.GetAliasRegionStart()),
        .size = page_table.GetAliasRegionSize(),
    };
}

std::vector<RuntimeCheatInfo> CheatEngine::GetCheats() const {
    auto result = menu;
    for (const auto& cheat : cheats) {
        result[cheat.cheat_id].enabled = cheat.enabled;
    }
    return result;
}

bool CheatEngine::SetCheatEnabled(u32 id, bool enabled) {
    if (id != CHEAT_MENU_ROOT) {
        if (id >= menu.size()) {
            return false;
        }
        if (menu[id].is_master) {
            // Master codes cannot be disabled.
            return enabled;
        }
    }
    for (auto& cheat : cheats) {
        if (!cheat.is_master && cheat.enabled != enabled && IsInGroup(menu, cheat.cheat_id, id)) {
            cheat.enabled = enabled;
            is_pending_reload = true;
            is_pending_restore = is_pending_restore || !enabled;
        }
    }
    return true;
}

void CheatEngine::Execute() {
    // Modules register before loading finishes, and an exited application stays current until
    // AM removes it. Only execute against a running process.
    const auto state = process.GetState();
    if (state != Kernel::KProcess::State::Running &&
        state != Kernel::KProcess::State::RunningAttached &&
        state != Kernel::KProcess::State::DebugBreak) {
        return;
    }

    // (Before the reload: the cheats still on write their changes again on this tick)
    if (std::exchange(is_pending_restore, false)) {
        static_cast<StandardVmCallbacks*>(vm.GetCallbacks())->RestoreOriginalCode();
    }
    if (std::exchange(is_pending_reload, false)) {
        vm.LoadProgram(cheats);
    }

    if (vm.GetProgramSize() == 0) {
        return;
    }

    vm.Execute(metadata);
}

} // namespace Core::Memory
