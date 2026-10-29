// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include "shader_recompiler/frontend/ir/ir_emitter.h"
#include "shader_recompiler/frontend/ir/program.h"
#include "shader_recompiler/frontend/ir/basic_block.h"
#include "shader_recompiler/shader_info.h"
#include "shader_recompiler/ir_opt/control_flow_reachability.h"

namespace Shader::Optimization {
struct BufferTextureAddress {
    u32 cbuf_index;
    u32 cbuf_offset;
    u32 offset;
    u32 stride;
    IR::U32 index;
};

namespace BufferTexture {
inline std::optional<u32> Constant(IR::Value value) {
    value = value.Resolve();
    if (!value.IsImmediate() || value.Type() != IR::Type::U32) return {};
    return value.U32();
}
inline bool Zero(IR::Value value, u32 depth = 0) {
    if (depth > 4) return false;
    if (const auto constant = Constant(value)) return *constant == 0;
    const auto* inst = value.TryInstRecursive();
    if (!inst) return false;
    if (inst->GetOpcode() == IR::Opcode::IMul32)
        return Zero(inst->Arg(0), depth + 1) || Zero(inst->Arg(1), depth + 1);
    return false;
}
inline IR::Value RemoveZeroAdd(IR::Value value) {
    value = value.Resolve();
    const auto* inst = value.TryInstRecursive();
    if (inst && inst->GetOpcode() == IR::Opcode::IAdd32) {
        if (Zero(inst->Arg(0))) return inst->Arg(1).Resolve();
        if (Zero(inst->Arg(1))) return inst->Arg(0).Resolve();
    }
    return value;
}
inline bool CbufLow(IR::Value value, u32 bank, u32 offset) {
    auto* inst = value.TryInstRecursive();
    if (!inst) return false;
    if (inst->GetOpcode() == IR::Opcode::CompositeExtractU32x2) {
        if (Constant(inst->Arg(1)) != 0U) return false;
        inst = inst->Arg(0).TryInstRecursive();
        if (!inst || inst->GetOpcode() != IR::Opcode::GetCbufU32x2) return false;
    } else if (inst->GetOpcode() != IR::Opcode::GetCbufU32) return false;
    return Constant(inst->Arg(0)) == bank && Constant(inst->Arg(1)) == offset;
}
inline bool Half(IR::Value value, u32 position, IR::Value& source) {
    const auto* inst = value.TryInstRecursive();
    if (!inst || inst->GetOpcode() != IR::Opcode::BitFieldUExtract ||
        Constant(inst->Arg(1)) != position || Constant(inst->Arg(2)) != 16U) return false;
    source = inst->Arg(0).Resolve();
    return true;
}
struct Product { IR::Value index; u32 stride; };
inline std::optional<Product> Multiply(IR::Value value) {
    const auto* inst = value.TryInstRecursive();
    if (!inst) return {};
    if (inst->GetOpcode() == IR::Opcode::IMul32) {
        if (const auto k = Constant(inst->Arg(1))) return Product{inst->Arg(0), *k};
        if (const auto k = Constant(inst->Arg(0))) return Product{inst->Arg(1), *k};
        return {};
    }
    if (inst->GetOpcode() != IR::Opcode::IAdd32) return {};
    for (u32 side = 0; side < 2; ++side) {
        const auto* shift = inst->Arg(side).TryInstRecursive();
        if (!shift || shift->GetOpcode() != IR::Opcode::ShiftLeftLogical32 ||
            Constant(shift->Arg(1)) != 16U) continue;
        const auto high = Multiply(RemoveZeroAdd(shift->Arg(0)));
        const auto low = Multiply(inst->Arg(1 - side));
        IR::Value high_source, low_source;
        if (high && low && high->stride == low->stride &&
            Half(high->index, 16, high_source) && Half(low->index, 0, low_source) &&
            high_source == low_source) return Product{high_source, high->stride};
    }
    return {};
}

inline std::optional<BufferTextureAddress> Track(IR::Value value, const Info& info) {
    value = value.Resolve();
    auto* load = value.TryInstRecursive();
    u32 component{};
    if (!load) return {};
    if (load->GetOpcode() == IR::Opcode::CompositeExtractU32x2 ||
        load->GetOpcode() == IR::Opcode::CompositeExtractU32x4) {
        const auto field = Constant(load->Arg(1));
        if (!field || *field > 3) return {};
        component = *field * 4;
        load = load->Arg(0).TryInstRecursive();
    }
    if (!load || (load->GetOpcode() != IR::Opcode::LoadStorage32 &&
                  load->GetOpcode() != IR::Opcode::LoadStorage64 &&
                  load->GetOpcode() != IR::Opcode::LoadStorage128)) return {};
    const auto binding = Constant(load->Arg(0));
    if (!binding || *binding >= info.storage_buffers_descriptors.size()) return {};
    const auto& buffer = info.storage_buffers_descriptors[*binding];
    if (buffer.count != 1 || buffer.is_written) return {};
    const auto* subtract = load->Arg(1).TryInstRecursive();
    if (!subtract || subtract->GetOpcode() != IR::Opcode::ISub32) return {};
    const auto* align = subtract->Arg(1).TryInstRecursive();
    if (!align || align->GetOpcode() != IR::Opcode::BitwiseAnd32 ||
        !CbufLow(align->Arg(0), buffer.cbuf_index, buffer.cbuf_offset)) return {};
    const auto mask = Constant(align->Arg(1));
    if (!mask || !*mask || ((~*mask + 1U) & ~*mask) != 0) return {};
    const auto* address = subtract->Arg(0).TryInstRecursive();
    if (!address || address->GetOpcode() != IR::Opcode::IAdd32) return {};
    IR::Value relative;
    if (CbufLow(address->Arg(0), buffer.cbuf_index, buffer.cbuf_offset)) relative = address->Arg(1);
    else if (CbufLow(address->Arg(1), buffer.cbuf_index, buffer.cbuf_offset)) relative = address->Arg(0);
    else return {};
    u64 offset = component;
    const auto* add = relative.TryInstRecursive();
    if (add && add->GetOpcode() == IR::Opcode::IAdd32) {
        if (const auto right_constant = Constant(add->Arg(1))) { offset += *right_constant; relative = add->Arg(0); }
        else if (const auto left_constant = Constant(add->Arg(0))) { offset += *left_constant; relative = add->Arg(1); }
    }
    const auto product = Multiply(relative);
    if (!product || !product->stride || (product->stride & 3) ||
        offset >= product->stride || product->stride - offset < 4) return {};
    return BufferTextureAddress{buffer.cbuf_index, buffer.cbuf_offset, static_cast<u32>(offset),
                                product->stride, IR::U32{product->index}};
}
inline bool SameArray(const BufferTextureAddress& a, const BufferTextureAddress& b) {
    return a.cbuf_index == b.cbuf_index && a.cbuf_offset == b.cbuf_offset &&
           a.offset == b.offset && a.stride == b.stride;
}

class Tracker {
public:
    explicit Tracker(IR::Program& program_) : program{program_} {}

