// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstring>

#include "common/logging.h"
#include "core/arm/hypervisor/hv_exclusive_monitor.h"

namespace Core {
namespace {
template <typename T>
T* HostPointer(Memory::Memory& memory, VAddr address) {
    if ((address % sizeof(T)) != 0) {
        return nullptr;
    }
    u8* const host = memory.GetPointerSilent(Common::ProcessAddress{address});
    return reinterpret_cast<T*>(host);
}

template <typename T>
T AtomicLoad(Memory::Memory& memory, VAddr address, T fallback) {
    T* const host = HostPointer<T>(memory, address);
    if (host == nullptr) {
        return fallback;
    }
    return __atomic_load_n(host, __ATOMIC_ACQUIRE);
}

template <typename T>
bool AtomicCompareExchange(Memory::Memory& memory, VAddr address, T expected, T value) {
    T* const host = HostPointer<T>(memory, address);
    if (host == nullptr) {
        LOG_ERROR(Core_ARM, "Inaccessible exclusive write{} @ {:#016x} = {:#016x}", sizeof(T) * 8,
                  address, static_cast<u64>(value));
        return true;
    }
    return __atomic_compare_exchange_n(host, std::addressof(expected), value, false,
                                       __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

[[nodiscard]] u32 LoadExclusive(const u32* ptr) {
    u32 value;
    asm volatile("ldaxr %w0, [%1]" : "=&r"(value) : "r"(ptr) : "memory");
    return value;
}
[[nodiscard]] u64 LoadExclusive(const u64* ptr) {
    u64 value;
    asm volatile("ldaxr %x0, [%1]" : "=&r"(value) : "r"(ptr) : "memory");
    return value;
}

[[nodiscard]] bool StoreExclusive(u32* ptr, u32 value) {
    u32 status;
    asm volatile("stlxr %w0, %w2, [%1]" : "=&r"(status) : "r"(ptr), "r"(value) : "memory");
    return status == 0;
}
[[nodiscard]] bool StoreExclusive(u64* ptr, u64 value) {
    u32 status;
    asm volatile("stlxr %w0, %x2, [%1]" : "=&r"(status) : "r"(ptr), "r"(value) : "memory");
    return status == 0;
}

void ClearExclusiveLocal() {
    asm volatile("clrex" ::: "memory");
}

template <typename T>
constexpr bool UsesHardwareMonitor = sizeof(T) == 4 || sizeof(T) == 8;

} // Anonymous namespace

HvExclusiveMonitor::HvExclusiveMonitor(Memory::Memory& memory, std::size_t core_count)
    : m_memory{memory}, m_reservations(core_count), m_lost_reservations(core_count) {}

HvExclusiveMonitor::~HvExclusiveMonitor() = default;

template <typename T>
T HvExclusiveMonitor::TakeReservation(std::size_t core_index, VAddr addr, T fallback) {
    T value{};
    bool reserved = false;
    if constexpr (UsesHardwareMonitor<T>) {
        if (T* const host = HostPointer<T>(m_memory, addr)) {
            value = LoadExclusive(host);
            reserved = true;
        } else {
            value = fallback;
        }
    } else {
        value = AtomicLoad<T>(m_memory, addr, fallback);
    }
    m_reservations[core_index] = {addr, u128{value, 0}, true, reserved};
    return value;
}

template <typename T>
bool HvExclusiveMonitor::ReleaseReservation(std::size_t core_index, VAddr vaddr, T value) {
    Reservation& reservation = m_reservations[core_index];
    if (!reservation.held || reservation.address != vaddr) {
        return false;
    }
    reservation.held = false;
    const T expected = static_cast<T>(reservation.value[0]);

    if constexpr (UsesHardwareMonitor<T>) {
        T* const host = HostPointer<T>(m_memory, vaddr);
        if (host == nullptr) {
            // Fall through to the compare-exchange below. Returning true told the
            // guest its store-exclusive had landed when nothing was written at all,
            // and returning false hangs it, the kernel retries a failed STXR forever.
            LOG_ERROR(Core_ARM, "Inaccessible exclusive write{} @ {:#016x}", sizeof(T) * 8, vaddr);
        } else if (reservation.reserved) {
            if (StoreExclusive(host, value)) {
                m_lost_reservations[core_index] = 0;
                return true;
            }
            if (++m_lost_reservations[core_index] <= LOST_RESERVATION_RETRIES) {
                return false;
            }
            m_lost_reservations[core_index] = 0;
        }
        ClearExclusiveLocal();
    }
    return AtomicCompareExchange<T>(m_memory, vaddr, expected, value);
}

u8 HvExclusiveMonitor::ExclusiveRead8(std::size_t core_index, VAddr addr) {
    return TakeReservation<u8>(core_index, addr, m_memory.Read8(addr));
}

u16 HvExclusiveMonitor::ExclusiveRead16(std::size_t core_index, VAddr addr) {
    return TakeReservation<u16>(core_index, addr, m_memory.Read16(addr));
}

u32 HvExclusiveMonitor::ExclusiveRead32(std::size_t core_index, VAddr addr) {
    return TakeReservation<u32>(core_index, addr, m_memory.Read32(addr));
}

u64 HvExclusiveMonitor::ExclusiveRead64(std::size_t core_index, VAddr addr) {
    return TakeReservation<u64>(core_index, addr, m_memory.Read64(addr));
}

u128 HvExclusiveMonitor::ExclusiveRead128(std::size_t core_index, VAddr addr) {
    const u128 value{m_memory.Read64(addr), m_memory.Read64(addr + sizeof(u64))};
    m_reservations[core_index] = {addr, value, true};
    return value;
}

void HvExclusiveMonitor::ClearExclusive(std::size_t core_index) {
    m_reservations[core_index].held = false;
    if (m_reservations[core_index].reserved) {
        m_reservations[core_index].reserved = false;
        ClearExclusiveLocal();
    }
}

bool HvExclusiveMonitor::ExclusiveWrite8(std::size_t core_index, VAddr vaddr, u8 value) {
    return ReleaseReservation<u8>(core_index, vaddr, value);
}

bool HvExclusiveMonitor::ExclusiveWrite16(std::size_t core_index, VAddr vaddr, u16 value) {
    return ReleaseReservation<u16>(core_index, vaddr, value);
}

bool HvExclusiveMonitor::ExclusiveWrite32(std::size_t core_index, VAddr vaddr, u32 value) {
    return ReleaseReservation<u32>(core_index, vaddr, value);
}

bool HvExclusiveMonitor::ExclusiveWrite64(std::size_t core_index, VAddr vaddr, u64 value) {
    return ReleaseReservation<u64>(core_index, vaddr, value);
}

bool HvExclusiveMonitor::ExclusiveWrite128(std::size_t core_index, VAddr vaddr, u128 value) {
    Reservation& reservation = m_reservations[core_index];
    if (!reservation.held || reservation.address != vaddr) {
        return false;
    }
    reservation.held = false;

    unsigned __int128 expected{};
    unsigned __int128 desired{};
    std::memcpy(std::addressof(expected), reservation.value.data(), sizeof(expected));
    std::memcpy(std::addressof(desired), value.data(), sizeof(desired));

    auto* const host = HostPointer<unsigned __int128>(m_memory, vaddr);
    if (host == nullptr) {
        LOG_ERROR(Core_ARM, "Inaccessible exclusive write128 @ {:#016x}", vaddr);
        return true;
    }
    return __atomic_compare_exchange_n(host, std::addressof(expected), desired, false,
                                       __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

} // namespace Core
