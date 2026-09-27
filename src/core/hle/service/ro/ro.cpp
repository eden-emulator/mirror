// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <openssl/err.h>
#include <openssl/evp.h>

#include "core/hle/kernel/k_process.h"
#include "core/hle/service/ro/ro.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"
#ifdef HAS_NCE
#include "core/arm/nce/patcher.h"
#include "core/hle/kernel/k_shared_memory.h"
#endif
#include "core/hle/service/cmif_serialization.h"

namespace Service::RO {

constexpr Result ResultOutOfAddressSpace{ErrorModule::RO, 2};
constexpr Result ResultAlreadyLoaded{ErrorModule::RO, 3};
constexpr Result ResultInvalidNro{ErrorModule::RO, 4};
constexpr Result ResultInvalidNrr{ErrorModule::RO, 6};
constexpr Result ResultTooManyNro{ErrorModule::RO, 7};
constexpr Result ResultTooManyNrr{ErrorModule::RO, 8};
constexpr Result ResultNotAuthorized{ErrorModule::RO, 9};
constexpr Result ResultInvalidNrrKind{ErrorModule::RO, 10};
constexpr Result ResultInternalError{ErrorModule::RO, 1023};
constexpr Result ResultInvalidAddress{ErrorModule::RO, 1025};
constexpr Result ResultInvalidSize{ErrorModule::RO, 1026};
constexpr Result ResultNotLoaded{ErrorModule::RO, 1028};
constexpr Result ResultNotRegistered{ErrorModule::RO, 1029};
constexpr Result ResultInvalidSession{ErrorModule::RO, 1030};
constexpr Result ResultInvalidProcess{ErrorModule::RO, 1031};

namespace {

struct ProcessMemoryRegion {
    u64 address;
    u64 size;
};

size_t GetTotalProcessMemoryRegionSize(const ProcessMemoryRegion* regions, size_t num_regions) {
    size_t total = 0;

    for (size_t i = 0; i < num_regions; ++i) {
        total += regions[i].size;
    }

    return total;
}

size_t SetupNroProcessMemoryRegions(ProcessMemoryRegion* regions, u64 nro_heap_address,
                                    u64 nro_heap_size, u64 bss_heap_address, u64 bss_heap_size) {
    // Reset region count.
    size_t num_regions = 0;

    // We always want a region for the nro.
    regions[num_regions++] = {nro_heap_address, nro_heap_size};

    // If we have bss, create a region for bss.
    if (bss_heap_size > 0) {
        regions[num_regions++] = {bss_heap_address, bss_heap_size};
    }

    return num_regions;
}

Result SetProcessMemoryPermission(Kernel::KProcess* process, u64 address, u64 size,
                                  Kernel::Svc::MemoryPermission permission) {
    auto& page_table = process->GetPageTable();

    // Set permission.
    R_RETURN(page_table.SetProcessMemoryPermission(address, size, permission));
}

Result UnmapProcessCodeMemory(Kernel::KProcess* process, u64 process_code_address,
                              const ProcessMemoryRegion* regions, size_t num_regions) {
    // Get the total process memory region size.
    const size_t total_size = GetTotalProcessMemoryRegionSize(regions, num_regions);

    auto& page_table = process->GetPageTable();

    // Unmap each region in order.
    size_t cur_offset = total_size;
    for (size_t i = 0; i < num_regions; ++i) {
        // We want to unmap in reverse order.
        const auto& cur_region = regions[num_regions - 1 - i];

        // Subtract to update the current offset.
        cur_offset -= cur_region.size;

        // Unmap.
        R_TRY(page_table.UnmapCodeMemory(process_code_address + cur_offset, cur_region.address,
                                         cur_region.size));
    }

    R_SUCCEED();
}

Result EnsureGuardPages(Kernel::KProcessPageTable& page_table, u64 map_address, u64 map_size) {
    Kernel::KMemoryInfo memory_info;
    Kernel::Svc::PageInfo page_info;

    // Ensure page before mapping is unmapped.
    R_TRY(page_table.QueryInfo(std::addressof(memory_info), std::addressof(page_info),
                               map_address - 1));
    R_UNLESS(memory_info.GetSvcState() == Kernel::Svc::MemoryState::Free,
             Kernel::ResultInvalidState);

    // Ensure page after mapping is unmapped.
    R_TRY(page_table.QueryInfo(std::addressof(memory_info), std::addressof(page_info),
                               map_address + map_size));
    R_UNLESS(memory_info.GetSvcState() == Kernel::Svc::MemoryState::Free,
             Kernel::ResultInvalidState);

    // Successfully verified guard pages.
    R_SUCCEED();
}

Result MapProcessCodeMemory(u64* out, Kernel::KProcess* process, const ProcessMemoryRegion* regions,
                            size_t num_regions, std::mt19937_64& generate_random) {
    auto& page_table = process->GetPageTable();
    const u64 alias_code_start =
        GetInteger(page_table.GetAliasCodeRegionStart()) / Kernel::PageSize;
    const u64 alias_code_size = page_table.GetAliasCodeRegionSize() / Kernel::PageSize;

    for (size_t trial = 0; trial < 64; trial++) {
        // Generate a new trial address.
        const u64 mapped_address =
            (alias_code_start + (generate_random() % alias_code_size)) * Kernel::PageSize;

        const auto MapRegions = [&] {
            // Map the regions in order.
            u64 mapped_size = 0;
            for (size_t i = 0; i < num_regions; ++i) {
                // If we fail, unmap up to where we've mapped.
                ON_RESULT_FAILURE {
                    R_ASSERT(UnmapProcessCodeMemory(process, mapped_address, regions, i));
                };

                // Map the current region.
                R_TRY(page_table.MapCodeMemory(mapped_address + mapped_size, regions[i].address,
                                               regions[i].size));

                mapped_size += regions[i].size;
            }

            // If we fail, unmap all mapped regions.
            ON_RESULT_FAILURE {
                R_ASSERT(UnmapProcessCodeMemory(process, mapped_address, regions, num_regions));
            };

            // Ensure guard pages.
            R_RETURN(EnsureGuardPages(page_table, mapped_address, mapped_size));
        };

        if (R_SUCCEEDED(MapRegions())) {
            // Set the output address.
            *out = mapped_address;
            R_SUCCEED();
        }
    }

    // We failed to map anything.
    R_THROW(RO::ResultOutOfAddressSpace);
}

Result MapNro(u64* out_base_address, Kernel::KProcess* process, u64 nro_heap_address,
              u64 nro_heap_size, u64 bss_heap_address, u64 bss_heap_size,
              std::mt19937_64& generate_random) {
    // Set up the process memory regions.
    std::array<ProcessMemoryRegion, 2> regions{};
    const size_t num_regions = SetupNroProcessMemoryRegions(
        regions.data(), nro_heap_address, nro_heap_size, bss_heap_address, bss_heap_size);

    // Re-map the nro/bss as code memory in the destination process.
    R_RETURN(MapProcessCodeMemory(out_base_address, process, regions.data(), num_regions,
                                  generate_random));
}

Result SetNroPerms(Kernel::KProcess* process, u64 base_address, u64 rx_size, u64 ro_size,
                   u64 rw_size) {
    const u64 rx_offset = 0;
    const u64 ro_offset = rx_offset + rx_size;
    const u64 rw_offset = ro_offset + ro_size;

    R_TRY(SetProcessMemoryPermission(process, base_address + rx_offset, rx_size,
                                     Kernel::Svc::MemoryPermission::ReadExecute));
    R_TRY(SetProcessMemoryPermission(process, base_address + ro_offset, ro_size,
                                     Kernel::Svc::MemoryPermission::Read));
    R_TRY(SetProcessMemoryPermission(process, base_address + rw_offset, rw_size,
                                     Kernel::Svc::MemoryPermission::ReadWrite));

    R_SUCCEED();
}

Result UnmapNro(Kernel::KProcess* process, u64 base_address, u64 nro_heap_address,
                u64 nro_heap_size, u64 bss_heap_address, u64 bss_heap_size) {
    // Set up the process memory regions.
    std::array<ProcessMemoryRegion, 2> regions{};
    const size_t num_regions = SetupNroProcessMemoryRegions(
        regions.data(), nro_heap_address, nro_heap_size, bss_heap_address, bss_heap_size);

    // Unmap the nro/bss.
    R_RETURN(UnmapProcessCodeMemory(process, base_address, regions.data(), num_regions));
}

} // namespace

enum class NrrKind : u8 {
    User = 0,
    JitPlugin = 1,
    Count,
};

static constexpr size_t ModuleIdSize = 0x20;
struct ModuleId {
    std::array<u8, ModuleIdSize> data;
};
static_assert(sizeof(ModuleId) == ModuleIdSize);

struct NrrCertification {
    static constexpr size_t RsaKeySize = 0x100;
    static constexpr size_t SignedSize = 0x120;

