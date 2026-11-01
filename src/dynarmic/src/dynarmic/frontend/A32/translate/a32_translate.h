// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * SPDX-License-Identifier: 0BSD
 */
#pragma once

#include "common/common_types.h"

#include "dynarmic/interface/A32/arch_version.h"
#include "dynarmic/interface/A32/config.h"

namespace Dynarmic::IR {
class Block;
}  // namespace Dynarmic::IR

namespace Dynarmic::A32 {

class LocationDescriptor;
struct TranslateCallbacks;

/**
 * This function translates instructions in memory into our intermediate representation.
 * @param descriptor The starting location of the basic block. Includes information like PC, Thumb state, &c.
 * @param tcb The callbacks we should use to read emulated memory.
 * @param conf Configures how certain instructions are translated.
 * @return A translated basic block in the intermediate representation.
 */
void Translate(IR::Block& block, LocationDescriptor descriptor, const A32::UserConfig& conf);

/**
 * This function translates a single provided instruction into our intermediate representation.
 * @param block The block to append the IR for the instruction to.
 * @param descriptor The location of the instruction. Includes information like PC, Thumb state, &c.
 * @param instruction The instruction to translate.
 * @return The translated instruction translated to the intermediate representation.
 */
bool TranslateSingleInstruction(IR::Block& block, LocationDescriptor descriptor, u32 instruction);

}  // namespace Dynarmic::A32
