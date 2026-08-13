// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstring>
#include <map>
#include <tuple>

#include <dxbc_modinfo.h>
#include <dxbc_module.h>
#include <dxbc_reader.h>
#include <thirdparty/spirv.hpp>

#include "video_core/frame_gen/lsfg_translate.h"

namespace VideoCore::FrameGen {

namespace {

constexpr u32 DECORATION_LITERAL_WORD = 3;
constexpr size_t SPIRV_HEADER_WORDS = 5;

void RenumberBindings(dxvk::SpirvCodeBuffer& code) {
    std::vector<u32> literal_offsets;
    for (const auto instruction : code) {
        if (instruction.opCode() == spv::OpFunction) {
            break;
        }
        if (instruction.opCode() == spv::OpDecorate &&
            instruction.arg(2) == spv::DecorationBinding) {
            literal_offsets.push_back(instruction.offset() + DECORATION_LITERAL_WORD);
        }
    }

    for (size_t i = 0; i < literal_offsets.size(); ++i) {
        code.data()[literal_offsets[i]] = static_cast<u32>(i);
    }
}

void RenumberBindingsInOrder(std::vector<u32>& words) {
    struct Slot {
        u32 set;
        u32 binding;
        size_t literal_offset;
    };

    std::map<u32, u32> sets;
    std::vector<Slot> slots;

    size_t offset = SPIRV_HEADER_WORDS;
    while (offset + 1 <= words.size()) {
        const u32 length = words[offset] >> spv::WordCountShift;
        const u32 opcode = words[offset] & spv::OpCodeMask;
        if (length == 0 || offset + length > words.size()) {
            return;
        }
        if (opcode == spv::OpFunction) {
            break;
        }
        if (opcode == spv::OpDecorate && length >= 4) {
            if (words[offset + 2] == spv::DecorationDescriptorSet) {
                sets[words[offset + 1]] = words[offset + 3];
            } else if (words[offset + 2] == spv::DecorationBinding) {
                slots.push_back(Slot{0, words[offset + 3], offset + DECORATION_LITERAL_WORD});
            }
        }
        offset += length;
    }

    for (Slot& slot : slots) {
        const auto hit = sets.find(words[slot.literal_offset - 2]);
        slot.set = hit == sets.end() ? 0 : hit->second;
    }

    std::ranges::stable_sort(slots, [](const Slot& lhs, const Slot& rhs) {
        return std::tie(lhs.set, lhs.binding) < std::tie(rhs.set, rhs.binding);
    });

    for (size_t i = 0; i < slots.size(); ++i) {
        words[slots[i].literal_offset] = static_cast<u32>(i);
    }
}

} // Anonymous namespace

bool IsSpirvModule(std::span<const u8> blob) {
    if (blob.size() < SPIRV_HEADER_WORDS * sizeof(u32) || blob.size() % sizeof(u32) != 0) {
        return false;
    }
    u32 magic{};
    std::memcpy(&magic, blob.data(), sizeof(magic));
    return magic == spv::MagicNumber;
}

std::vector<u32> AdoptSpirvModule(std::span<const u8> blob) {
    if (!IsSpirvModule(blob)) {
        return {};
    }

    std::vector<u32> words(blob.size() / sizeof(u32));
    std::memcpy(words.data(), blob.data(), blob.size());

    RenumberBindingsInOrder(words);
    return words;
}

std::vector<u32> TranslateComputeShader(std::span<const u8> dxbc) {
    if (dxbc.empty()) {
        return {};
    }

    try {
        dxvk::DxbcReader reader{reinterpret_cast<const char*>(dxbc.data()), dxbc.size()};
        dxvk::DxbcModule module{reader};

        const dxvk::DxbcModuleInfo module_info{};
        dxvk::SpirvCodeBuffer code = module.compile(module_info, "CS");
        if (code.dwords() == 0) {
            return {};
        }

        RenumberBindings(code);
        return std::vector<u32>{code.data(), code.data() + code.dwords()};
    } catch (...) {
        return {};
    }
}

} // namespace VideoCore::FrameGen
