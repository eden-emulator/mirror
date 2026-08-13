// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <dxbc_modinfo.h>
#include <dxbc_module.h>
#include <dxbc_reader.h>
#include <thirdparty/spirv.hpp>

#include "video_core/frame_gen/lsfg_translate.h"

namespace VideoCore::FrameGen {

namespace {

constexpr u32 DECORATION_LITERAL_WORD = 3;

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

} // Anonymous namespace

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
