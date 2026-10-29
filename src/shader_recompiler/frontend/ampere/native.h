// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// Encoding reference: Mesa NAK sm70_encode.rs (MIT), Copyright 2022 Collabora, Ltd.
#pragma once

#include <span>
#include <string>
#include <array>
#include <string_view>
#include <vector>

#include "common/common_types.h"
#include "shader_recompiler/exception.h"
#include "shader_recompiler/frontend/ampere/instruction.h"

namespace Shader {
class Environment;
namespace IR {
class Block;
}
} // namespace Shader

namespace Shader::Ampere {

// Raised for anything the native frontend does not model exactly. Selecting the native frontend
// per program catches it before any IR is produced and falls back to the Maxwell lowering.
class UnsupportedError : public Shader::Exception {
public:
    UnsupportedError(u32 instruction_index, std::string_view reason)
        : Shader::Exception{std::string{reason}}, index{instruction_index} {}
    u32 index;
};

enum class FrontendMode {
    Lower,
    Ampere,
    Auto,
    Compare,
};

// EDEN_NATIVE_FRONTEND=lower|ampere|auto|compare, default lower.
[[nodiscard]] FrontendMode SelectedFrontendMode();

// Salt mixed into the pipeline cache identity of native-translated programs. Bump it whenever the
// native translation changes so cached results of an older translator are never reused.
inline constexpr u64 NATIVE_TRANSLATOR_SALT = 0x4f554e4345a00001ULL;

// Maxwell Location offset of native instruction `index` (same mapping the lowering uses).
[[nodiscard]] constexpr u32 LocationOf(u32 index) {
    return 0x50U + index * 8 + ((index + 1) / 3) * 8;
}
// Inverse of LocationOf; returns ~0 when the offset is not an instruction slot.
[[nodiscard]] constexpr u32 IndexOf(u32 location) {
    if (location < 0x50) return ~0U;
    const u32 rel = location - 0x50;
    const u32 group = rel / 32;
    switch (rel % 32) {
    case 0: return group * 3;
    case 8: return group * 3 + 1;
    case 24: return group * 3 + 2;
    default: return ~0U;
    }
}

// Uniform registers occupy the top of the IR register file: UR0..UR62 are slots 192..254 and the
// uniform predicates UP0..UP6 are the bits of slot 191. Guest GPRs in that range are rejected.
inline constexpr u32 UNIFORM_PREDICATE_SLOT = 191;
inline constexpr u32 UNIFORM_REGISTER_BASE = 192;

// Where the uniform file lives in the IR register file. Programs whose GPRs stay below slot 191 use
// the fixed layout above; programs that use higher GPRs get the uniform registers packed right above
// the highest GPR they touch.
struct UniformLayout {
    u32 gpr_limit{UNIFORM_PREDICATE_SLOT};         // GPRs must stay below this slot (fixed layout)
    u32 predicate_slot{UNIFORM_PREDICATE_SLOT};    // UP0..UP6 are the bits of this slot
    u32 register_base{UNIFORM_REGISTER_BASE};      // UR0..UR62 are the slots from here (fixed layout)
    // Programs that use almost every GPR get their uniform registers placed in GPR numbers the
    // program never touches: slots[k] is the register holding URk.
    bool scattered{};
    std::array<u8, 64> slots{};
    std::array<bool, 256> reserved{}; // register numbers taken by the uniform file
};

// Layout chosen for `program` by its validation (the default layout when none was recorded).
[[nodiscard]] UniformLayout LayoutOf(std::span<const Instruction> program);

// Environment state a native program requires, discovered while validating it.
struct ProgramInfo {
    u32 texture_buffer{~0U}; // constant buffer holding the texture descriptors (bound-texture buffer)
    // Filled by a probe pass: the highest GPR and uniform register touched, and whether any uniform
    // predicate is.
    u32 max_gpr{0};
    std::array<bool, 256> used_gprs{}; // GPRs the program touches
    u32 max_ur{0};
    bool uses_any_ur{false};
    bool uses_up{false};
};

// Dry run of the whole native pipeline for one program: control flow, per-instruction decode and
// translation into scratch IR. Throws UnsupportedError when the program cannot be handled natively.
[[nodiscard]] ProgramInfo ValidateProgram(Environment& env, std::span<const Instruction> program);

// Diagnostic: reasons for every instruction the native frontend cannot translate (flow errors end the
// list). Empty when ValidateProgram would succeed.
[[nodiscard]] std::vector<std::string> CollectUnsupported(Environment& env,
                                                          std::span<const Instruction> program);

// Translate native instructions [begin, end) (Maxwell Location offsets) into `block`.
void Translate(Environment& env, IR::Block* block, u32 location_begin, u32 location_end);

} // namespace Shader::Ampere