    u64 program_id_mask;
    u64 program_id_pattern;
    std::array<u8, 0x10> reserved_10;
    std::array<u8, RsaKeySize> modulus;
    std::array<u8, RsaKeySize> signature;
};
static_assert(sizeof(NrrCertification) == NrrCertification::RsaKeySize + NrrCertification::SignedSize);

class NrrHeader {
public:
    static constexpr u32 Magic = Common::MakeMagic('N', 'R', 'R', '0');

public:
    bool IsMagicValid() const {
        return m_magic == Magic;
    }

    bool IsProgramIdValid() const {
        return (m_program_id & m_certification.program_id_mask) ==
               m_certification.program_id_pattern;
    }

    NrrKind GetNrrKind() const {
        const NrrKind kind = static_cast<NrrKind>(m_nrr_kind);
        ASSERT(kind < NrrKind::Count);
        return kind;
    }

    u64 GetProgramId() const {
        return m_program_id;
    }

    u32 GetSize() const {
        return m_size;
    }

    u32 GetNumHashes() const {
        return m_num_hashes;
    }

    size_t GetHashesOffset() const {
        return m_hashes_offset;
    }

    u32 GetKeyGeneration() const {
        return m_key_generation;
    }

    const u8* GetCertificationSignature() const {
        return m_certification.signature.data();
    }

