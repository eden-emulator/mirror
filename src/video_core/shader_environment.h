// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <filesystem>
#include <iosfwd>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <type_traits>
#include "common/container/unordered_map.h"
#include <vector>

#include "common/common_types.h"
#include "common/polyfill_thread.h"
#include "common/unique_function.h"
#include "shader_recompiler/environment.h"
#include "shader_recompiler/frontend/ampere/trap_guards.h"
#include "video_core/engines/maxwell_3d.h"

namespace Tegra {
class Memorymanager;
}

namespace VideoCommon {
std::optional<u32> ReadNativeGuardConstant(Tegra::Engines::KeplerCompute& compute,
                                          Tegra::MemoryManager& memory, u32 bank, u32 offset);


class GenericEnvironment : public Shader::Environment {
public:
    explicit GenericEnvironment() = default;
    explicit GenericEnvironment(Tegra::MemoryManager& gpu_memory_, GPUVAddr program_base_,
                                u32 start_address_);

    ~GenericEnvironment() override;

    [[nodiscard]] u32 TextureBoundBuffer() const final;

    [[nodiscard]] u32 LocalMemorySize() const final;

    [[nodiscard]] u32 SharedMemorySize() const final;

    [[nodiscard]] std::array<u32, 3> WorkgroupSize() const final;

    [[nodiscard]] u64 ReadInstruction(u32 address) final;

    [[nodiscard]] bool HasNativeRegisterCopies() const override;

    [[nodiscard]] bool IsCbufStorageBufferPointer(u32 index, u32 offset) override;

    [[nodiscard]] u32 TranslateProgramAddress(u32 address) const override;

    [[nodiscard]] std::optional<u64> Analyze();

    void SetCachedSize(size_t size_bytes);

    [[nodiscard]] size_t CachedSizeWords() const noexcept;

    [[nodiscard]] size_t CachedSizeBytes() const noexcept;

    [[nodiscard]] size_t ReadSizeBytes() const noexcept;

    [[nodiscard]] size_t SourceSizeBytes() const noexcept {
        return native_source_size;
    }

    [[nodiscard]] const std::vector<Shader::Ampere::Instruction>& NativeTrapPrefix() const noexcept {
        return native_trap_prefix;
    }

    [[nodiscard]] bool CanBeSerialized() const noexcept;

    [[nodiscard]] u64 CalculateHash() const;

    void Dump(u64 pipeline_hash, u64 shader_hash) override;

    void Serialize(std::ofstream& file) const;

    bool HasHLEMacroState() const override {
        return has_hle_engine_state;
    }

    [[nodiscard]] bool IsNativeAmpere() const override {
        return !ampere_program.empty();
    }

    [[nodiscard]] std::span<const Shader::Ampere::Instruction> NativeProgram() const override {
        return ampere_program;
    }

    [[nodiscard]] std::span<const u32> NativeIndirectTargets() const override {
        return ampere_indirect_targets;
    }

    [[nodiscard]] std::span<const u32> NativeIndirectTargetsAt(u32 index) const override {
        for (const auto& [branch, targets] : native_indirect_tables) {
            if (branch == index) return targets;
        }
        return NativeIndirectTargets();
    }

    [[nodiscard]] u32 NativeSourceBase() const override {
        return native_source_header_size;
    }

protected:
    std::optional<u64> TryFindSize();

    void InitializeNativeCode(u32 source_header_size = 128,
                              const Shader::Ampere::TrapGuardReader& guard_reader = {});
    size_t native_source_size{};
    u32 native_source_header_size{};
    std::vector<Shader::Ampere::Instruction> native_trap_prefix;
    // Set only when the program is translated by the native Ampere frontend.
    std::vector<Shader::Ampere::Instruction> ampere_program;
    std::vector<u32> ampere_indirect_targets; // BRX destinations, as instruction indices
    // One table per BRX found by the capture: BRX instruction index -> destination instruction indices.
    std::vector<std::pair<u32, std::vector<u32>>> native_indirect_tables;
    size_t stage_index{}; // graphics stage of this environment (0 for compute), used for logging

