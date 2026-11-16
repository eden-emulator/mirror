// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <vector>

#include "core/arm/exclusive_monitor.h"
#include "core/memory.h"

namespace Core {
class HvExclusiveMonitor final : public ExclusiveMonitor {
public:
    explicit HvExclusiveMonitor(Memory::Memory& memory, std::size_t core_count);
    ~HvExclusiveMonitor() override;

    u8 ExclusiveRead8(std::size_t core_index, VAddr addr) override;
    u16 ExclusiveRead16(std::size_t core_index, VAddr addr) override;
    u32 ExclusiveRead32(std::size_t core_index, VAddr addr) override;
    u64 ExclusiveRead64(std::size_t core_index, VAddr addr) override;
    u128 ExclusiveRead128(std::size_t core_index, VAddr addr) override;
    void ClearExclusive(std::size_t core_index) override;

    bool ExclusiveWrite8(std::size_t core_index, VAddr vaddr, u8 value) override;
    bool ExclusiveWrite16(std::size_t core_index, VAddr vaddr, u16 value) override;
    bool ExclusiveWrite32(std::size_t core_index, VAddr vaddr, u32 value) override;
    bool ExclusiveWrite64(std::size_t core_index, VAddr vaddr, u64 value) override;
    bool ExclusiveWrite128(std::size_t core_index, VAddr vaddr, u128 value) override;

private:
    template <typename T>
    T TakeReservation(std::size_t core_index, VAddr addr, T fallback);

    template <typename T>
    bool ReleaseReservation(std::size_t core_index, VAddr vaddr, T value);

    struct Reservation {
        VAddr address{};
        u128 value{};
        bool held{};
        bool reserved{};
    };

    static constexpr u32 LOST_RESERVATION_RETRIES = 1;

    Memory::Memory& m_memory;
    std::vector<Reservation> m_reservations;
    std::vector<u32> m_lost_reservations;
};

} // namespace Core