    const u8* GetCertificationSignedArea() const {
        return reinterpret_cast<const u8*>(std::addressof(m_certification));
    }

    const u8* GetCertificationModulus() const {
        return m_certification.modulus.data();
    }

    const u8* GetSignature() const {
        return m_signature.data();
    }

    size_t GetSignedAreaSize() const {
        return m_size - GetSignedAreaOffset();
    }

    static constexpr size_t GetSignedAreaOffset() {
        return offsetof(NrrHeader, m_program_id);
    }

private:
    u32 m_magic;
    u32 m_key_generation;
    INSERT_PADDING_BYTES_NOINIT(8);
    NrrCertification m_certification;
    std::array<u8, 0x100> m_signature;
    u64 m_program_id;
    u32 m_size;
    u8 m_nrr_kind; // 7.0.0+
    INSERT_PADDING_BYTES_NOINIT(3);
    u32 m_hashes_offset;
    u32 m_num_hashes;
    INSERT_PADDING_BYTES_NOINIT(8);
};
static_assert(sizeof(NrrHeader) == 0x350, "NrrHeader has wrong size");

static constexpr u32 NRO_HEADER_MAGIC = Common::MakeMagic('N', 'R', 'O', '0');
struct NroHeader {
    u32 m_entrypoint_insn;
    u32 m_mod_offset;
    INSERT_PADDING_BYTES_NOINIT(0x8);
    u32 m_magic;
    INSERT_PADDING_BYTES_NOINIT(0x4);
    u32 m_size;
    INSERT_PADDING_BYTES_NOINIT(0x4);
    u32 m_text_offset;
    u32 m_text_size;
    u32 m_ro_offset;
    u32 m_ro_size;
    u32 m_rw_offset;
    u32 m_rw_size;
    u32 m_bss_size;
    INSERT_PADDING_BYTES_NOINIT(0x4);
    ModuleId m_module_id;
    INSERT_PADDING_BYTES_NOINIT(0x20);
};
static_assert(sizeof(NroHeader) == 0x80, "NroHeader has wrong size");

namespace {

// Atmosphere defines as follows:
// Sessions = 0x03, NrrInfos = 0x40, NroInfos = 0x40
// This may not be enough for some mods (plugin.nro dependant games) like SSBU
// Suppose someone loads like 64 plugins of these, now what?
constexpr size_t MaxSessions = 0x03; // No change
constexpr size_t MaxNrrInfos = 0x100; // Up to 256 NRRs
constexpr size_t MaxNroInfos = 0x100; // Up to 256 NROs

constexpr u64 InvalidProcessId = 0xffffffffffffffffULL;
constexpr u64 InvalidContextId = 0xffffffffffffffffULL;

// Types.
using Sha256Hash = std::array<u8, 32>;

struct NroInfo {
    u64 base_address;
    u64 nro_heap_address;
    u64 nro_heap_size;
    u64 bss_heap_address;
    u64 bss_heap_size;
    u64 code_size;
    u64 rw_size;
    ModuleId module_id;
};

struct NrrInfo {
    u64 nrr_heap_address;
    u64 nrr_heap_size;

    // Verification.
    std::vector<Sha256Hash> hashes;
};

struct ProcessContext {
    constexpr ProcessContext() = default;

    void Initialize(Kernel::KernelCore& kernel, Kernel::KProcess* process, u64 process_id) {
        ASSERT(!m_in_use);

        m_nro_in_use = {};
        m_nrr_in_use = {};
        m_nro_infos = {};
        m_nrr_infos = {};

        m_process = process;
        m_process_id = process_id;
        m_in_use = true;

        if (m_process) {
            m_process->Open(kernel);
        }
    }