    std::optional<BufferTextureAddress> Find(IR::Value value) {
        value = value.Resolve();
        auto* inst = value.TryInstRecursive();
        if (!inst) return {};
        if (const auto found = cache.find(inst); found != cache.end()) return found->second;
        if (active.size() >= 128 || !active.insert(inst).second) return {};
        auto result = Track(value, program.info);
        if (!result && (inst->GetOpcode() == IR::Opcode::BitCastU32F32 || inst->GetOpcode() == IR::Opcode::BitCastF32U32))
            result = Find(inst->Arg(0));
        if (!result && inst->GetOpcode() == IR::Opcode::SelectU32) {
            const auto a = Find(inst->Arg(1)), b = Find(inst->Arg(2));
            if (a && b && SameArray(*a, *b)) {
                if (auto* block = Owner(inst)) {
                    IR::IREmitter ir{*block, IR::Block::InstructionList::s_iterator_to(*inst)};
                    result = *a;
                    if (a->index.Resolve() != b->index.Resolve())
                        result->index = IR::U32{ir.Select(IR::U1{inst->Arg(0)}, a->index, b->index)};
                }
            }
        }
        if (!result && inst->GetOpcode() == IR::Opcode::Phi && inst->NumArgs()) {
            std::vector<BufferTextureAddress> inputs;
            for (size_t i = 0; i < inst->NumArgs(); ++i) {
                const auto input = Find(inst->Arg(i));
                if (!input || (!inputs.empty() && !SameArray(inputs.front(), *input))) break;
                inputs.push_back(*input);
            }
            if (inputs.size() == inst->NumArgs()) {
                if (std::ranges::all_of(inputs, [&](const auto& input) {
                    return input.index.Resolve() == inputs.front().index.Resolve();
                })) result = inputs.front();
                else if (auto* block = Owner(inst)) {
                    auto phi = block->PrependNewInst(block->begin(), IR::Opcode::Phi);
                    phi->SetFlags(IR::Type::U32);
                    for (size_t i = 0; i < inputs.size(); ++i)
                        phi->AddPhiOperand(inst->PhiBlock(i), inputs[i].index);
                    result = inputs.front();
                    result->index = IR::U32{IR::Value{&*phi}};
                }
            }
        }
        active.erase(inst);
        cache.emplace(inst, result);
        return result;
    }

