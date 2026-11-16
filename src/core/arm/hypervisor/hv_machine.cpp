// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstring>
#include <memory>
#include <mutex>

#include <sys/mman.h>

#include "common/logging.h"
#include "core/arm/hypervisor/arm_hypervisor.h"
#include "core/arm/hypervisor/hv_machine.h"
#include "core/arm/hypervisor/hv_page_table.h"

namespace Core {
namespace {
std::mutex instance_mutex;
std::unique_ptr<HvMachine> instance;
bool instance_attempted{};

constexpr size_t HOST_PAGE_SIZE = 0x4000;

constexpr u64 KERNEL_REGION_IPA = 0x4'0000'0000ull;

constexpr u64 PAGE_TABLE_IPA_BASE = 0x8'0000'0000ull;

constexpr u64 PAGE_TABLE_ARENA_MINIMUM = 256ull * 1024 * 1024;

constexpr u32 MakeHvc(u32 imm16) {
    return 0xD4000002u | (imm16 << 5);
}

constexpr u32 INSTRUCTION_BRK_0 = 0xD4200000u;
constexpr u32 INSTRUCTION_ERET = 0xD69F03E0u;
constexpr u32 INSTRUCTION_CLREX = 0xD5033F5Fu;
constexpr u32 INSTRUCTION_TLBI_VMALLE1IS = 0xD508831Fu;
constexpr u32 INSTRUCTION_IC_IALLU = 0xD508751Fu;
constexpr u32 INSTRUCTION_DSB_ISH = 0xD5033B9Fu;
constexpr u32 INSTRUCTION_ISB = 0xD5033FDFu;

constexpr u64 VECTOR_ENTRY_SIZE = 0x80;
constexpr u64 VECTOR_ENTRY_COUNT = 16;

} // Anonymous namespace

HvMachine::HvMachine() {
    // Ask for the hardware maximum IPA size. It only raises a limit, nothing is
    // allocated, and the layout below has to fit under it.
    hv_vm_config_t config = hv_vm_config_create();
    if (config == nullptr) {
        LOG_WARNING(Core_ARM, "Could not create a hypervisor VM config");
        return;
    }

    hv_return_t result = hv_vm_config_get_max_ipa_size(&max_ipa_size_bits);
    if (result != HV_SUCCESS) {
        LOG_WARNING(Core_ARM, "hv_vm_config_get_max_ipa_size failed: {:#x}",
                    static_cast<u32>(result));
        os_release(config);
        return;
    }
    result = hv_vm_config_set_ipa_size(config, max_ipa_size_bits);
    if (result != HV_SUCCESS) {
        LOG_WARNING(Core_ARM, "hv_vm_config_set_ipa_size({}) failed: {:#x}", max_ipa_size_bits,
                    static_cast<u32>(result));
        os_release(config);
        return;
    }

    // KERNEL_REGION_IPA and the arena above it are fixed, so a host reporting
    // fewer bits would put the arena outside what the VM can translate.
    const u64 ipa_limit = IpaLimit();
    const u64 required = PAGE_TABLE_IPA_BASE + PAGE_TABLE_ARENA_MINIMUM;
    if (ipa_limit < required) {
        LOG_ERROR(Core_ARM,
                  "This host's VM supports {} bits of IPA ({:#x}), too small for the backend's "
                  "layout: the kernel region is at {:#x} and the page-table arena needs {:#x}",
                  max_ipa_size_bits, ipa_limit, KERNEL_REGION_IPA, required);
        os_release(config);
        return;
    }

    LOG_INFO(Core_ARM, "VM IPA size: {} bits ({:#x}); kernel region {:#x}, page tables from {:#x}",
             max_ipa_size_bits, ipa_limit, KERNEL_REGION_IPA, PAGE_TABLE_IPA_BASE);

    result = hv_vm_create(config);
    os_release(config);

    if (result != HV_SUCCESS) {
        if (result == HV_DENIED) {
            LOG_WARNING(Core_ARM,
                        "hv_vm_create denied: the binary is not signed with the "
                        "com.apple.security.hypervisor entitlement");
        } else if (result == HV_UNSUPPORTED) {
            LOG_WARNING(Core_ARM, "Hypervisor.framework is not supported on this machine");
        } else {
            LOG_WARNING(Core_ARM, "hv_vm_create failed: {:#x}", static_cast<u32>(result));
        }
        return;
    }

    created = true;
    LOG_INFO(Core_ARM, "Hypervisor VM created, max IPA size {} bits", max_ipa_size_bits);

    kernel_page_table = std::make_unique<HvPageTable>(*this);
    kernel_region_ready = kernel_page_table->IsValid() && InitializeKernelRegion();
    if (!kernel_region_ready) {
        LOG_ERROR(Core_ARM, "Could not set up the hypervisor kernel region");
    }

    {
        std::scoped_lock lock{table_ipa_mutex};
        table_ipa_after_kernel = next_table_ipa;
    }
}

bool HvMachine::InitializeKernelRegion() {
    kernel_region_host = mmap(nullptr, HOST_PAGE_SIZE, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (kernel_region_host == MAP_FAILED) {
        kernel_region_host = nullptr;
        return false;
    }
    std::memset(kernel_region_host, 0, HOST_PAGE_SIZE);

    auto* code = static_cast<u32*>(kernel_region_host);
    for (u32 entry = 0; entry < VECTOR_ENTRY_COUNT; ++entry) {
        u32* slot = code + (entry * VECTOR_ENTRY_SIZE) / sizeof(u32);
        slot[0] = MakeHvc(entry);
        slot[1] = INSTRUCTION_BRK_0;
    }

    u32* trampoline = code + EXCEPTION_TRAMPOLINE_OFFSET / sizeof(u32);
    trampoline[0] = INSTRUCTION_CLREX;
    trampoline[1] = INSTRUCTION_ERET;
    trampoline[2] = INSTRUCTION_BRK_0;

    u32* flush = code + FLUSH_TRAMPOLINE_OFFSET / sizeof(u32);
    flush[0] = INSTRUCTION_TLBI_VMALLE1IS;
    flush[1] = INSTRUCTION_IC_IALLU;
    flush[2] = INSTRUCTION_DSB_ISH;
    flush[3] = INSTRUCTION_ISB;
    flush[4] = INSTRUCTION_CLREX;
    flush[5] = INSTRUCTION_ERET;
    flush[6] = INSTRUCTION_BRK_0;

    if (!MapMemory(kernel_region_host, KERNEL_REGION_IPA, HOST_PAGE_SIZE)) {
        return false;
    }

    constexpr u64 VA_MASK = (1ull << HvPageTable::VA_BITS) - 1;
    return kernel_page_table->Map(KERNEL_REGION_VA & VA_MASK, KERNEL_REGION_IPA, HOST_PAGE_SIZE,
                                  HvPermission::KernelReadExecute);
}

HvMachine::~HvMachine() {
    process_page_tables.clear();

    kernel_page_table.reset();
    if (kernel_region_host != nullptr) {
        UnmapMemory(KERNEL_REGION_IPA, HOST_PAGE_SIZE);
        munmap(kernel_region_host, HOST_PAGE_SIZE);
    }
    if (created) {
        hv_vm_destroy();
    }
}

u64 HvMachine::AllocateTableIpa(u64 size) {
    std::scoped_lock lock{table_ipa_mutex};
    if (next_table_ipa == 0) {
        next_table_ipa = PAGE_TABLE_IPA_BASE;
    }

    const u64 ipa_limit = IpaLimit();
    if (ipa_limit != 0 && (next_table_ipa >= ipa_limit || size > ipa_limit - next_table_ipa)) {
        LOG_ERROR(Core_ARM,
                  "Page-table IPA arena exhausted: {:#x} + {:#x} exceeds the VM's {} bit IPA "
                  "limit of {:#x}",
                  next_table_ipa, size, max_ipa_size_bits, ipa_limit);
        return 0;
    }
    const u64 ipa = next_table_ipa;
    next_table_ipa += size;
    return ipa;
}

bool HvMachine::EnsureDramMapped(void* backing, u64 base, u64 size) {
    if (base + size > KERNEL_REGION_IPA) {
        LOG_ERROR(Core_ARM,
                  "Guest DRAM {:#x}..{:#x} ({} MiB) runs into the kernel region at {:#x}; "
                  "reduce the memory layout setting",
                  base, base + size, size / (1024 * 1024), KERNEL_REGION_IPA);
        return false;
    }

    std::scoped_lock lock{process_mutex};

    if (dram_mapped && backing == dram_backing && base == dram_base && size == dram_size) {
        return true;
    }

    if (dram_mapped) {
        LOG_INFO(Core_ARM, "Guest DRAM backing changed; releasing the previous session's mapping");
        UnmapMemory(dram_base, dram_size);
        dram_mapped = false;
        DetachAllPageTables();
        process_page_tables.clear();
        {
            std::scoped_lock table_lock{table_ipa_mutex};
            next_table_ipa = table_ipa_after_kernel;
        }
    }

    if (!MapMemory(backing, base, size)) {
        return false;
    }
    dram_mapped = true;
    dram_backing = backing;
    dram_base = base;
    dram_size = size;
    LOG_INFO(Core_ARM, "Mapped {} MiB of guest DRAM at IPA {:#x}", size / (1024 * 1024), base);
    return true;
}

HvPageTable* HvMachine::ProcessPageTable(const void* process) {
    std::scoped_lock lock{process_mutex};
    auto it = process_page_tables.find(process);
    if (it == process_page_tables.end()) {
        auto table = std::make_unique<HvPageTable>(*this);
        if (!table->IsValid()) {
            return nullptr;
        }
        it = process_page_tables.emplace(process, std::move(table)).first;
    }
    return it->second.get();
}

HvMachine* HvMachine::Instance() {
    std::scoped_lock lock{instance_mutex};
    if (!instance_attempted) {
        instance_attempted = true;
        instance.reset(new HvMachine());
        if (!instance->created) {
            instance.reset();
        }
    }
    return instance ? &*instance : nullptr;
}

bool HvMachine::MapMemory(void* host_addr, u64 ipa, u64 size) {
    std::scoped_lock lock{map_mutex};
    const hv_return_t result =
        hv_vm_map(host_addr, ipa, size,
                  static_cast<hv_memory_flags_t>(HV_MEMORY_READ | HV_MEMORY_WRITE |
                                                 HV_MEMORY_EXEC));
    if (result != HV_SUCCESS) {
        LOG_ERROR(Core_ARM, "hv_vm_map({}, {:#x}, {:#x}) failed: {:#x}", host_addr, ipa, size,
                  static_cast<u32>(result));
        return false;
    }
    return true;
}

void HvMachine::UnmapMemory(u64 ipa, u64 size) {
    std::scoped_lock lock{map_mutex};
    const hv_return_t result = hv_vm_unmap(ipa, size);
    if (result != HV_SUCCESS) {
        LOG_ERROR(Core_ARM, "hv_vm_unmap({:#x}, {:#x}) failed: {:#x}", ipa, size,
                  static_cast<u32>(result));
    }
}

} // namespace Core
