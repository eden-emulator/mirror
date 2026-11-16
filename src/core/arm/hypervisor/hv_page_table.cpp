// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <atomic>
#include <cstring>

#include <sys/mman.h>

#include "common/alignment.h"
#include "common/logging.h"
#include "core/arm/hypervisor/hv_machine.h"
#include "core/arm/hypervisor/hv_page_table.h"

namespace Core {
namespace {
constexpr u64 DESCRIPTOR_TYPE_MASK = 0x3;
constexpr u64 DESCRIPTOR_TABLE = 0x3;
constexpr u64 DESCRIPTOR_PAGE = 0x3;
constexpr u64 DESCRIPTOR_ACCESS_FLAG = 1ull << 10;
constexpr u64 DESCRIPTOR_INNER_SHAREABLE = 3ull << 8;
constexpr u64 DESCRIPTOR_ATTR_INDEX_NORMAL = 0ull << 2;
constexpr u64 DESCRIPTOR_ADDRESS_MASK = 0x0000'FFFF'FFFF'F000ull;

constexpr u64 ENTRIES_PER_TABLE = 512;
constexpr u64 TABLE_SIZE = ENTRIES_PER_TABLE * sizeof(u64);

constexpr size_t HOST_PAGE_SIZE = 0x4000;

// Live pages we repointed at different memory, see Map.
std::atomic<u64> repointed_live{};

constexpr u64 Level1Index(u64 va) {
    return (va >> 30) & (ENTRIES_PER_TABLE - 1);
}
constexpr u64 Level2Index(u64 va) {
    return (va >> 21) & (ENTRIES_PER_TABLE - 1);
}
constexpr u64 Level3Index(u64 va) {
    return (va >> 12) & (ENTRIES_PER_TABLE - 1);
}

} // Anonymous namespace

HvPageTable::HvPageTable(HvMachine& machine_) : machine{machine_} {
    if (!AllocateTable(root, root_ipa)) {
        LOG_ERROR(Core_ARM, "Could not allocate a page table root");
    }
}

HvPageTable::~HvPageTable() {
    for (const Backing& backing : backings) {
        machine.UnmapMemory(backing.ipa, backing.size);
        munmap(backing.host, backing.size);
    }
}

bool HvPageTable::AllocateTable(u64*& out_host, u64& out_ipa) {
    if (bump_remaining < TABLE_SIZE) {
        void* host = mmap(nullptr, HOST_PAGE_SIZE, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (host == MAP_FAILED) {
            LOG_ERROR(Core_ARM, "Could not allocate a page table backing");
            return false;
        }
        std::memset(host, 0, HOST_PAGE_SIZE);

        const u64 ipa = machine.AllocateTableIpa(HOST_PAGE_SIZE);
        if (ipa == 0) {
            munmap(host, HOST_PAGE_SIZE);
            return false;
        }
        if (!machine.MapMemory(host, ipa, HOST_PAGE_SIZE)) {
            munmap(host, HOST_PAGE_SIZE);
            return false;
        }

        backings.push_back({host, ipa, HOST_PAGE_SIZE});

        bump_host = static_cast<u8*>(host);
        bump_ipa = ipa;
        bump_remaining = HOST_PAGE_SIZE;
    }

    out_host = reinterpret_cast<u64*>(bump_host);
    out_ipa = bump_ipa;
    tables_by_ipa.emplace(out_ipa, out_host);

    bump_host += TABLE_SIZE;
    bump_ipa += TABLE_SIZE;
    bump_remaining -= TABLE_SIZE;
    return true;
}

u64* HvPageTable::WalkTo(u64 va, bool create) {
    if (root == nullptr) {
        return nullptr;
    }

    u64* table = root;
    for (const u64 index : {Level1Index(va), Level2Index(va)}) {
        u64& descriptor = table[index];
        if ((descriptor & DESCRIPTOR_TYPE_MASK) != DESCRIPTOR_TABLE) {
            if (!create) {
                return nullptr;
            }
            u64* next_host{};
            u64 next_ipa{};
            if (!AllocateTable(next_host, next_ipa)) {
                return nullptr;
            }
            // Zero the new table before anything can walk into it. Publish the
            // descriptor first and another core's table walker reads memory
            // directly, past our store buffer, and descends into stale entries.
            asm volatile("dsb ishst" ::: "memory");
            descriptor = next_ipa | DESCRIPTOR_TABLE;
            table = next_host;
            continue;
        }

        const auto it = tables_by_ipa.find(descriptor & DESCRIPTOR_ADDRESS_MASK);
        if (it == tables_by_ipa.end()) {
            LOG_ERROR(Core_ARM, "Page table descriptor points at an unknown table");
            return nullptr;
        }
        table = it->second;
    }

    return &table[Level3Index(va)];
}

bool HvPageTable::Map(u64 va, u64 ipa, u64 size, HvPermission permission,
                      bool* replaced_live) {
    if (!Common::IsAligned(va, PAGE_SIZE) || !Common::IsAligned(ipa, PAGE_SIZE) ||
        !Common::IsAligned(size, PAGE_SIZE)) {
        LOG_ERROR(Core_ARM, "Unaligned mapping va={:#x} ipa={:#x} size={:#x}", va, ipa, size);
        return false;
    }

    std::scoped_lock lock{mutex};
    for (u64 offset = 0; offset < size; offset += PAGE_SIZE) {
        u64* descriptor = WalkTo(va + offset, true);
        if (descriptor == nullptr) {
            return false;
        }
        const u64 desired = ((ipa + offset) & DESCRIPTOR_ADDRESS_MASK) | DESCRIPTOR_PAGE |
                            DESCRIPTOR_ACCESS_FLAG | DESCRIPTOR_INNER_SHAREABLE |
                            DESCRIPTOR_ATTR_INDEX_NORMAL | static_cast<u64>(permission);

        if (*descriptor != desired && (*descriptor & DESCRIPTOR_TYPE_MASK) == DESCRIPTOR_PAGE) {
            if (replaced_live != nullptr) {
                *replaced_live = true;
            }
            // Permission changes are fine. Pointing a live page at different
            // memory is not, everything the guest stored through that VA goes
            // invisible and looks like a lost store.
            const u64 old_ipa = *descriptor & DESCRIPTOR_ADDRESS_MASK;
            const u64 new_ipa = desired & DESCRIPTOR_ADDRESS_MASK;
            if (old_ipa != new_ipa) {
                const u64 n = repointed_live.fetch_add(1, std::memory_order::relaxed) + 1;
                if (n <= 32 || (n & (n - 1)) == 0) {
                    LOG_WARNING(Core_ARM,
                                "Live page va={:#x} repointed ipa {:#x} -> {:#x} ({} so far)", va,
                                old_ipa, new_ipa, n);
                }
            }
        }
        *descriptor = desired;
    }
    return true;
}

bool HvPageTable::Unmap(u64 va, u64 size) {
    if (!Common::IsAligned(va, PAGE_SIZE) || !Common::IsAligned(size, PAGE_SIZE)) {
        LOG_ERROR(Core_ARM, "Unaligned unmap va={:#x} size={:#x}", va, size);
        return false;
    }

    std::scoped_lock lock{mutex};
    bool removed = false;
    for (u64 offset = 0; offset < size; offset += PAGE_SIZE) {
        u64* descriptor = WalkTo(va + offset, false);
        if (descriptor != nullptr && *descriptor != 0) {
            *descriptor = 0;
            removed = true;
        }
    }
    return removed;
}

} // namespace Core
