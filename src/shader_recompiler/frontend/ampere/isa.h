// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// Encoding reference: Mesa NAK sm70_encode.rs (MIT), Copyright 2022 Collabora, Ltd.
#pragma once

#include <array>
#include <bit>
#include <string>

#include "common/common_types.h"
#include "shader_recompiler/frontend/ampere/ampere_isa_table.h"
#include "shader_recompiler/frontend/ampere/native.h"

namespace Shader::Ampere {

// 12-bit opcode indexed decode table built at compile time from the generated rows.
struct IsaTable {
    std::array<const IsaRow*, 4096> rows{};
    constexpr IsaTable() {
        for (const auto& entry : ISA_ROWS) rows[entry.op12] = &entry.row;
    }
};
inline constexpr IsaTable ISA_TABLE{};

[[nodiscard]] inline const IsaRow* LookupRow(u32 op12) {
    return ISA_TABLE.rows[op12 & 0xfff];
}

// Index of the first bit set in `instruction` that the row does not explain, or -1.
// Scheduling bits (105..127), guard and opcode are part of every row.
[[nodiscard]] inline int FirstUnknownBit(const Instruction& instruction, const IsaRow& row) {
    for (u32 word = 0; word < 4; ++word) {
        const u32 stray = instruction.words[word] & ~row.known[word];
        if (stray) return static_cast<int>(word * 32 + std::countr_zero(stray));
    }
    return -1;
}

// Instruction fields shared by every ALU-style encoding.
struct Fields {
    static constexpr u32 FORM_LO = 9;
};

} // namespace Shader::Ampere
