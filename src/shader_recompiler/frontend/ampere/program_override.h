// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstring>
#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"

namespace Shader::Ampere {

inline u64 NativeProgramKey(std::span<const Instruction> source) {
    u64 hash = 14695981039346656037ULL;
    for (const auto& instruction : source) {
        for (u32 word : instruction.words) {
            for (u32 byte = 0; byte < 4; ++byte) hash = (hash ^ ((word >> (byte * 8)) & 255)) * 1099511628211ULL;
        }
    }
    return hash;
}

// Private iteration format: version, lengths, texture bank, exact original
// instructions, then the newly generated canonical instructions. Source bytes
// are compared directly; the filename hash is only an index.
inline std::vector<u8> EncodeProgramOverride(std::span<const Instruction> source,
                                            const LoweredProgram& program) {
    if (!program || program.source_size != source.size_bytes() || program.source_instruction_offsets.size() != source.size()) return {};
    const std::array<u32, 6> header{0x4e454445, 0x3230564f,
        static_cast<u32>(source.size_bytes()), static_cast<u32>(program.instructions.size()),
        program.required_texture_buffer, static_cast<u32>(program.source_instruction_offsets.size())};
    std::vector<u8> bytes(sizeof(header) + source.size_bytes() + program.instructions.size() * 8 + program.source_instruction_offsets.size() * 4);
    std::memcpy(bytes.data(), header.data(), sizeof(header));
    std::memcpy(bytes.data() + sizeof(header), source.data(), source.size_bytes());
    std::memcpy(bytes.data() + sizeof(header) + source.size_bytes(), program.instructions.data(), program.instructions.size() * 8);
    std::memcpy(bytes.data() + sizeof(header) + source.size_bytes() + program.instructions.size() * 8,
                program.source_instruction_offsets.data(), program.source_instruction_offsets.size() * 4);
    return bytes;
}

inline u32 ProgramOverrideSourceSize(std::span<const u8> bytes) {
    std::array<u32, 6> header{};
    if (bytes.size() < sizeof(header)) return 0;
    std::memcpy(header.data(), bytes.data(), sizeof(header));
    const bool mapped = header[1] == 0x3230564f;
    if (header[0] != 0x4e454445 || (!mapped && header[1] != 0x3130564f) ||
        (mapped ? header[5] != header[2] / 16 : header[5] != 0) ||
        header[2] == 0 || header[2] % sizeof(Instruction) || header[2] > 1048576 ||
        header[3] == 0 || header[3] > 1048576 || (header[4] != ~0U && header[4] >= 18)) return 0;
    if (bytes.size() != sizeof(header) + size_t{header[2]} + size_t{header[3]} * 8 + size_t{header[5]} * 4) return 0;
    return header[2];
}

inline LoweredProgram DecodeProgramOverride(std::span<const u8> bytes,
                                            std::span<const Instruction> source) {
    LoweredProgram result;
    const auto fail = [&result](std::string_view message) {
        result.instructions.clear();
        result.source_instruction_offsets.clear();
        result.error = message;
        return result;
    };
    std::array<u32, 6> header{};
    if (!ProgramOverrideSourceSize(bytes)) return fail("invalid native override header or length");
    std::memcpy(header.data(), bytes.data(), sizeof(header));
    if (header[2] != source.size_bytes()) return fail("native override source length mismatch");
    if (std::memcmp(bytes.data() + sizeof(header), source.data(), source.size_bytes()))
        return fail("native override source mismatch");
    result.instructions.resize(header[3]);
    std::memcpy(result.instructions.data(), bytes.data() + sizeof(header) + source.size_bytes(), header[3] * 8);
    if (header[5]) {
        result.source_instruction_offsets.resize(header[5]);
        std::memcpy(result.source_instruction_offsets.data(),
                    bytes.data() + sizeof(header) + source.size_bytes() + size_t{header[3]} * 8,
                    size_t{header[5]} * 4);
        u32 previous = 0;
        for (u32 offset : result.source_instruction_offsets) {
            if (offset >= result.instructions.size() || offset < previous)
                return fail("native override address map mismatch");
            previous = offset;
        }
    }
    result.source_size = header[2];
    result.required_texture_buffer = header[4];
    return result;
}

} // namespace Shader::Ampere