    void Finalize(Kernel::KernelCore& kernel) {
        ASSERT(m_in_use);

        if (m_process) {
            m_process->Close(kernel);
        }

        m_nro_in_use = {};
        m_nrr_in_use = {};
        m_nro_infos = {};
        m_nrr_infos = {};

        m_process = nullptr;
        m_process_id = InvalidProcessId;
        m_in_use = false;
    }

    Kernel::KProcess* GetProcess() const {
        return m_process;
    }

    u64 GetProcessId() const {
        return m_process_id;
    }

    bool IsFree() const {
        return !m_in_use;
    }

    u64 GetProgramId(Kernel::KProcess* other_process) const {
        // Automatically select a handle, allowing for override.
        if (other_process) {
            return other_process->GetProgramId();
        } else if (m_process) {
            return m_process->GetProgramId();
        } else {
            return 0;
        }
    }

    Result GetNrrInfoByAddress(NrrInfo** out, u64 nrr_heap_address) {
        for (size_t i = 0; i < MaxNrrInfos; i++) {
            if (m_nrr_in_use[i] && m_nrr_infos[i].nrr_heap_address == nrr_heap_address) {
                if (out != nullptr) {
                    *out = std::addressof(m_nrr_infos[i]);
                }
                R_SUCCEED();
            }
        }
        R_THROW(RO::ResultNotRegistered);
    }

    Result GetFreeNrrInfo(NrrInfo** out) {
        for (size_t i = 0; i < MaxNrrInfos; i++) {
            if (!m_nrr_in_use[i]) {
                if (out != nullptr) {
                    *out = std::addressof(m_nrr_infos[i]);
                }
                R_SUCCEED();
            }
        }
        R_THROW(RO::ResultTooManyNrr);
    }

    Result GetNroInfoByAddress(NroInfo** out, u64 nro_address) {
        for (size_t i = 0; i < MaxNroInfos; i++) {
            if (m_nro_in_use[i] && m_nro_infos[i].base_address == nro_address) {
                if (out != nullptr) {
                    *out = std::addressof(m_nro_infos[i]);
                }
                R_SUCCEED();
            }
        }
        R_THROW(RO::ResultNotLoaded);
    }

    Result GetNroInfoByModuleId(NroInfo** out, const ModuleId* module_id) {
        for (size_t i = 0; i < MaxNroInfos; i++) {
            if (m_nro_in_use[i] && std::memcmp(std::addressof(m_nro_infos[i].module_id), module_id,
                                               sizeof(*module_id)) == 0) {
                if (out != nullptr) {
                    *out = std::addressof(m_nro_infos[i]);
                }
                R_SUCCEED();
            }
        }
        R_THROW(RO::ResultNotLoaded);
    }

    Result GetFreeNroInfo(NroInfo** out) {
        for (size_t i = 0; i < MaxNroInfos; i++) {
            if (!m_nro_in_use[i]) {
                if (out != nullptr) {
                    *out = std::addressof(m_nro_infos[i]);
                }
                R_SUCCEED();
            }
        }
        R_THROW(RO::ResultTooManyNro);
    }

    Result ValidateHasNroHash(u64 base_address, const NroHeader* nro_header) const {
        // Calculate hash.
        Sha256Hash hash;
        {
            const u64 size = nro_header->m_size;
            std::vector<u8> nro_data(size);
            m_process->GetMemory().ReadBlock(base_address, nro_data.data(), size);
            u32 hash_len = 0;
            EVP_Digest(nro_data.data(), nro_data.size(), hash.data(), &hash_len, EVP_sha256(), nullptr);
        }

        for (size_t i = 0; i < MaxNrrInfos; i++) {
            // Ensure we only check NRRs that are used.
            if (!m_nrr_in_use[i]) {
                continue;
            }

            // Locate the hash within the hash list.
            const auto hash_it = std::ranges::find(m_nrr_infos[i].hashes, hash);
            if (hash_it == m_nrr_infos[i].hashes.end()) {
                continue;
            }

            // The hash is valid!
            R_SUCCEED();
        }

        R_THROW(RO::ResultNotAuthorized);
    }

