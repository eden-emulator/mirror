// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <mutex>
#include <unordered_map>
#include <vector>

#include "common/common_funcs.h"
#include "common/common_types.h"

namespace Core {
class HvMachine;

enum class HvPermission : u64 {
    KernelReadExecute = (1ull << 54) | (0ull << 6),
    UserReadExecute = (1ull << 53) | (3ull << 6),
    UserReadWriteExecute = (1ull << 53) | (1ull << 6),
};

class HvPageTable : Common::NonCopyable, Common::NonMoveable {
public:

    static constexpr u32 VA_BITS = 39;
    static constexpr u64 PAGE_BITS = 12;
    static constexpr u64 PAGE_SIZE = 1ull << PAGE_BITS;

    explicit HvPageTable(HvMachine& machine);
    ~HvPageTable();

    [[nodiscard]] u64 BaseRegister() const noexcept {
        return root_ipa;
    }

    [[nodiscard]] bool IsValid() const noexcept {
        return root != nullptr;
    }

    bool Map(u64 va, u64 ipa, u64 size, HvPermission permission,
             bool* replaced_live = nullptr);
    bool Unmap(u64 va, u64 size);

private:
    static constexpr u64 ENTRIES_PER_TABLE = 512;

    [[nodiscard]] u64* WalkTo(u64 va, bool create);

    [[nodiscard]] bool AllocateTable(u64*& out_host, u64& out_ipa);

    HvMachine& machine;

    u64* root{};
    u64 root_ipa{};

    std::unordered_map<u64, u64*> tables_by_ipa;

    struct Backing {
        void* host{};
        u64 ipa{};
        size_t size{};
    };
    std::vector<Backing> backings;

    u8* bump_host{};
    u64 bump_ipa{};
    size_t bump_remaining{};

    mutable std::mutex mutex;
};

} // namespace Core
