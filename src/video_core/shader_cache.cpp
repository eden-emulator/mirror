// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <atomic>
#include <algorithm>
#include <array>
#include <vector>
#include <boost/container/small_vector.hpp>

#include "common/assert.h"
#include "common/cityhash.h"
#include "shader_recompiler/frontend/maxwell/control_flow.h"
#include "shader_recompiler/exception.h"
#include "shader_recompiler/object_pool.h"
#include "video_core/control/channel_state.h"
#include "video_core/dirty_flags.h"
#include "video_core/engines/kepler_compute.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/host1x/gpu_device_memory_manager.h"
#include "video_core/memory_manager.h"
#include "video_core/shader_cache.h"
#include "video_core/shader_environment.h"
#include "video_core/texture_cache/util.h"

namespace VideoCommon {

void ShaderCache::InvalidateRegion(VAddr addr, size_t size) {
    std::scoped_lock lock{invalidation_mutex};
    DropRejections(addr, size);
    InvalidatePagesInRegion(addr, size);
    RemovePendingShaders();
}

void ShaderCache::OnCacheInvalidation(VAddr addr, size_t size) {
    std::scoped_lock lock{invalidation_mutex};
    DropRejections(addr, size);
    InvalidatePagesInRegion(addr, size);
}

void ShaderCache::SyncGuestHost() {
    std::scoped_lock lock{invalidation_mutex};
    RemovePendingShaders();
}

ShaderCache::ShaderCache(Tegra::MaxwellDeviceMemoryManager& device_memory_)
    : device_memory{device_memory_} {}

bool ShaderCache::RefreshStages(std::array<u64, 6>& unique_hashes) try {
    auto& dirty{maxwell3d->dirty.flags};
    if (!dirty[VideoCommon::Dirty::Shaders]) {
        return last_shaders_valid;
    }
    dirty[VideoCommon::Dirty::Shaders] = false;

    const GPUVAddr base_addr{maxwell3d->regs.program_region.Address()};
    const bool native = maxwell3d->GetNativeMacroEngine().native_turing;
    for (size_t index = 0; index < Tegra::Engines::Maxwell3D::Regs::MaxShaderProgram; ++index) {
        if (!maxwell3d->regs.IsShaderConfigEnabled(index)) {
            unique_hashes[index] = 0;
            continue;
        }
        const auto& shader_config{maxwell3d->regs.pipelines[index]};
        const auto program{static_cast<Tegra::Engines::Maxwell3D::Regs::ShaderType>(index)};
        if (program == Tegra::Engines::Maxwell3D::Regs::ShaderType::Pixel &&
            !maxwell3d->regs.rasterize_enable) {
            unique_hashes[index] = 0;
            continue;
        }
        const GPUVAddr shader_addr{native
            ? (static_cast<u64>(shader_config.reserved[0] & 0xff) << 32) | shader_config.reserved[1]
            : base_addr + shader_config.offset};
        const std::optional<VAddr> cpu_shader_addr{gpu_memory->GpuToCpuAddress(shader_addr)};
        if (!cpu_shader_addr) {
            LOG_ERROR(HW_GPU, "Invalid GPU address for shader {:#016x}", shader_addr);
            last_shaders_valid = false;
            return false;
        }
        const ShaderInfo* shader_info{TryGet(*cpu_shader_addr)};
        if (!shader_info) {
            if (native) {
                ThrowIfRejected(*cpu_shader_addr, shader_addr);
            }
            const u32 start_address{native ? 0 : shader_config.offset};
            try {
                GraphicsEnvironment env{*maxwell3d, *gpu_memory, program, native ? shader_addr : base_addr, start_address};
                shader_info = MakeShaderInfo(env, *cpu_shader_addr);
            } catch (const Shader::Exception& error) {
                if (native) {
                    RememberRejection(*cpu_shader_addr, shader_addr, error.what());
                }
                throw;
            }
        }
        shader_infos[index] = shader_info;
        unique_hashes[index] = shader_info->unique_hash;
    }
    last_shaders_valid = true;
    return true;
} catch (const Shader::Exception& error) {
    auto& native = maxwell3d->GetNativeMacroEngine();
    if (!native.native_turing) throw;
    native.native_execution_failed = true;
    last_shaders_valid = false;
    // Rate-limited: the first 32, then every 1024th with the running total.
    static std::atomic<u64> rejected_native_shaders{};
    const u64 count = ++rejected_native_shaders;
    if (count <= 32 || count % 1024 == 0) {
        LOG_ERROR(HW_GPU, "Native graphics shader rejected: {} (rejections so far: {})", error.what(), count);
    }
    return false;
}

const ShaderInfo* ShaderCache::ComputeShader() {
    const GPUVAddr program_base{kepler_compute->regs.code_loc.Address()};
    const auto& qmd{kepler_compute->launch_description};
    const GPUVAddr shader_addr{program_base + qmd.program_start};
    const std::optional<VAddr> cpu_shader_addr{gpu_memory->GpuToCpuAddress(shader_addr)};
    if (!cpu_shader_addr) {
        LOG_ERROR(HW_GPU, "Invalid GPU address for shader {:#016x}", shader_addr);
        return nullptr;
    }
    if (const ShaderInfo* const shader = TryGet(*cpu_shader_addr)) {
        if (!Shader::Ampere::VerifyTrapGuards(shader->native_trap_prefix, [&](u32 bank, u32 offset) {
                return ReadNativeGuardConstant(*kepler_compute, *gpu_memory, bank, offset);
            })) throw Shader::RuntimeError("Native compute trap guard failed for the current dispatch");
        return shader;
    }
    if (kepler_compute->IsNativeMode()) {
        ThrowIfRejected(*cpu_shader_addr, shader_addr);
    }
    try {
        ComputeEnvironment env{*kepler_compute, *gpu_memory, program_base, qmd.program_start};
        return MakeShaderInfo(env, *cpu_shader_addr);
    } catch (const Shader::Exception& error) {
        if (kepler_compute->IsNativeMode()) {
            RememberRejection(*cpu_shader_addr, shader_addr, error.what());
        }
        throw;
    }
}

void ShaderCache::GetGraphicsEnvironments(GraphicsEnvironments& result,
                                          const std::array<u64, NUM_PROGRAMS>& unique_hashes) {
    size_t env_index{};
    const GPUVAddr base_addr{maxwell3d->regs.program_region.Address()};
    const bool native = maxwell3d->GetNativeMacroEngine().native_turing;
    for (size_t index = 0; index < NUM_PROGRAMS; ++index) {
        if (unique_hashes[index] == 0) {
            continue;
        }
        const auto program{static_cast<Tegra::Engines::Maxwell3D::Regs::ShaderType>(index)};
        auto& env{result.envs[index]};
        const auto& config = maxwell3d->regs.pipelines[index];
        const u32 start_address{native ? 0 : config.offset};
        const GPUVAddr address = native
            ? (static_cast<u64>(config.reserved[0] & 0xff) << 32) | config.reserved[1]
            : base_addr;
        env = GraphicsEnvironment{*maxwell3d, *gpu_memory, program, address, start_address};
        env.SetCachedSize(shader_infos[index]->size_bytes);
        result.env_ptrs[env_index++] = &env;
    }
}

namespace {
constexpr size_t REJECT_PREFIX_BYTES = 512;

// Number of leading bytes of the shader that are mapped (0 when too little is readable to tell
// two shaders apart).
size_t MappedPrefix(const Tegra::MemoryManager& gpu_memory, GPUVAddr gpu_addr) {
    size_t size = REJECT_PREFIX_BYTES;
    while (size >= 16 && !gpu_memory.IsFullyMappedRange(gpu_addr, size)) {
        size /= 2;
    }
    return size >= 16 ? size : 0;
}

u64 PrefixHash(const Tegra::MemoryManager& gpu_memory, GPUVAddr gpu_addr, size_t size) {
    std::array<u8, REJECT_PREFIX_BYTES> bytes;
    gpu_memory.ReadBlockUnsafe(gpu_addr, bytes.data(), size);
    return Common::CityHash64(reinterpret_cast<const char*>(bytes.data()), size);
}
} // Anonymous namespace

void ShaderCache::ThrowIfRejected(VAddr cpu_addr, GPUVAddr gpu_addr) {
    std::string error;
    {
        std::scoped_lock lock{lookup_mutex};
        const auto it = rejected_shaders.find(cpu_addr);
        if (it == rejected_shaders.end()) {
            return;
        }
        const size_t size = MappedPrefix(*gpu_memory, gpu_addr);
        if (size == it->second.prefix_size && PrefixHash(*gpu_memory, gpu_addr, size) == it->second.prefix_hash) {
            error = it->second.error;
        } else {
            rejected_shaders.erase(it);
            return;
        }
    }
    throw Shader::RuntimeError("{}", error);
}

void ShaderCache::DropRejections(VAddr addr, size_t size) {
    std::scoped_lock lock{lookup_mutex};
    for (auto it = rejected_shaders.begin(); it != rejected_shaders.end();) {
        const auto current = it++;
        if (current->first < addr + size && addr < current->first + current->second.prefix_size) {
            rejected_shaders.erase(current);
        }
    }
}

void ShaderCache::RememberRejection(VAddr cpu_addr, GPUVAddr gpu_addr, const char* error) {
    const size_t size = MappedPrefix(*gpu_memory, gpu_addr);
    if (size == 0) {
        return;
    }
    const u64 hash = PrefixHash(*gpu_memory, gpu_addr, size);
    std::scoped_lock lock{lookup_mutex};
    rejected_shaders.insert_or_assign(cpu_addr, RejectedShader{hash, size, error});
}

ShaderInfo* ShaderCache::TryGet(VAddr addr) const {
    std::scoped_lock lock{lookup_mutex};

    const auto it = lookup_cache.find(addr);
    if (it == lookup_cache.end()) {
        return nullptr;
    }
    return it->second->data;
}

void ShaderCache::Register(std::unique_ptr<ShaderInfo> data, VAddr addr, size_t size) {
    std::scoped_lock lock{invalidation_mutex, lookup_mutex};

    const VAddr addr_end = addr + size;
    Entry* const entry = NewEntry(addr, addr_end, data.get());

    const u64 page_end = (addr_end + YUZU_PAGESIZE - 1) >> YUZU_PAGEBITS;
    for (u64 page = addr >> YUZU_PAGEBITS; page < page_end; ++page) {
        invalidation_cache[page].push_back(entry);
    }

    storage.push_back(std::move(data));

    device_memory.UpdatePagesCachedCount(addr, size, 1);
}

void ShaderCache::InvalidatePagesInRegion(VAddr addr, size_t size) {
    const VAddr addr_end = addr + size;
    const u64 page_end = (addr_end + YUZU_PAGESIZE - 1) >> YUZU_PAGEBITS;
    for (u64 page = addr >> YUZU_PAGEBITS; page < page_end; ++page) {
        auto it = invalidation_cache.find(page);
        if (it == invalidation_cache.end()) {
            continue;
        }
        InvalidatePageEntries(it->second, addr, addr_end);
    }
}

void ShaderCache::RemovePendingShaders() {
    if (marked_for_removal.empty()) {
        return;
    }
    // Remove duplicates
    std::ranges::sort(marked_for_removal);
    marked_for_removal.erase(std::unique(marked_for_removal.begin(), marked_for_removal.end()),
                             marked_for_removal.end());
    // Linear growth anyways - maybe consider static_vector instead?
    boost::container::small_vector<ShaderInfo*, 16> removed_shaders;
    std::scoped_lock lock{lookup_mutex};
    for (Entry* const entry : marked_for_removal) {
        removed_shaders.push_back(entry->data);
        auto const it = lookup_cache.find(entry->addr_start);
        ASSERT(it != lookup_cache.end());
        lookup_cache.erase(it);
    }
    marked_for_removal.clear();

    if (!removed_shaders.empty()) {
        // Remove the given shaders from the cache
        std::erase_if(storage, [&removed_shaders](const std::unique_ptr<ShaderInfo>& shader) {
            return std::ranges::find(removed_shaders, shader.get()) != removed_shaders.end();
        });
    }
}

void ShaderCache::InvalidatePageEntries(std::vector<Entry*>& entries, VAddr addr, VAddr addr_end) {
    size_t index = 0;
    while (index < entries.size()) {
        Entry* const entry = entries[index];
        if (!entry->Overlaps(addr, addr_end)) {
            ++index;
            continue;
        }

        UnmarkMemory(entry);
        RemoveEntryFromInvalidationCache(entry);
        marked_for_removal.push_back(entry);
    }
}

void ShaderCache::RemoveEntryFromInvalidationCache(const Entry* entry) {
    const u64 page_end = (entry->addr_end + YUZU_PAGESIZE - 1) >> YUZU_PAGEBITS;
    for (u64 page = entry->addr_start >> YUZU_PAGEBITS; page < page_end; ++page) {
        const auto entries_it = invalidation_cache.find(page);
        ASSERT(entries_it != invalidation_cache.end());
        std::vector<Entry*>& entries = entries_it->second;

        const auto entry_it = std::ranges::find(entries, entry);
        ASSERT(entry_it != entries.end());
        entries.erase(entry_it);
    }
}

void ShaderCache::UnmarkMemory(Entry* entry) {
    if (!entry->is_memory_marked) {
        return;
    }
    entry->is_memory_marked = false;

    const VAddr addr = entry->addr_start;
    const size_t size = entry->addr_end - addr;
    device_memory.UpdatePagesCachedCount(addr, size, -1);
}

ShaderCache::Entry* ShaderCache::NewEntry(VAddr addr, VAddr addr_end, ShaderInfo* data) {
    auto entry = std::make_unique<Entry>(Entry{addr, addr_end, data});
    Entry* const entry_pointer = entry.get();

    lookup_cache.emplace(addr, std::move(entry));
    return entry_pointer;
}

const ShaderInfo* ShaderCache::MakeShaderInfo(GenericEnvironment& env, VAddr cpu_addr) {
    auto info = std::make_unique<ShaderInfo>();
    if (const std::optional<u64> cached_hash{env.Analyze()}) {
        info->unique_hash = *cached_hash;
        info->size_bytes = env.CachedSizeBytes();
    } else {
        // Slow path, not really hit on commercial games
        // Build a control flow graph to get the real shader size
        Shader::ObjectPool<Shader::Maxwell::Flow::Block> flow_block;
        Shader::Maxwell::Flow::CFG cfg{env, flow_block, env.StartAddress()};
        info->unique_hash = env.CalculateHash();
        info->size_bytes = env.ReadSizeBytes();
    }
    info->native_trap_prefix = env.NativeTrapPrefix();
    const size_t size_bytes{env.SourceSizeBytes() ? env.SourceSizeBytes() : info->size_bytes};
    const ShaderInfo* const result{info.get()};
    Register(std::move(info), cpu_addr, size_bytes);
    return result;
}

} // namespace VideoCommon