    std::optional<BufferTextureAddress> FindForUse(IR::Value value, const IR::Block* use) {
        if (const auto result = Find(value)) return result;
        std::optional<BufferTextureAddress> source;
        const IR::Block* root{};
        std::vector<std::pair<const IR::Block*, const IR::Block*>> invalid_edges;
        std::unordered_set<const IR::Inst*> visiting;
        size_t budget = 4096;
        const auto collect = [&](auto&& self, IR::Value current,
                                 std::pair<const IR::Block*, const IR::Block*> edge, u32 depth) -> bool {
            if (depth > 128 || !budget) return false;
            --budget;
            current = current.Resolve();
            auto* inst = current.TryInstRecursive();
            if (const auto origin = Track(current, program.info)) {
                const auto* owner = Owner(inst);
                if (!owner || (source && (!SameArray(*source, *origin) ||
                    source->index.Resolve() != origin->index.Resolve() || root != owner))) return false;
                source = *origin;
                root = owner;
                return true;
            }
            if (inst && (inst->GetOpcode() == IR::Opcode::BitCastU32F32 ||
                         inst->GetOpcode() == IR::Opcode::BitCastF32U32))
                return self(self, inst->Arg(0), edge, depth + 1);
            if (inst && inst->GetOpcode() == IR::Opcode::Phi && inst->NumArgs()) {
                const auto* owner = Owner(inst);
                if (!owner || !visiting.insert(inst).second) return false;
                for (size_t i = 0; i < inst->NumArgs(); ++i)
                    if (!self(self, inst->Arg(i), {inst->PhiBlock(i), owner}, depth + 1)) return false;
                visiting.erase(inst);
                return true;
            }
            if (!edge.first || !edge.second) return false;
            invalid_edges.push_back(edge);
            return true;
        };
        if (!collect(collect, value, {}, 0) || !source || invalid_edges.empty()) return {};
        ControlFlowReachability paths{program, root, use};
        for (const auto& [from, to] : invalid_edges)
            if (!paths.ExcludesEdge(from, to)) return {};
        return source;
    }