    Result ValidateNro(ModuleId* out_module_id, u64* out_rx_size, u64* out_ro_size, u64* out_rw_size, u64 base_address, u64 expected_nro_size, u64 expected_bss_size) {
        // Ensure we have a process to work on.
        R_UNLESS(m_process != nullptr, RO::ResultInvalidProcess);

        // Read the NRO header.
        NroHeader header{};
        m_process->GetMemory().ReadBlock(base_address, std::addressof(header), sizeof(header));

        // Validate header.
        R_UNLESS(header.m_magic == NRO_HEADER_MAGIC, RO::ResultInvalidNro);

        // Read sizes from header.
        const u64 nro_size = header.m_size;
        const u64 text_ofs = header.m_text_offset;
        const u64 text_size = header.m_text_size;
        const u64 ro_ofs = header.m_ro_offset;
        const u64 ro_size = header.m_ro_size;
        const u64 rw_ofs = header.m_rw_offset;
        const u64 rw_size = header.m_rw_size;
        const u64 bss_size = header.m_bss_size;

        // Validate sizes meet expected.
        R_UNLESS(nro_size == expected_nro_size, RO::ResultInvalidNro);
        R_UNLESS(bss_size == expected_bss_size, RO::ResultInvalidNro);

        // Validate all sizes are aligned.
        R_UNLESS(Common::IsAligned(text_size, Core::Memory::YUZU_PAGESIZE), RO::ResultInvalidNro);
        R_UNLESS(Common::IsAligned(ro_size, Core::Memory::YUZU_PAGESIZE), RO::ResultInvalidNro);
        R_UNLESS(Common::IsAligned(rw_size, Core::Memory::YUZU_PAGESIZE), RO::ResultInvalidNro);
        R_UNLESS(Common::IsAligned(bss_size, Core::Memory::YUZU_PAGESIZE), RO::ResultInvalidNro);

        // Validate sections are in order.
        R_UNLESS(text_ofs <= ro_ofs, RO::ResultInvalidNro);
        R_UNLESS(ro_ofs <= rw_ofs, RO::ResultInvalidNro);

        // Validate sections are sequential and contiguous.
        R_UNLESS(text_ofs == 0, RO::ResultInvalidNro);
        R_UNLESS(text_ofs + text_size == ro_ofs, RO::ResultInvalidNro);
        R_UNLESS(ro_ofs + ro_size == rw_ofs, RO::ResultInvalidNro);
        R_UNLESS(rw_ofs + rw_size == nro_size, RO::ResultInvalidNro);

        // Verify NRO hash.
        R_TRY(this->ValidateHasNroHash(base_address, std::addressof(header)));

        // Check if NRO has already been loaded.
        const ModuleId* module_id = std::addressof(header.m_module_id);
        R_UNLESS(R_FAILED(this->GetNroInfoByModuleId(nullptr, module_id)), RO::ResultAlreadyLoaded);

        // Apply patches to NRO.
        // LocateAndApplyIpsPatchesToModule(module_id, static_cast<u8*>(mapped_memory), nro_size);

        // Copy to output.
        *out_module_id = *module_id;
        *out_rx_size = text_size;
        *out_ro_size = ro_size;
        *out_rw_size = rw_size;
        R_SUCCEED();
    }

    void SetNrrInfoInUse(const NrrInfo* info, bool in_use) {
        ASSERT(std::addressof(m_nrr_infos[0]) <= info &&
               info <= std::addressof(m_nrr_infos[MaxNrrInfos - 1]));
        const size_t index = info - std::addressof(m_nrr_infos[0]);
        m_nrr_in_use[index] = in_use;
    }

    void SetNroInfoInUse(const NroInfo* info, bool in_use) {
        ASSERT(std::addressof(m_nro_infos[0]) <= info &&
               info <= std::addressof(m_nro_infos[MaxNroInfos - 1]));
        const size_t index = info - std::addressof(m_nro_infos[0]);
        m_nro_in_use[index] = in_use;
    }

private:
    std::array<bool, MaxNroInfos> m_nro_in_use{};
    std::array<bool, MaxNrrInfos> m_nrr_in_use{};
    std::array<NroInfo, MaxNroInfos> m_nro_infos{};
    std::array<NrrInfo, MaxNrrInfos> m_nrr_infos{};
    Kernel::KProcess* m_process{};
    u64 m_process_id{InvalidProcessId};
    bool m_in_use{};
};

Result ValidateAddressAndNonZeroSize(u64 address, u64 size) {
    R_UNLESS(Common::IsAligned(address, Core::Memory::YUZU_PAGESIZE), RO::ResultInvalidAddress);
    R_UNLESS(size != 0, RO::ResultInvalidSize);
    R_UNLESS(Common::IsAligned(size, Core::Memory::YUZU_PAGESIZE), RO::ResultInvalidSize);
    R_UNLESS(address < address + size, RO::ResultInvalidSize);
    R_SUCCEED();
}

Result ValidateAddressAndSize(u64 address, u64 size) {
    R_UNLESS(Common::IsAligned(address, Core::Memory::YUZU_PAGESIZE), RO::ResultInvalidAddress);
    R_UNLESS(Common::IsAligned(size, Core::Memory::YUZU_PAGESIZE), RO::ResultInvalidSize);
    R_UNLESS(size == 0 || address < address + size, RO::ResultInvalidSize);
    R_SUCCEED();
}

class RoContext {
public:
    explicit RoContext() = default;

