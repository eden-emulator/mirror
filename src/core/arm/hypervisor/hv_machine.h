// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>

#include <Hypervisor/Hypervisor.h>

#include "common/common_funcs.h"
#include "common/common_types.h"

namespace Core {
class HvPageTable;

class HvMachine : Common::NonCopyable, Common::NonMoveable {
public:

    [[nodiscard]] static HvMachine* Instance();

    ~HvMachine();
    [[nodiscard]] bool MapMemory(void* host_addr, u64 ipa, u64 size);

    void UnmapMemory(u64 ipa, u64 size);

    [[nodiscard]] u64 IpaLimit() const noexcept {
        return max_ipa_size_bits == 0 || max_ipa_size_bits >= 64
                   ? 0
                   : (1ull << max_ipa_size_bits);
    }

    static constexpr u64 KERNEL_REGION_VA = 0xFFFF'FF80'0000'0000ull;

    static constexpr u64 EXCEPTION_TRAMPOLINE_OFFSET = 0x800;

    static constexpr u64 FLUSH_TRAMPOLINE_OFFSET = 0x820;

    [[nodiscard]] HvPageTable& KernelPageTable() {
        return *kernel_page_table;
    }

    [[nodiscard]] bool HasKernelRegion() const noexcept {
        return kernel_region_ready;
    }

    [[nodiscard]] bool EnsureDramMapped(void* backing, u64 dram_base, u64 size);

    [[nodiscard]] u64 AllocateTableIpa(u64 size);

    [[nodiscard]] HvPageTable* ProcessPageTable(const void* process);

private:

    HvMachine();

    bool InitializeKernelRegion();

    bool created{};
    u32 max_ipa_size_bits{};
    std::mutex map_mutex;

    std::unique_ptr<HvPageTable> kernel_page_table;
    void* kernel_region_host{};
    bool kernel_region_ready{};

    bool dram_mapped{};
    void* dram_backing{};
    u64 dram_base{};
    u64 dram_size{};
    u64 next_table_ipa{};
    u64 table_ipa_after_kernel{};
    std::mutex table_ipa_mutex;
    std::unordered_map<const void*, std::unique_ptr<HvPageTable>> process_page_tables;
    std::mutex process_mutex;
};

} // namespace Core