    Tegra::Texture::TICEntry ReadTextureInfo(GPUVAddr tic_addr, u32 tic_limit,
                                             bool via_header_index, u32 raw);

    Tegra::MemoryManager* gpu_memory{};
    GPUVAddr program_base{};

    std::vector<u64> code;
    ::Common::unordered_map<u32, Shader::TextureType> texture_types;
    ::Common::unordered_map<u32, Shader::TexturePixelFormat> texture_pixel_formats;
    ::Common::unordered_map<u64, u32> cbuf_values;
    ::Common::unordered_map<u64, u8> storage_pointer_types;
    ::Common::unordered_map<u64, Shader::ReplaceConstant> cbuf_replacements;

    u32 local_memory_size{};
    u32 texture_bound{};
    u32 shared_memory_size{};
    std::array<u32, 3> workgroup_size{};

    u32 read_lowest = (std::numeric_limits<u32>::max)();
    u32 read_highest = 0;

    u32 cached_lowest = (std::numeric_limits<u32>::max)();
    u32 cached_highest = 0;
    u32 initial_offset = 0;

    u32 viewport_transform_state = 1;

    bool has_unbound_instructions = false;
    bool has_hle_engine_state = false;
};

class GraphicsEnvironment final : public GenericEnvironment {
public:
    explicit GraphicsEnvironment() = default;
    explicit GraphicsEnvironment(Tegra::Engines::Maxwell3D& maxwell3d_,
                                 Tegra::MemoryManager& gpu_memory_,
                                 Tegra::Engines::Maxwell3D::Regs::ShaderType program,
                                 GPUVAddr program_base_, u32 start_address_);

    ~GraphicsEnvironment() override = default;

    u32 ReadCbufValue(u32 cbuf_index, u32 cbuf_offset) override;

    Shader::TextureType ReadTextureType(u32 handle) override;

    Shader::TexturePixelFormat ReadTexturePixelFormat(u32 handle) override;

    bool IsTexturePixelFormatInteger(u32 handle) override;

    u32 ReadViewportTransformState() override;

    std::optional<Shader::ReplaceConstant> GetReplaceConstBuffer(u32 bank, u32 offset) override;

private:
    Tegra::Engines::Maxwell3D* maxwell3d{};
};

class ComputeEnvironment final : public GenericEnvironment {
public:
    explicit ComputeEnvironment() = default;
    explicit ComputeEnvironment(Tegra::Engines::KeplerCompute& kepler_compute_,
                                Tegra::MemoryManager& gpu_memory_, GPUVAddr program_base_,
                                u32 start_address_);

    ~ComputeEnvironment() override = default;

    u32 ReadCbufValue(u32 cbuf_index, u32 cbuf_offset) override;

    Shader::TextureType ReadTextureType(u32 handle) override;

    Shader::TexturePixelFormat ReadTexturePixelFormat(u32 handle) override;

    bool IsTexturePixelFormatInteger(u32 handle) override;

    u32 ReadViewportTransformState() override;

    std::optional<Shader::ReplaceConstant> GetReplaceConstBuffer(
        [[maybe_unused]] u32 bank, [[maybe_unused]] u32 offset) override {
        return std::nullopt;
    }

private:
    Tegra::Engines::KeplerCompute* kepler_compute{};
};

class FileEnvironment final : public Shader::Environment {
public:
    FileEnvironment() = default;
    ~FileEnvironment() override = default;

    FileEnvironment& operator=(FileEnvironment&&) noexcept = default;
    FileEnvironment(FileEnvironment&&) noexcept = default;

    FileEnvironment& operator=(const FileEnvironment&) = delete;
    FileEnvironment(const FileEnvironment&) = delete;

    void Deserialize(std::ifstream& file);

    [[nodiscard]] u64 ReadInstruction(u32 address) override;

    [[nodiscard]] bool HasNativeRegisterCopies() const override;

    [[nodiscard]] bool IsCbufStorageBufferPointer(u32 index, u32 offset) override;