    using CbufReader = std::function<std::optional<u32>(u32, u32)>;
    std::array<BufferTextureExclusion, 4> Exclusions(const BufferTextureAddress& source,
                                                   const IR::Block* use, CbufReader read = {}) const {
        std::array<BufferTextureExclusion, 4> result{};
        size_t count{}, candidates{};
        for (const auto* block : program.blocks) for (const auto& compare : block->Instructions()) {
            if (compare.GetOpcode() != IR::Opcode::IEqual &&
                compare.GetOpcode() != IR::Opcode::INotEqual) continue;
            IR::Value value = compare.Arg(0).Resolve();
            auto constant = Constant(compare.Arg(1));
            if (!constant) { value = compare.Arg(1).Resolve(); constant = Constant(compare.Arg(0)); }
            if (!constant) continue;
            IR::Value loaded = value;
            u32 mask = ~0U;
            if (const auto* operation = value.TryInstRecursive(); operation &&
                operation->GetOpcode() == IR::Opcode::BitwiseAnd32) {
                auto bits = Constant(operation->Arg(1));
                loaded = operation->Arg(0);
                if (!bits) { bits = Constant(operation->Arg(0)); loaded = operation->Arg(1); }
                if (!bits || !*bits) continue;
                mask = *bits;
            }
            const auto field = Track(loaded, program.info);
            if (!field || field->cbuf_index != source.cbuf_index ||
                field->cbuf_offset != source.cbuf_offset || field->stride != source.stride ||
                field->index.Resolve() != source.index.Resolve() || (*constant & ~mask)) continue;
            const auto* owner = Owner(value.TryInstRecursive());
            if (!owner || ++candidates > 32) return result;
            ControlFlowReachability paths{program, owner, use};
            for (bool not_equal : {false, true}) {
                const BufferTextureExclusion exclusion{field->offset, mask, *constant, not_equal};
                if (std::ranges::find(result, exclusion) != result.end() ||
                    !paths.ExcludesEquality(value, *constant, not_equal)) continue;
                result[count++] = exclusion;
                if (count == result.size()) return result;
            }
        }
        if (!read || count) return result;
        std::unordered_set<const IR::Inst*> visited;
        const auto discover = [&](auto&& self, IR::Value value, u32 depth) -> void {
            const auto* inst = value.TryInstRecursive();
            if (!inst || depth > 32 || visited.size() >= 256 || !visited.insert(inst).second || count == result.size()) return;
            const auto field = Track(value, program.info);
            if (field && field->cbuf_index == source.cbuf_index &&
                field->cbuf_offset == source.cbuf_offset && field->stride == source.stride &&
                field->index.Resolve() == source.index.Resolve()) {
                const auto* owner = Owner(inst);
                if (!owner) return;
                for (u32 tag = 0; tag < 8 && count < result.size(); ++tag) {
                    size_t budget = 4096;
                    const auto evaluate = [&](auto&& eval, IR::Value input, u32 level) -> std::optional<u32> {
                        input = input.Resolve();
                        if (input == value.Resolve()) return tag;
                        if (const auto constant = Constant(input)) return constant;
                        const auto* operation = input.TryInstRecursive();
                        if (!operation || level > 32 || !budget) return {};
                        --budget;
                        const auto op = operation->GetOpcode();
                        if (op != IR::Opcode::IAdd32 && op != IR::Opcode::ISub32 &&
                            op != IR::Opcode::IMul32 && op != IR::Opcode::UMin32 &&
                            op != IR::Opcode::BitwiseAnd32 && op != IR::Opcode::ShiftLeftLogical32 &&
                            op != IR::Opcode::ShiftRightLogical32 && op != IR::Opcode::GetCbufU32) return {};
                        const auto a = eval(eval, operation->Arg(0), level + 1);
                        const auto b = eval(eval, operation->Arg(1), level + 1);
                        if (!a || !b) return {};
                        switch (op) {
                        case IR::Opcode::IAdd32: return *a + *b;
                        case IR::Opcode::ISub32: return *a - *b;
                        case IR::Opcode::IMul32: return *a * *b;
                        case IR::Opcode::UMin32: return std::min(*a, *b);
                        case IR::Opcode::BitwiseAnd32: return *a & *b;
                        case IR::Opcode::ShiftLeftLogical32: return *b < 32 ? std::optional<u32>{*a << *b} : std::nullopt;
                        case IR::Opcode::ShiftRightLogical32: return *b < 32 ? std::optional<u32>{*a >> *b} : std::nullopt;
                        case IR::Opcode::GetCbufU32:
                            return *a < Info::MAX_CBUFS && *b < 65536 && !(*b & 3) ? read(*a, *b) : std::nullopt;
                        default: return {};
                        }
                    };
                    ControlFlowReachability paths{program, owner, use,
                        [&](IR::Value input) { return evaluate(evaluate, input, 0); }};
                    if (paths.IsUnreachable()) result[count++] = {field->offset, ~0U, tag};
                }
                return;
            }
            // Follow only expressions whose values we can evaluate. Memory
            // addresses and unrelated Phi inputs must not supply a record tag.
            switch (inst->GetOpcode()) {
            case IR::Opcode::IAdd32: case IR::Opcode::ISub32: case IR::Opcode::IMul32:
            case IR::Opcode::UMin32: case IR::Opcode::BitwiseAnd32:
            case IR::Opcode::ShiftLeftLogical32: case IR::Opcode::ShiftRightLogical32:
            case IR::Opcode::GetCbufU32:
                for (size_t arg = 0; arg < inst->NumArgs(); ++arg) self(self, inst->Arg(arg), depth + 1);
                break;
            default: break;
            }
        };
        for (const auto* block : program.blocks) for (const auto& compare : block->Instructions())
            if (compare.GetOpcode() == IR::Opcode::IEqual || compare.GetOpcode() == IR::Opcode::INotEqual)
                for (size_t arg = 0; arg < compare.NumArgs(); ++arg) discover(discover, compare.Arg(arg), 0);
        return result;
    }