    Result RegisterProcess(Kernel::KernelCore& kernel, size_t* out_context_id, Kernel::KProcess* process, u64 process_id) {
        // Validate process id.
        R_UNLESS(process->GetProcessId() == process_id, RO::ResultInvalidProcess);

        // Check if a process context already exists.
        R_UNLESS(this->GetContextByProcessId(process_id) == nullptr, RO::ResultInvalidSession);

        // Allocate a context to manage the process handle.
        *out_context_id = this->AllocateContext(kernel, process, process_id);

        R_SUCCEED();
    }

    Result ValidateProcess(size_t context_id, u64 process_id) {
        const ProcessContext* ctx = this->GetContextById(context_id);
        R_UNLESS(ctx != nullptr, RO::ResultInvalidProcess);
        R_UNLESS(ctx->GetProcessId() == process_id, RO::ResultInvalidProcess);
        R_SUCCEED();
    }

    void UnregisterProcess(Kernel::KernelCore& kernel, size_t context_id) {
        this->FreeContext(kernel, context_id);
    }

    Result RegisterModuleInfo(size_t context_id, u64 nrr_address, u64 nrr_size, NrrKind nrr_kind,
                              bool enforce_nrr_kind) {
        // Get context.
        ProcessContext* context = this->GetContextById(context_id);
        ASSERT(context != nullptr);

        // Validate address/size.
        R_TRY(ValidateAddressAndNonZeroSize(nrr_address, nrr_size));

        // Check we have space for a new NRR.
        NrrInfo* nrr_info = nullptr;
        R_TRY(context->GetFreeNrrInfo(std::addressof(nrr_info)));

        // Ensure we have a valid process to read from.
        Kernel::KProcess* process = context->GetProcess();
        R_UNLESS(process != nullptr, RO::ResultInvalidProcess);

        // Read NRR.
        NrrHeader header{};
        process->GetMemory().ReadBlock(nrr_address, std::addressof(header), sizeof(header));

        // Set NRR info.
        context->SetNrrInfoInUse(nrr_info, true);
        nrr_info->nrr_heap_address = nrr_address;
        nrr_info->nrr_heap_size = nrr_size;

        // Read NRR hash list.
        nrr_info->hashes.resize(header.GetNumHashes());
        process->GetMemory().ReadBlock(nrr_address + header.GetHashesOffset(),
                                       nrr_info->hashes.data(),
                                       sizeof(Sha256Hash) * header.GetNumHashes());

        R_SUCCEED();
    }

    Result UnregisterModuleInfo(size_t context_id, u64 nrr_address) {
        // Get context.
        ProcessContext* context = this->GetContextById(context_id);
        ASSERT(context != nullptr);

        // Validate address.
        R_UNLESS(Common::IsAligned(nrr_address, Core::Memory::YUZU_PAGESIZE),
                 RO::ResultInvalidAddress);

        // Check the NRR is loaded.
        NrrInfo* nrr_info = nullptr;
        R_TRY(context->GetNrrInfoByAddress(std::addressof(nrr_info), nrr_address));

        // Nintendo does this unconditionally, whether or not the actual unmap succeeds.
        context->SetNrrInfoInUse(nrr_info, false);
        *nrr_info = {};

        R_SUCCEED();
    }

