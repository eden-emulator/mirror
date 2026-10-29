// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// Encoding reference: Mesa NAK sm70_encode.rs (MIT), Copyright 2022 Collabora, Ltd.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>

#include <fmt/format.h>

#include "shader_recompiler/environment.h"
#include "shader_recompiler/frontend/ampere/classify.h"
#include "shader_recompiler/frontend/ampere/flow.h"
#include "shader_recompiler/frontend/ampere/native.h"
#include "shader_recompiler/frontend/ampere/visitor.h"
#include "shader_recompiler/frontend/ir/basic_block.h"
#include "shader_recompiler/object_pool.h"

namespace Shader::Ampere {

FrontendMode SelectedFrontendMode() {
    static const FrontendMode mode = [] {
        const char* const value = std::getenv("EDEN_NATIVE_FRONTEND");
        if (!value) return FrontendMode::Lower;
        if (!std::strcmp(value, "ampere")) return FrontendMode::Ampere;
        if (!std::strcmp(value, "auto")) return FrontendMode::Auto;
        if (!std::strcmp(value, "compare")) return FrontendMode::Compare;
        return FrontendMode::Lower;
    }();
    return mode;
}

namespace {
// Layouts other than the default are remembered by the content of the program, so that translating
// its blocks later finds the layout its validation chose.
u64 ProgramHash(std::span<const Instruction> program) {
    u64 hash = 0xcbf29ce484222325ULL;
    for (const Instruction& insn : program) {
        for (const u32 word : insn.words) {
            hash = (hash ^ word) * 0x100000001b3ULL;
        }
    }
    return hash ^ (u64{program.size()} << 40);
}

std::mutex layout_mutex;
std::unordered_map<u64, UniformLayout>& Layouts() {
    static std::unordered_map<u64, UniformLayout> layouts;
    return layouts;
}

void RunValidation(Environment& env, std::span<const Instruction> program, const FlowAnalysis& analysis,
                   ProgramInfo& info, const UniformLayout& layout, bool probe) {
    ObjectPool<IR::Inst> inst_pool;
    ObjectPool<IR::Block> block_pool;
    IR::Block* const scratch = block_pool.Create(inst_pool);
    for (u32 index = 0; index < program.size(); ++index) {
        if (!analysis.reachable[index]) continue;
        // Branches are consumed by the control flow builder, not translated.
        if (program[index].Bits(0, 12) == 0x947) continue;
        Visitor{env, *scratch, program[index], index, &info, layout, probe}.Run();
    }
}
} // Anonymous namespace

UniformLayout LayoutOf(std::span<const Instruction> program) {
    std::scoped_lock lock{layout_mutex};
    const auto& layouts = Layouts();
    if (layouts.empty()) {
        return {};
    }
    const auto it = layouts.find(ProgramHash(program));
    return it == layouts.end() ? UniformLayout{} : it->second;
}

namespace {
ProgramInfo ValidateProgramImpl(Environment& env, std::span<const Instruction> program);
} // Anonymous namespace

namespace {
// The verdict of validating a program depends only on its instructions and its branch tables, so it
// is remembered: the same shader is validated again for every pipeline it appears in.
struct Verdict {
    bool accepted{};
    ProgramInfo info;
    u32 index{};
    std::string reason;
};

std::mutex verdict_mutex;
std::unordered_map<u64, Verdict>& Verdicts() {
    static std::unordered_map<u64, Verdict> verdicts;
    return verdicts;
}

u64 VerdictKey(Environment& env, std::span<const Instruction> program) {
    u64 hash = ProgramHash(program);
    const auto mix = [&hash](u32 value) { hash = (hash ^ value) * 0x100000001b3ULL; };
    for (const u32 target : env.NativeIndirectTargets()) mix(target);
    for (u32 index = 0; index < program.size(); ++index) {
        const u32 op = program[index].Bits(0, 12);
        if (op == 0x949 || op == 0x958) {
            mix(index);
            for (const u32 target : env.NativeIndirectTargetsAt(index)) mix(target);
        }
    }
    return hash;
}
} // Anonymous namespace

// Rejections carry the four words of the instruction they stopped at, so logs show what to decode.
ProgramInfo ValidateProgram(Environment& env, std::span<const Instruction> program) {
    const u64 key = VerdictKey(env, program);
    {
        std::scoped_lock lock{verdict_mutex};
        if (const auto it = Verdicts().find(key); it != Verdicts().end()) {
            if (it->second.accepted) {
                return it->second.info;
            }
            throw UnsupportedError(it->second.index, it->second.reason);
        }
    }
    Verdict verdict;
    try {
        verdict.info = ValidateProgramImpl(env, program);
        verdict.accepted = true;
    } catch (const UnsupportedError& error) {
        verdict.index = error.index;
        verdict.reason = error.what();
        if (error.index < program.size()) {
            const auto& words = program[error.index].words;
            verdict.reason = fmt::format("{} [{:08x} {:08x} {:08x} {:08x}]", error.what(), words[0], words[1],
                                         words[2], words[3]);
        }
    }
    {
        std::scoped_lock lock{verdict_mutex};
        if (Verdicts().size() < 65536) {
            Verdicts().emplace(key, verdict);
        }
    }
    if (verdict.accepted) {
        return verdict.info;
    }
    throw UnsupportedError(verdict.index, verdict.reason);
}

namespace {
ProgramInfo ValidateProgramImpl(Environment& env, std::span<const Instruction> program) {
    ProgramInfo info;
    const IndirectTargetProvider per_branch = [&env](u32 index) { return env.NativeIndirectTargetsAt(index); };
    const FlowAnalysis analysis = AnalyzeFlow(program, env.NativeIndirectTargets(), &per_branch);
    try {
        RunValidation(env, program, analysis, info, {}, false);
        return info;
    } catch (const UnsupportedError& error) {
        if (std::string_view{error.what()}.find("reserved for the uniform file") == std::string_view::npos) {
            throw;
        }
        // A GPR collides with the fixed uniform slots. Find out which registers the program really
        // touches and pack the uniform file just above them.
        ProgramInfo probe_info;
        RunValidation(env, program, analysis, probe_info, {}, true);
        UniformLayout layout;
        layout.gpr_limit = probe_info.max_gpr + 1;
        layout.predicate_slot = layout.gpr_limit;
        layout.register_base = layout.gpr_limit + (probe_info.uses_up ? 1 : 0);
        const u32 slots = probe_info.uses_any_ur ? probe_info.max_ur + 1 : 0;
        if (std::getenv("EDEN_NATIVE_LAYOUT_DEBUG")) {
            std::fprintf(stderr, "native layout: max_gpr=%u max_ur=%u uses_up=%d -> base=%u slots=%u\n",
                         probe_info.max_gpr, probe_info.max_ur, probe_info.uses_up ? 1 : 0,
                         layout.register_base, slots);
        }
        if (layout.register_base + slots > 255 || (probe_info.uses_up && layout.predicate_slot >= 255)) {
            // Not enough room above the highest GPR: keep every GPR where it is and place the uniform
            // file in the extra SSA variables above RZ.
            layout = UniformLayout{};
            layout.gpr_limit = 255;
            layout.predicate_slot = 256;
            layout.register_base = 257;
            if (std::getenv("EDEN_NATIVE_LAYOUT_DEBUG")) {
                std::fprintf(stderr, "native layout: extended file, %u slots\n", slots);
            }
        }
        {
            std::scoped_lock lock{layout_mutex};
            Layouts()[ProgramHash(program)] = layout;
        }
        ProgramInfo real_info;
        RunValidation(env, program, analysis, real_info, layout, false);
        return real_info;
    }
}

} // Anonymous namespace

std::vector<std::string> CollectUnsupported(Environment& env, std::span<const Instruction> program) {
    std::vector<std::string> reasons;
    FlowAnalysis analysis;
    try {
        const IndirectTargetProvider per_branch = [&env](u32 index) { return env.NativeIndirectTargetsAt(index); };
        analysis = AnalyzeFlow(program, env.NativeIndirectTargets(), &per_branch);
    } catch (const UnsupportedError& e) {
        reasons.emplace_back(e.what());
        return reasons;
    }
    ObjectPool<IR::Inst> inst_pool;
    ObjectPool<IR::Block> block_pool;
    IR::Block* const scratch = block_pool.Create(inst_pool);
    ProgramInfo info;
    const UniformLayout layout = LayoutOf(program);
    for (u32 index = 0; index < program.size(); ++index) {
        if (!analysis.reachable[index] || program[index].Bits(0, 12) == 0x947) continue;
        try {
            Visitor{env, *scratch, program[index], index, &info, layout}.Run();
        } catch (const UnsupportedError& e) {
            reasons.emplace_back(e.what());
        }
    }
    return reasons;
}

void Translate(Environment& env, IR::Block* block, u32 location_begin, u32 location_end) {
    if (location_begin == location_end) return;
    const std::span<const Instruction> program = env.NativeProgram();
    const u32 begin = IndexOf(location_begin);
    const u32 end = IndexOf(location_end);
    if (begin == ~0U || end == ~0U || end > program.size() || begin > end) {
        throw UnsupportedError(0, "native block range");
    }
    const UniformLayout layout = LayoutOf(program);
    for (u32 index = begin; index < end; ++index) {
        Visitor{env, *block, program[index], index, nullptr, layout}.Run();
    }
}

} // namespace Shader::Ampere