    size_t ResolvedCount() const {
        return static_cast<size_t>(std::ranges::count_if(cache, [](const auto& entry) { return entry.second.has_value(); }));
    }

private:
    IR::Block* Owner(const IR::Inst* inst) const {
        for (auto* block : program.blocks)
            for (const auto& candidate : block->Instructions()) if (&candidate == inst) return block;
        return nullptr;
    }
    IR::Program& program;
    std::unordered_map<const IR::Inst*, std::optional<BufferTextureAddress>> cache;
    std::unordered_set<const IR::Inst*> active;
};

inline bool ContainsLoadedHandle(IR::Value value) {
    std::vector<IR::Value> pending{value};
    std::unordered_set<const IR::Inst*> visited;
    while (!pending.empty()) {
        const auto current = pending.back(); pending.pop_back();
        const auto* inst = current.TryInstRecursive();
        if (!inst || !visited.insert(inst).second) continue;
        switch (inst->GetOpcode()) {
        case IR::Opcode::GetCbufU32: case IR::Opcode::GetCbufU32x2: break;
        case IR::Opcode::LoadStorageU8: case IR::Opcode::LoadStorageS8:
        case IR::Opcode::LoadStorageU16: case IR::Opcode::LoadStorageS16:
        case IR::Opcode::LoadStorage32: case IR::Opcode::LoadStorage64: case IR::Opcode::LoadStorage128:
        case IR::Opcode::LoadGlobalU8: case IR::Opcode::LoadGlobalS8:
        case IR::Opcode::LoadGlobalU16: case IR::Opcode::LoadGlobalS16:
        case IR::Opcode::LoadGlobal32: case IR::Opcode::LoadGlobal64: case IR::Opcode::LoadGlobal128:
            return true;
        default:
            for (size_t i = 0; i < inst->NumArgs(); ++i) pending.push_back(inst->Arg(i));
        }
    }
    return false;
}

} // namespace BufferTexture
} // namespace Shader::Optimization