    [[nodiscard]] u32 TranslateProgramAddress(u32 address) const override;

    [[nodiscard]] bool IsNativeAmpere() const override {
        return !ampere_program.empty();
    }

    [[nodiscard]] std::span<const Shader::Ampere::Instruction> NativeProgram() const override {
        return ampere_program;
    }

    [[nodiscard]] std::span<const u32> NativeIndirectTargets() const override {
        return ampere_indirect_targets;
    }

    [[nodiscard]] std::span<const u32> NativeIndirectTargetsAt(u32 index) const override {
        for (const auto& [branch, targets] : native_indirect_tables) {
            if (branch == index) return targets;
        }
        return NativeIndirectTargets();
    }

    [[nodiscard]] u32 NativeSourceBase() const override {
        return native_source_header_size;
    }

    /// True when a cached native program was written by a different translator version; it must not be used.
    [[nodiscard]] bool IsStale() const noexcept {
        return stale;
    }

    [[nodiscard]] u32 ReadCbufValue(u32 cbuf_index, u32 cbuf_offset) override;

    [[nodiscard]] Shader::TextureType ReadTextureType(u32 handle) override;

    [[nodiscard]] Shader::TexturePixelFormat ReadTexturePixelFormat(u32 handle) override;

    [[nodiscard]] bool IsTexturePixelFormatInteger(u32 handle) override;

    [[nodiscard]] u32 ReadViewportTransformState() override;

    [[nodiscard]] u32 LocalMemorySize() const override;

    [[nodiscard]] u32 SharedMemorySize() const override;

    [[nodiscard]] u32 TextureBoundBuffer() const override;

    [[nodiscard]] std::array<u32, 3> WorkgroupSize() const override;

    [[nodiscard]] std::optional<Shader::ReplaceConstant> GetReplaceConstBuffer(u32 bank,
                                                                               u32 offset) override;

    [[nodiscard]] bool HasHLEMacroState() const override {
        return cbuf_replacements.size() != 0;
    }

    void Dump(u64 pipeline_hash, u64 shader_hash) override;

private:
    std::vector<u64> code;
    ::Common::unordered_map<u32, Shader::TextureType> texture_types;
    ::Common::unordered_map<u32, Shader::TexturePixelFormat> texture_pixel_formats;
    ::Common::unordered_map<u64, u32> cbuf_values;
    ::Common::unordered_map<u64, u8> storage_pointer_types;
    ::Common::unordered_map<u64, Shader::ReplaceConstant> cbuf_replacements;
    std::array<u32, 3> workgroup_size{};
    u32 local_memory_size{};
    u32 shared_memory_size{};
    u32 texture_bound{};
    u32 read_lowest{};
    u32 read_highest{};
    u32 initial_offset{};
    u32 viewport_transform_state = 1;
    // Natively translated programs keep their Ampere instructions instead of lowered Maxwell code.
    std::vector<Shader::Ampere::Instruction> ampere_program;
    std::vector<u32> ampere_indirect_targets;
    std::vector<std::pair<u32, std::vector<u32>>> native_indirect_tables;
    u32 native_source_header_size{};
    bool stale{};
};

void SerializePipeline(std::span<const char> key, std::span<const GenericEnvironment* const> envs,
                       const std::filesystem::path& filename, u32 cache_version);

template <typename Key, typename Envs>
    requires std::is_trivially_copyable_v<Key>
        && std::has_unique_object_representations_v<Key>
void SerializePipeline(const Key& key, const Envs& envs, const std::filesystem::path& filename, u32 cache_version) {
    SerializePipeline(std::span(reinterpret_cast<const char*>(&key), sizeof(key)), std::span(envs.data(), envs.size()), filename, cache_version);
}

void LoadPipelines(std::stop_token stop_loading, const std::filesystem::path& filename, u32 expected_cache_version,
    Common::UniqueFunction<void, std::ifstream&, FileEnvironment> load_compute,
    Common::UniqueFunction<void, std::ifstream&, std::vector<FileEnvironment>> load_graphics);

} // namespace VideoCommon