    Result MapManualLoadModuleMemory(Kernel::KernelCore& kernel, u64* out_address, size_t context_id, u64 nro_address,
                                     u64 nro_size, u64 bss_address, u64 bss_size) {
        // Get context.
        ProcessContext* context = this->GetContextById(context_id);
        ASSERT(context != nullptr);

        // Validate address/size.
        R_TRY(ValidateAddressAndNonZeroSize(nro_address, nro_size));
        R_TRY(ValidateAddressAndSize(bss_address, bss_size));

        const u64 total_size = nro_size + bss_size;
        R_UNLESS(total_size >= nro_size, RO::ResultInvalidSize);
        R_UNLESS(total_size >= bss_size, RO::ResultInvalidSize);

        // Check we have space for a new NRO.
        NroInfo* nro_info = nullptr;
        R_TRY(context->GetFreeNroInfo(std::addressof(nro_info)));
        nro_info->nro_heap_address = nro_address;
        nro_info->nro_heap_size = nro_size;
        nro_info->bss_heap_address = bss_address;
        nro_info->bss_heap_size = bss_size;

        // Map the NRO.
        R_TRY(MapNro(std::addressof(nro_info->base_address), context->GetProcess(), nro_address,
                     nro_size, bss_address, bss_size, generate_random));
        ON_RESULT_FAILURE {
            UnmapNro(context->GetProcess(), nro_info->base_address, nro_address, nro_size,
                     bss_address, bss_size);
        };

        // Validate the NRO (parsing region extents).
        u64 rx_size = 0, ro_size = 0, rw_size = 0;
        R_TRY(context->ValidateNro(std::addressof(nro_info->module_id), std::addressof(rx_size),
                                   std::addressof(ro_size), std::addressof(rw_size),
                                   nro_info->base_address, nro_size, bss_size));
#ifdef HAS_NCE
        if (Settings::IsNceEnabled()) {
            auto* process = context->GetProcess();
            auto& memory = process->GetMemory();

            std::vector<u8> image(total_size);
            memory.ReadBlock(nro_info->base_address, image.data(), rx_size);

            Kernel::CodeSet::Segment code{.size = static_cast<u32>(rx_size)};
            Core::NCE::Patcher patch;
            patch.PatchText(image, code);
            patch.RelocateAndCopy(nro_info->base_address, code, image, nullptr);

            const u64 patch_address = nro_info->base_address + total_size;
            const size_t patch_size = patch.GetSectionSize();
            constexpr auto permission = Kernel::Svc::MemoryPermission::ReadExecute;

            auto* patch_memory = Kernel::KSharedMemory::Create(kernel);
            R_TRY(patch_memory->Initialize(kernel, kernel.System().DeviceMemory(), process, permission, permission, patch_size));
            std::memcpy(patch_memory->GetPointer(), image.data() + total_size, patch_size);
            R_TRY(process->AddSharedMemory(kernel, patch_memory, patch_address, patch_size));
            R_TRY(patch_memory->Map(*process, patch_address, patch_size, permission));
            memory.WriteBlock(nro_info->base_address, image.data(), rx_size);
        }
#endif
        // Set NRO perms.
        R_TRY(SetNroPerms(context->GetProcess(), nro_info->base_address, rx_size, ro_size,
                          rw_size + bss_size));

        context->SetNroInfoInUse(nro_info, true);
        nro_info->code_size = rx_size + ro_size;
        nro_info->rw_size = rw_size;
        *out_address = nro_info->base_address;
        R_SUCCEED();
    }

    Result UnmapManualLoadModuleMemory(size_t context_id, u64 nro_address) {
        // Get context.
        ProcessContext* context = this->GetContextById(context_id);
        ASSERT(context != nullptr);

        // Validate address.
        R_UNLESS(Common::IsAligned(nro_address, Core::Memory::YUZU_PAGESIZE),
                 RO::ResultInvalidAddress);

        // Check the NRO is loaded.
        NroInfo* nro_info = nullptr;
        R_TRY(context->GetNroInfoByAddress(std::addressof(nro_info), nro_address));

        // Unmap.
        const NroInfo nro_backup = *nro_info;
        {
            // Nintendo does this unconditionally, whether or not the actual unmap succeeds.
            context->SetNroInfoInUse(nro_info, false);
            std::memset(nro_info, 0, sizeof(*nro_info));
        }
        R_RETURN(UnmapNro(context->GetProcess(), nro_backup.base_address,
                          nro_backup.nro_heap_address, nro_backup.code_size + nro_backup.rw_size,
                          nro_backup.bss_heap_address, nro_backup.bss_heap_size));
    }

private:
    std::array<ProcessContext, MaxSessions> process_contexts;
    std::mt19937_64 generate_random;

    // Context Helpers.
    ProcessContext* GetContextById(size_t context_id) {
        if (context_id == InvalidContextId) {
            return nullptr;
        }

        ASSERT(context_id < process_contexts.size());
        return std::addressof(process_contexts[context_id]);
    }

    ProcessContext* GetContextByProcessId(u64 process_id) {
        for (size_t i = 0; i < MaxSessions; i++) {
            if (process_contexts[i].GetProcessId() == process_id) {
                return std::addressof(process_contexts[i]);
            }
        }
        return nullptr;
    }

    size_t AllocateContext(Kernel::KernelCore& kernel, Kernel::KProcess* process, u64 process_id) {
        // Find a free process context.
        for (size_t i = 0; i < MaxSessions; i++) {
            ProcessContext* context = std::addressof(process_contexts[i]);

            if (context->IsFree()) {
                context->Initialize(kernel, process, process_id);
                return i;
            }
        }

        // Failure to find a free context is actually an abort condition.
        UNREACHABLE();
    }

