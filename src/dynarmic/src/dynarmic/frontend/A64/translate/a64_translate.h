// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * SPDX-License-Identifier: 0BSD
 */
#pragma once

#include <functional>
#include <optional>

#include "common/common_types.h"
#include "dynarmic/interface/A64/config.h"

namespace Dynarmic {

namespace IR {
class Block;
}  // namespace IR

namespace A64 {

class LocationDescriptor;

/**
 * This function translates instructions in memory into our intermediate representation.
 * @param descriptor The starting location of the basic block. Includes information like PC, FPCR state, &c.
 * @param memory_read_code The function we should use to read emulated memory.
 * @param conf Configures how certain instructions are translated.
 * @return A translated basic block in the intermediate representation.
 */
void Translate(IR::Block& block, LocationDescriptor descriptor, A64::UserConfig const& conf);

/**
 * This function translates a single provided instruction into our intermediate representation.
 * @param block The block to append the IR for the instruction to.
 * @param descriptor The location of the instruction. Includes information like PC, FPCR state, &c.
 * @param instruction The instruction to translate.
 * @return The translated instruction translated to the intermediate representation.
 */
bool TranslateSingleInstruction(IR::Block& block, LocationDescriptor descriptor, u32 instruction);

}  // namespace A64
}  // namespace Dynarmic