    void FreeContext(Kernel::KernelCore& kernel, size_t context_id) {
        if (ProcessContext* context = GetContextById(context_id); context != nullptr) {
            context->Finalize(kernel);
        }
    }
};

class RoInterface : public ServiceFramework<RoInterface> {
public:
    explicit RoInterface(Core::System& system_, const char* name_, std::shared_ptr<RoContext> ro, NrrKind nrr_kind)
        : ServiceFramework{system_, name_}
        , m_ro(ro)
        , m_context_id(InvalidContextId)
        , m_nrr_kind(nrr_kind)
    {}

    ~RoInterface() {
        m_ro->UnregisterProcess(system.Kernel(), m_context_id);
    }

    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }

    Result MapManualLoadModuleMemory(Out<u64> out_load_address, ClientProcessId client_pid,
                                     u64 nro_address, u64 nro_size, u64 bss_address, u64 bss_size) {
        R_TRY(m_ro->ValidateProcess(m_context_id, *client_pid));
        R_RETURN(m_ro->MapManualLoadModuleMemory(system.Kernel(), out_load_address.Get(), m_context_id, nro_address,
                                                 nro_size, bss_address, bss_size));
    }

    Result UnmapManualLoadModuleMemory(ClientProcessId client_pid, u64 nro_address) {
        R_TRY(m_ro->ValidateProcess(m_context_id, *client_pid));
        R_RETURN(m_ro->UnmapManualLoadModuleMemory(m_context_id, nro_address));
    }

    Result RegisterModuleInfo(ClientProcessId client_pid, u64 nrr_address, u64 nrr_size) {
        R_TRY(m_ro->ValidateProcess(m_context_id, *client_pid));
        R_RETURN(
            m_ro->RegisterModuleInfo(m_context_id, nrr_address, nrr_size, NrrKind::User, true));
    }

    Result UnregisterModuleInfo(ClientProcessId client_pid, u64 nrr_address) {
        R_TRY(m_ro->ValidateProcess(m_context_id, *client_pid));
        R_RETURN(m_ro->UnregisterModuleInfo(m_context_id, nrr_address));
    }

    Result RegisterProcessHandle(ClientProcessId client_pid, InCopyHandle<Kernel::KProcess> process) {
        // Register the process.
        R_RETURN(m_ro->RegisterProcess(system.Kernel(), std::addressof(m_context_id), process.Get(), *client_pid));
    }

    Result RegisterProcessModuleInfo(ClientProcessId client_pid, u64 nrr_address, u64 nrr_size, InCopyHandle<Kernel::KProcess> process) {
        // Validate the process.
        R_TRY(m_ro->ValidateProcess(m_context_id, *client_pid));
        // Register the module.
        R_RETURN(m_ro->RegisterModuleInfo(m_context_id, nrr_address, nrr_size, m_nrr_kind, m_nrr_kind == NrrKind::JitPlugin));
    }

private:
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0,  C<&RoInterface::MapManualLoadModuleMemory>, "MapManualLoadModuleMemory"},
        FunctionInfo{1,  C<&RoInterface::UnmapManualLoadModuleMemory>, "UnmapManualLoadModuleMemory"},
        FunctionInfo{2,  C<&RoInterface::RegisterModuleInfo>, "RegisterModuleInfo"},
        FunctionInfo{3,  C<&RoInterface::UnregisterModuleInfo>, "UnregisterModuleInfo"},
        FunctionInfo{4,  C<&RoInterface::RegisterProcessHandle>, "RegisterProcessHandle"},
        FunctionInfo{10, C<&RoInterface::RegisterProcessModuleInfo>, "RegisterProcessModuleInfo"}
    );
    std::shared_ptr<RoContext> m_ro{};
    size_t m_context_id{};
    NrrKind m_nrr_kind{};
};

} // namespace


class IDebugMonitorInterface final : public ServiceFramework<IDebugMonitorInterface> {
public:
    explicit IDebugMonitorInterface(Core::System& system_)
        : ServiceFramework{system_, "ro:dmnt"}
    {}

    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, nullptr, "GetProcessModuleInfo" }
    );
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    auto ro = std::make_shared<RoContext>();

    const auto RoInterfaceFactoryForUser = [&, ro] {
        return std::make_shared<RoInterface>(system, "ldr:ro", ro, NrrKind::User);
    };

    server_manager->RegisterNamedService("ldr:ro", std::move(RoInterfaceFactoryForUser), 2);
    server_manager->RegisterNamedService("ro:1", std::make_shared<RoInterface>(system, "ro:1", ro, NrrKind::JitPlugin), 2);
    server_manager->RegisterNamedService("ro:dmnt", std::make_shared<IDebugMonitorInterface>(system), 2);
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::RO
