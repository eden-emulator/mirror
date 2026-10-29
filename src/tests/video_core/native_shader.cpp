// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <bit>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <unordered_map>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "video_core/texture_cache/format_lookup_table.h"
#include "video_core/buffer_cache/storage_range_groups.h"
#include "video_core/shader_environment.h"
#include "video_core/buffer_cache/storage_buffer_size.h"
#include "shader_recompiler/environment.h"
#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/exception.h"
#include "shader_recompiler/frontend/ir/program.h"
#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"
#include "shader_recompiler/frontend/ampere/trap_guards.h"
#include "shader_recompiler/frontend/ampere/program_override.h"
#include "shader_recompiler/frontend/ampere/program_capture.h"
#include "shader_recompiler/frontend/ampere/program_address_map.h"
#include "shader_recompiler/frontend/ir/basic_block.h"
#include "shader_recompiler/frontend/ir/modifiers.h"
#include "shader_recompiler/frontend/maxwell/decode.h"
#include "shader_recompiler/frontend/maxwell/instruction.h"
#include "shader_recompiler/frontend/maxwell/translate_program.h"
#include "shader_recompiler/host_translate_info.h"
#include "shader_recompiler/ir_opt/passes.h"
#include "shader_recompiler/ir_opt/buffer_texture.h"
#include "shader_recompiler/ir_opt/conditional_phi.h"
#include "shader_recompiler/frontend/maxwell/translate/impl/impl.h"

namespace {
namespace IR = Shader::IR;
using Native = Shader::Ampere::Instruction;

class EmptyEnvironment : public Shader::Environment {
public:
    bool HasNativeRegisterCopies() const override { return true; }
    u64 ReadInstruction(u32) override { throw std::logic_error("unexpected instruction read"); }
    u32 ReadCbufValue(u32, u32) override { throw std::logic_error("unexpected constant folding"); }
    Shader::TextureType ReadTextureType(u32) override { return {}; }
    Shader::TexturePixelFormat ReadTexturePixelFormat(u32) override { return {}; }
    bool IsTexturePixelFormatInteger(u32) override { return false; }
    u32 ReadViewportTransformState() override { return 1; }
    u32 TextureBoundBuffer() const override { return 0; }
    u32 LocalMemorySize() const override { return local_size; }
    u32 local_size{};
    u32 SharedMemorySize() const override { return 0; }
    std::array<u32, 3> WorkgroupSize() const override { return {}; }
    bool HasHLEMacroState() const override { return false; }
    std::optional<Shader::ReplaceConstant> GetReplaceConstBuffer(u32, u32) override { return {}; }
    void Dump(u64, u64) override {}
};

constexpr std::array<Native, 7> VertexTransform{{
    {{0xff007321, 0x000080ff, 0x00000400, 0x000e2200}}, // load two input components
    {{0x00037a02, 0x00c00000, 0x00000f00, 0x000fe400}}, // scale X
    {{0x00027a02, 0x00c00100, 0x00000f00, 0x000fe200}}, // scale Y
    {{0x00007623, 0x00c00200, 0x00010003, 0x001fe200}}, // X * scale X + offset X
    {{0x01017623, 0x00c00300, 0x00010002, 0x000fc400}}, // Y * scale Y + offset Y
    {{0xff007322, 0x00007000, 0x000004ff, 0x000fe200}}, // store position X/Y
    {{0x0000794d, 0x00000000, 0x03800000, 0x000fea00}},
}};

// Evaluate the existing frontend's emitted IR, independently of native bit
// decoding, to check actual attribute/constant/register data flow.
std::array<float, 2> EvaluateVertex(IR::Block& block, std::array<float, 2> input,
                                  std::array<float, 4> constants, s32 vertex_id = 0) {
    std::unordered_map<const IR::Inst*, u32> values;
    std::unordered_map<const IR::Inst*, std::array<u32, 2>> pairs;
    std::array<u32, IR::NUM_REGS> regs{};
    std::array<float, 2> output{};
    const auto get = [&](const IR::Value& value) {
        if (!value.IsImmediate()) return values.at(value.Inst());
        if (value.Type() == IR::Type::U1) return static_cast<u32>(value.U1());
        return value.Type() == IR::Type::F32 ? std::bit_cast<u32>(value.F32()) : value.U32();
    };
    for (const auto& inst : block.Instructions()) {
        const auto arg = [&](size_t i) { return get(inst.Arg(i)); };
        switch (inst.GetOpcode()) {
        case IR::Opcode::GetRegister:
            values[&inst] = regs[static_cast<u32>(inst.Arg(0).Reg())];
            break;
        case IR::Opcode::SetRegister:
            if (inst.Arg(0).Reg() != IR::Reg::RZ) regs[static_cast<u32>(inst.Arg(0).Reg())] = arg(1);
            break;
        case IR::Opcode::GetAttributeU32:
            REQUIRE(static_cast<u32>(inst.Arg(0).Attribute()) == 0x2fc / 4);
            values[&inst] = static_cast<u32>(vertex_id);
            break;
        case IR::Opcode::ShiftLeftLogical32:
            values[&inst] = arg(0) << arg(1);
            break;
        case IR::Opcode::ShiftRightArithmetic32:
            values[&inst] = static_cast<u32>(static_cast<s32>(arg(0)) >> arg(1));
            break;
        case IR::Opcode::SLessThan:
            values[&inst] = static_cast<s32>(arg(0)) < static_cast<s32>(arg(1));
            break;
        case IR::Opcode::ULessThan:
            values[&inst] = arg(0) < arg(1);
            break;
        case IR::Opcode::SelectU32:
            values[&inst] = arg(0) ? arg(1) : arg(2);
            break;
        case IR::Opcode::SMin32:
            values[&inst] = static_cast<u32>(std::min(static_cast<s32>(arg(0)), static_cast<s32>(arg(1))));
            break;
        case IR::Opcode::SMax32:
            values[&inst] = static_cast<u32>(std::max(static_cast<s32>(arg(0)), static_cast<s32>(arg(1))));
            break;
        case IR::Opcode::INeg32:
            values[&inst] = 0U - arg(0);
            break;
        case IR::Opcode::IAdd32:
            values[&inst] = arg(0) + arg(1);
            break;
        case IR::Opcode::ConvertF32S32:
            values[&inst] = std::bit_cast<u32>(static_cast<float>(static_cast<s32>(arg(0))));
            break;
        case IR::Opcode::GetAttribute: {
            const auto attr = static_cast<u32>(inst.Arg(0).Attribute());
            if (attr == 0x2fc / 4) {
                values[&inst] = static_cast<u32>(vertex_id);
                break;
            }
            REQUIRE(attr >= 0x80 / 4);
            REQUIRE(attr < 0x88 / 4);
            values[&inst] = std::bit_cast<u32>(input[attr - 0x80 / 4]);
            break;
        }
        case IR::Opcode::GetCbufU32x2:
            REQUIRE(arg(0) == 3);
            REQUIRE(arg(1) % 4 == 0);
            REQUIRE(arg(1) + 8 <= sizeof(constants));
            pairs[&inst] = {std::bit_cast<u32>(constants[arg(1) / 4]), std::bit_cast<u32>(constants[arg(1) / 4 + 1])};
            break;
        case IR::Opcode::CompositeExtractU32x2:
            values[&inst] = pairs.at(inst.Arg(0).Inst()).at(arg(1));
            break;
        case IR::Opcode::GetCbufF32:
        case IR::Opcode::GetCbufU32:
            REQUIRE(arg(0) == 3);
            REQUIRE(arg(1) < 16);
            values[&inst] = std::bit_cast<u32>(constants[arg(1) / 4]);
            break;
        case IR::Opcode::BitCastF32U32:
        case IR::Opcode::BitCastU32F32:
            values[&inst] = arg(0);
            break;
        case IR::Opcode::LogicalAnd:
            values[&inst] = arg(0) && arg(1);
            break;
        case IR::Opcode::LogicalOr:
            values[&inst] = arg(0) || arg(1);
            break;
        case IR::Opcode::LogicalNot:
            values[&inst] = !arg(0);
            break;
        case IR::Opcode::FPIsNan32:
            values[&inst] = std::isnan(std::bit_cast<float>(arg(0)));
            break;
        case IR::Opcode::FPOrdGreaterThanEqual32:
            values[&inst] = std::bit_cast<float>(arg(0)) >= std::bit_cast<float>(arg(1));
            break;
        case IR::Opcode::FPRoundEven32:
            values[&inst] = std::bit_cast<u32>(std::nearbyint(std::bit_cast<float>(arg(0))));
            break;
        case IR::Opcode::FPFloor32:
            values[&inst] = std::bit_cast<u32>(std::floor(std::bit_cast<float>(arg(0))));
            break;
        case IR::Opcode::FPTrunc32:
            values[&inst] = std::bit_cast<u32>(std::trunc(std::bit_cast<float>(arg(0))));
            break;
        case IR::Opcode::FPClamp32:
            values[&inst] = std::bit_cast<u32>(std::clamp(std::bit_cast<float>(arg(0)),
                std::bit_cast<float>(arg(1)), std::bit_cast<float>(arg(2))));
            break;
        case IR::Opcode::ConvertU32F32:
            values[&inst] = static_cast<u32>(std::bit_cast<float>(arg(0)));
            break;
        case IR::Opcode::ConvertF32U32:
            values[&inst] = std::bit_cast<u32>(static_cast<float>(arg(0)));
            break;
        case IR::Opcode::BitFieldUExtract:
            values[&inst] = (arg(0) >> arg(1)) & ((1U << arg(2)) - 1);
            break;
        case IR::Opcode::BitwiseOr32:
            values[&inst] = arg(0) | arg(1);
            break;
        case IR::Opcode::FPOrdLessThan32:
            values[&inst] = std::bit_cast<float>(arg(0)) < std::bit_cast<float>(arg(1));
            break;
        case IR::Opcode::BitwiseXor32:
            values[&inst] = arg(0) ^ arg(1);
            break;
        case IR::Opcode::BitwiseNot32:
            values[&inst] = ~arg(0);
            break;
        case IR::Opcode::BitwiseAnd32:
            values[&inst] = arg(0) & arg(1);
            break;
        case IR::Opcode::FPAdd32:
            values[&inst] = std::bit_cast<u32>(std::bit_cast<float>(arg(0)) + std::bit_cast<float>(arg(1)));
            break;
        case IR::Opcode::FPMul32:
            values[&inst] = std::bit_cast<u32>(std::bit_cast<float>(arg(0)) * std::bit_cast<float>(arg(1)));
            break;
        case IR::Opcode::FPMin32:
            values[&inst] = std::bit_cast<u32>(std::fmin(std::bit_cast<float>(arg(0)), std::bit_cast<float>(arg(1))));
            break;
        case IR::Opcode::FPMax32:
            values[&inst] = std::bit_cast<u32>(std::fmax(std::bit_cast<float>(arg(0)), std::bit_cast<float>(arg(1))));
            break;
        case IR::Opcode::SelectF32:
            values[&inst] = arg(0) ? arg(1) : arg(2);
            break;
        case IR::Opcode::FPFma32: {
            const auto control = inst.Flags<IR::FpControl>();
            REQUIRE(control.fmz_mode == IR::FmzMode::FTZ);
            REQUIRE(control.rounding == IR::FpRounding::RN);
            values[&inst] = std::bit_cast<u32>(std::fma(std::bit_cast<float>(arg(0)),
                std::bit_cast<float>(arg(1)), std::bit_cast<float>(arg(2))));
            break;
        }
        case IR::Opcode::SetAttribute: {
            const auto attr = static_cast<u32>(inst.Arg(0).Attribute());
            REQUIRE(attr >= 0x70 / 4);
            REQUIRE(attr < 0x78 / 4);
            output[attr - 0x70 / 4] = std::bit_cast<float>(arg(1));
            break;
        }
        default:
            FAIL("Unexpected IR operation " << IR::NameOf(inst.GetOpcode()));
        }
    }
    return output;
}
} // namespace

namespace {
double DecodeTestHalf(u16 bits);
u16 EncodeTestHalf(double value);
using Warp = std::array<u32, 32>;
void ExecuteNativeWarp(IR::Block& block, u32 active,
                       std::array<std::array<u32, IR::NUM_REGS>, 32>& registers,
                       std::array<std::array<bool, 7>, 32>& predicates,
                       const std::unordered_map<u32, u32>* shared = nullptr) {
    std::unordered_map<const IR::Inst*, Warp> values;
    const auto get = [&](const IR::Value& value) -> Warp {
        if (!value.IsImmediate()) return values.at(value.Inst());
        Warp result;
        result.fill(value.Type() == IR::Type::U1 ? static_cast<u32>(value.U1()) :
                    value.Type() == IR::Type::F16 || value.Type() == IR::Type::U16 ? value.U16() : value.U32());
        return result;
    };
    for (const auto& inst : block.Instructions()) {
        std::array<Warp, 4> args{};
        for (size_t i = 0; i < inst.NumArgs(); ++i) {
            if (inst.Arg(i).Type() != IR::Type::Reg && inst.Arg(i).Type() != IR::Type::Pred)
                args[i] = get(inst.Arg(i));
        }
        Warp result{};
        const auto op = inst.GetOpcode();
        if (op == IR::Opcode::SubgroupBallot || op == IR::Opcode::VoteAny || op == IR::Opcode::VoteAll) {
            u32 mask = 0;
            for (u32 lane = 0; lane < 32; ++lane)
                if ((active & (1U << lane)) && args[0][lane]) mask |= 1U << lane;
            result.fill(op == IR::Opcode::SubgroupBallot ? mask : op == IR::Opcode::VoteAny ? mask != 0 : mask == active);
        } else {
            for (u32 lane = 0; lane < 32; ++lane) {
                const auto a = args[0][lane], b = args[1][lane], c = args[2][lane];
                switch (op) {
                case IR::Opcode::GetRegister: result[lane] = static_cast<u32>(inst.Arg(0).Reg()) == 255 ? 0 : registers[lane][static_cast<u32>(inst.Arg(0).Reg())]; break;
                case IR::Opcode::SetRegister:
                    if ((active & (1U << lane)) && static_cast<u32>(inst.Arg(0).Reg()) != 255) registers[lane][static_cast<u32>(inst.Arg(0).Reg())] = b;
                    break;
                case IR::Opcode::GetPred: result[lane] = predicates[lane][static_cast<u32>(inst.Arg(0).Pred())]; break;
                case IR::Opcode::SetPred:
                    if (active & (1U << lane)) predicates[lane][static_cast<u32>(inst.Arg(0).Pred())] = b != 0;
                    break;
                case IR::Opcode::LaneId: result[lane] = lane; break;
                case IR::Opcode::LoadSharedU32:
                    REQUIRE(shared != nullptr);
                    result[lane] = shared->at(a);
                    break;
                case IR::Opcode::UnpackFloat2x16: case IR::Opcode::PackFloat2x16: result[lane] = a; break;
                case IR::Opcode::CompositeExtractF16x2: result[lane] = (a >> (b * 16)) & 0xffff; break;
                case IR::Opcode::CompositeConstructF16x2: result[lane] = a | (b << 16); break;
                case IR::Opcode::FPNeg16: result[lane] = a ^ 0x8000; break;
                case IR::Opcode::FPAbs16: result[lane] = a & 0x7fff; break;
                case IR::Opcode::ConvertF32F16: result[lane] = std::bit_cast<u32>(static_cast<float>(DecodeTestHalf(static_cast<u16>(a)))); break;
                case IR::Opcode::ConvertF16F32: result[lane] = EncodeTestHalf(std::bit_cast<float>(a)); break;
                case IR::Opcode::FPFma32: result[lane] = std::bit_cast<u32>(std::fma(std::bit_cast<float>(a), std::bit_cast<float>(b), std::bit_cast<float>(c))); break;
                case IR::Opcode::FPFma16: result[lane] = EncodeTestHalf(std::fma(DecodeTestHalf(static_cast<u16>(a)),
                    DecodeTestHalf(static_cast<u16>(b)), DecodeTestHalf(static_cast<u16>(c)))); break;
                case IR::Opcode::BitCastF32U32: case IR::Opcode::BitCastU32F32: result[lane] = a; break;
                case IR::Opcode::SelectU32: case IR::Opcode::SelectU1: result[lane] = a ? b : c; break;
                case IR::Opcode::BitwiseAnd32: result[lane] = a & b; break;
                case IR::Opcode::BitwiseOr32: result[lane] = a | b; break;
                case IR::Opcode::BitwiseNot32: result[lane] = ~a; break;
                case IR::Opcode::UMin32: result[lane] = std::min(a, b); break;
                case IR::Opcode::UMax32: result[lane] = std::max(a, b); break;
                case IR::Opcode::ShiftRightLogical32: result[lane] = b < 32 ? a >> b : 0; break;
                case IR::Opcode::ShiftLeftLogical32: result[lane] = b < 32 ? a << b : 0; break;
                case IR::Opcode::INeg32: result[lane] = 0U - a; break;
                case IR::Opcode::IAdd32: result[lane] = a + b; break;
                case IR::Opcode::FindUMsb32: result[lane] = a ? 31U - std::countl_zero(a) : ~0U; break;
                case IR::Opcode::BitFieldUExtract: result[lane] = (a >> b) & ((1U << c) - 1); break;
                case IR::Opcode::ULessThan: result[lane] = a < b; break;
                case IR::Opcode::SLessThan: result[lane] = static_cast<s32>(a) < static_cast<s32>(b); break;
                case IR::Opcode::IEqual: result[lane] = a == b; break;
                case IR::Opcode::GetZeroFromOp: result[lane] = a == 0; break;
                case IR::Opcode::LogicalAnd: result[lane] = a && b; break;
                case IR::Opcode::LogicalNot: result[lane] = !a; break;
                case IR::Opcode::GetInBoundsFromOp: {
                    const auto* shuffle = inst.Arg(0).Inst();
                    const u32 index = get(shuffle->Arg(1))[lane] & 31;
                    const u32 clamp = get(shuffle->Arg(2))[lane], segment = get(shuffle->Arg(3))[lane];
                    const bool upward = shuffle->GetOpcode() == IR::Opcode::ShuffleUp;
                    const u32 target = upward ? lane - index : shuffle->GetOpcode() == IR::Opcode::ShuffleButterfly
                        ? lane ^ index : (index & ~segment) | (lane & segment);
                    const u32 bound = (lane & segment) | (clamp & ~segment);
                    result[lane] = upward ? static_cast<s32>(target) >= static_cast<s32>(bound) : target <= bound;
                    break;
                }
                case IR::Opcode::FSwizzleAdd: {
                    const u32 mode = (c >> ((lane & 3) * 2)) & 3;
                    const float x = std::bit_cast<float>(a), y = std::bit_cast<float>(b);
                    const float ma = mode == 0 || mode == 2 ? -1.0f : mode == 1 ? 1.0f : 0.0f;
                    const float mb = mode == 0 || mode == 1 ? -1.0f : mode == 2 ? 1.0f : 0.0f;
                    result[lane] = std::bit_cast<u32>(x * ma + y * mb);
                    break;
                }
                case IR::Opcode::ShuffleDown: result[lane] = args[0][lane + b <= c ? lane + b : lane]; break;
                case IR::Opcode::ShuffleButterfly: case IR::Opcode::ShuffleIndex: case IR::Opcode::ShuffleUp: {
                    const u32 segment = args[3][lane];
                    const bool upward = op == IR::Opcode::ShuffleUp;
                    const u32 target = upward ? lane - (b & 31) : op == IR::Opcode::ShuffleButterfly
                        ? lane ^ (b & 31) : ((b & 31) & ~segment) | (lane & segment);
                    const u32 bound = (lane & segment) | (c & ~segment);
                    const bool valid = upward ? static_cast<s32>(target) >= static_cast<s32>(bound) : target <= bound;
                    result[lane] = !valid ? a
                        : (active & (1U << target)) ? args[0][target] : 0xdeadbeefU;
                    break;
                }
                default: FAIL("Unexpected warp-uniform IR " << IR::NameOf(op));
                }
            }
        }
        values.emplace(&inst, result);
    }
}
}


namespace {
double DecodeTestHalf(u16 bits) {
    const u32 exponent = (bits >> 10) & 31, mantissa = bits & 1023;
    const double value = exponent == 31 ? (mantissa ? std::numeric_limits<double>::quiet_NaN()
                                                   : std::numeric_limits<double>::infinity())
                        : exponent == 0 ? std::ldexp(static_cast<double>(mantissa), -24)
                                        : std::ldexp(static_cast<double>(1024 + mantissa), static_cast<int>(exponent) - 25);
    return (bits & 0x8000) ? -value : value;
}
u16 EncodeTestHalf(double value) {
    const u16 sign = std::signbit(value) ? 0x8000 : 0;
    value = std::abs(value);
    if (std::isnan(value)) return 0xfe01; // Deliberately noncanonical: ReLU must canonicalize it.
    if (value >= 65520) return sign | 0x7c00;
    if (value < std::ldexp(1.0, -14))
        return sign | static_cast<u16>(std::nearbyint(std::ldexp(value, 24)));
    int exponent{};
    std::frexp(value, &exponent);
    --exponent;
    u32 mantissa = static_cast<u32>(std::nearbyint(std::ldexp(value, 10 - exponent)));
    if (mantissa == 2048) { ++exponent; mantissa = 1024; }
    return sign | static_cast<u16>(((exponent + 15) << 10) | (mantissa - 1024));
}
struct ScalarNativeMachine {
    std::array<u32, IR::NUM_REGS> regs{};
    std::array<bool, 7> predicates{};
    std::unordered_map<u64, u32> constants, global, shared, local;
    std::unordered_map<u32, u32> attributes;
    u32 workgroup_barriers{}, device_barriers{}, shared_memory_barriers{};
    bool carry{}, zero{}, sign{}, overflow{};
    void Execute(IR::Block& block) {
        std::unordered_map<const IR::Inst*, u64> values;
        std::unordered_map<const IR::Inst*, std::array<u32, 4>> vectors;
        const auto get = [&](const IR::Value& value) -> u64 {
            if (!value.IsImmediate()) return values.at(value.Inst());
            if (value.Type() == IR::Type::U1) return value.U1();
            if (value.Type() == IR::Type::F16 || value.Type() == IR::Type::U16) return value.U16();
            if (value.Type() == IR::Type::U64) return value.U64();
            if (value.Type() == IR::Type::F32) return std::bit_cast<u32>(value.F32());
            return value.U32();
        };
        for (const auto& inst : block.Instructions()) {
            const auto arg = [&](size_t i) { return get(inst.Arg(i)); };
            u64 result{};
            switch (inst.GetOpcode()) {
            case IR::Opcode::GetAttribute: case IR::Opcode::GetAttributeU32:
                result = attributes.at(static_cast<u32>(inst.Arg(0).Attribute())); break;
            case IR::Opcode::SetAttribute:
                attributes[static_cast<u32>(inst.Arg(0).Attribute())] = static_cast<u32>(arg(1)); break;
            case IR::Opcode::BitCastF32U32: case IR::Opcode::BitCastU32F32: result = arg(0); break;
            case IR::Opcode::UnpackFloat2x16: result = (arg(0) & 0xffff) | ((arg(0) >> 16) << 32); break;
            case IR::Opcode::PackFloat2x16: result = (arg(0) & 0xffff) | ((arg(0) >> 32) << 16); break;
            case IR::Opcode::CompositeConstructF16x2: result = arg(0) | (arg(1) << 32); break;
            case IR::Opcode::CompositeExtractF16x2: result = (arg(0) >> (arg(1) * 32)) & 0xffff; break;
            case IR::Opcode::FPNeg16: result = arg(0) ^ 0x8000; break;
            case IR::Opcode::FPAbs16: result = arg(0) & 0x7fff; break;
            case IR::Opcode::FPMul32: result = std::bit_cast<u32>(std::bit_cast<float>(static_cast<u32>(arg(0))) * std::bit_cast<float>(static_cast<u32>(arg(1)))); break;
            case IR::Opcode::FPAdd32: result = std::bit_cast<u32>(std::bit_cast<float>(static_cast<u32>(arg(0))) + std::bit_cast<float>(static_cast<u32>(arg(1)))); break;
            case IR::Opcode::FPFma32: result = std::bit_cast<u32>(std::fma(std::bit_cast<float>(static_cast<u32>(arg(0))),
                std::bit_cast<float>(static_cast<u32>(arg(1))), std::bit_cast<float>(static_cast<u32>(arg(2))))); break;
            case IR::Opcode::FPSaturate32: {
                const float value = std::bit_cast<float>(static_cast<u32>(arg(0)));
                result = std::bit_cast<u32>(std::isnan(value) ? 0.0f : std::clamp(value, 0.0f, 1.0f));
                break;
            }
            case IR::Opcode::FPFma16: result = EncodeTestHalf(std::fma(DecodeTestHalf(static_cast<u16>(arg(0))),
                DecodeTestHalf(static_cast<u16>(arg(1))), DecodeTestHalf(static_cast<u16>(arg(2))))); break;
            case IR::Opcode::FPRecip32: result = std::bit_cast<u32>(1.0f / std::bit_cast<float>(static_cast<u32>(arg(0)))); break;
            case IR::Opcode::FPFloor32: result = std::bit_cast<u32>(std::floor(std::bit_cast<float>(static_cast<u32>(arg(0))))); break;
            case IR::Opcode::ConvertF32F16: result = std::bit_cast<u32>(static_cast<float>(DecodeTestHalf(static_cast<u16>(arg(0))))); break;
            case IR::Opcode::FPUnordGreaterThanEqual16: {
                const float a = DecodeTestHalf(static_cast<u16>(arg(0)));
                const float b = DecodeTestHalf(static_cast<u16>(arg(1)));
                result = std::isnan(a) || std::isnan(b) || a >= b;
                break;
            }
            case IR::Opcode::FPUnordEqual16: {
                const double a = DecodeTestHalf(static_cast<u16>(arg(0))), b = DecodeTestHalf(static_cast<u16>(arg(1)));
                result = std::isnan(a) || std::isnan(b) || a == b;
                break;
            }
            case IR::Opcode::FPOrdEqual32: result = std::bit_cast<float>(static_cast<u32>(arg(0))) == std::bit_cast<float>(static_cast<u32>(arg(1))); break;
            case IR::Opcode::FPOrdLessThanEqual32: result = std::bit_cast<float>(static_cast<u32>(arg(0))) <= std::bit_cast<float>(static_cast<u32>(arg(1))); break;
            case IR::Opcode::FPOrdGreaterThanEqual32: result = std::bit_cast<float>(static_cast<u32>(arg(0))) >= std::bit_cast<float>(static_cast<u32>(arg(1))); break;
            case IR::Opcode::FPMax32: result = std::bit_cast<u32>(std::fmax(std::bit_cast<float>(static_cast<u32>(arg(0))), std::bit_cast<float>(static_cast<u32>(arg(1))))); break;
            case IR::Opcode::FPMin32: result = std::bit_cast<u32>(std::fmin(std::bit_cast<float>(static_cast<u32>(arg(0))), std::bit_cast<float>(static_cast<u32>(arg(1))))); break;
            case IR::Opcode::ConvertU64F32: {
                const float value = std::bit_cast<float>(static_cast<u32>(arg(0)));
                REQUIRE(std::isfinite(value));
                REQUIRE(value >= 0.0f);
                REQUIRE(static_cast<double>(value) < 0x1p64);
                result = static_cast<u64>(value);
                break;
            }
            case IR::Opcode::ConvertF32U32: result = std::bit_cast<u32>(static_cast<float>(static_cast<u32>(arg(0)))); break;
            case IR::Opcode::ConvertU32F32: result = static_cast<u32>(std::bit_cast<float>(static_cast<u32>(arg(0)))); break;
            case IR::Opcode::FPTrunc32: result = std::bit_cast<u32>(std::trunc(std::bit_cast<float>(static_cast<u32>(arg(0))))); break;
            case IR::Opcode::FPTanh: result = std::bit_cast<u32>(std::tanh(std::bit_cast<float>(static_cast<u32>(arg(0))))); break;
            case IR::Opcode::FPNeg32: result = arg(0) ^ 0x80000000U; break;
            case IR::Opcode::IAbs32: result = static_cast<s32>(arg(0)) < 0 ? 0U - static_cast<u32>(arg(0)) : static_cast<u32>(arg(0)); break;
            case IR::Opcode::FPAbs32: result = arg(0) & 0x7fffffffU; break;
            case IR::Opcode::ConvertF16F32: result = EncodeTestHalf(std::bit_cast<float>(static_cast<u32>(arg(0)))); break;
            case IR::Opcode::FPRoundEven32: result = std::bit_cast<u32>(std::nearbyint(std::bit_cast<float>(static_cast<u32>(arg(0))))); break;
            case IR::Opcode::FPIsNan32: result = std::isnan(std::bit_cast<float>(static_cast<u32>(arg(0)))); break;
            case IR::Opcode::FPClamp32: result = std::bit_cast<u32>(std::clamp(std::bit_cast<float>(static_cast<u32>(arg(0))), std::bit_cast<float>(static_cast<u32>(arg(1))), std::bit_cast<float>(static_cast<u32>(arg(2))))); break;
            case IR::Opcode::ConvertS32F32: {
                const float value = std::bit_cast<float>(static_cast<u32>(arg(0)));
                result = std::isnan(value) ? 0x87654321U : static_cast<u32>(static_cast<s32>(value));
                break;
            }
            case IR::Opcode::GetRegister: result = inst.Arg(0).Reg() == IR::Reg::RZ ? 0 : regs[static_cast<u32>(inst.Arg(0).Reg())]; break;
            case IR::Opcode::SetRegister: if (inst.Arg(0).Reg() != IR::Reg::RZ) regs[static_cast<u32>(inst.Arg(0).Reg())] = static_cast<u32>(arg(1)); break;
            case IR::Opcode::GetPred: result = predicates[static_cast<u32>(inst.Arg(0).Pred())]; break;
            case IR::Opcode::SetPred: predicates[static_cast<u32>(inst.Arg(0).Pred())] = arg(1) != 0; break;
            case IR::Opcode::IAdd32: result = static_cast<u32>(arg(0) + arg(1)); break;
            case IR::Opcode::ISub32: result = static_cast<u32>(arg(0) - arg(1)); break;
            case IR::Opcode::IMul32: result = static_cast<u32>(arg(0) * arg(1)); break;
            case IR::Opcode::IAdd64: result = arg(0) + arg(1); break;
            case IR::Opcode::UMax32: result = std::max(arg(0), arg(1)); break;
            case IR::Opcode::UMin32: result = std::min(arg(0), arg(1)); break;
            case IR::Opcode::FindUMsb32: result = arg(0) ? 31U - std::countl_zero(static_cast<u32>(arg(0))) : ~0U; break;
            case IR::Opcode::BitReverse32: {
                const u32 source = static_cast<u32>(arg(0));
                for (u32 bit = 0; bit < 32; ++bit) result |= u64{(source >> bit) & 1U} << (31 - bit);
                break;
            }
            case IR::Opcode::ShiftLeftLogical64: result = arg(1) < 64 ? arg(0) << arg(1) : 0; break;
            case IR::Opcode::ShiftRightLogical64: result = arg(1) < 64 ? arg(0) >> arg(1) : 0; break;
            case IR::Opcode::INeg32: result = 0U - static_cast<u32>(arg(0)); break;
            case IR::Opcode::ShiftLeftLogical32: result = arg(1) < 32 ? static_cast<u32>(arg(0) << arg(1)) : 0; break;
            case IR::Opcode::ShiftRightArithmetic32: result = static_cast<u32>(static_cast<s32>(arg(0)) >> std::min<u64>(arg(1), 31)); break;
            case IR::Opcode::ShiftRightLogical32: result = arg(1) < 32 ? static_cast<u32>(arg(0)) >> arg(1) : 0; break;
            case IR::Opcode::BitwiseAnd32: result = arg(0) & arg(1); break;
            case IR::Opcode::BitwiseOr32: result = arg(0) | arg(1); break;
            case IR::Opcode::BitwiseXor32: result = arg(0) ^ arg(1); break;
            case IR::Opcode::BitwiseNot32: result = arg(0) ^ 0xffffffffULL; break;
            case IR::Opcode::BitFieldSExtract: {
                const u32 value = static_cast<u32>(arg(0)) >> arg(1);
                const u32 width = static_cast<u32>(arg(2));
                result = width == 0 ? 0 : width == 32 ? value :
                    static_cast<u32>(static_cast<s32>(value << (32 - width)) >> (32 - width));
                break;
            }
            case IR::Opcode::BitFieldUExtract: result = (arg(0) >> arg(1)) & ((u64{1} << arg(2)) - 1); break;
            case IR::Opcode::BitFieldInsert: { const u64 mask = ((u64{1} << arg(3)) - 1) << arg(2); result = (arg(0) & ~mask) | ((arg(1) << arg(2)) & mask); break; }
            case IR::Opcode::SelectF32: case IR::Opcode::SelectU64: case IR::Opcode::SelectU32: case IR::Opcode::SelectU1: result = arg(0) ? arg(1) : arg(2); break;
            case IR::Opcode::LogicalAnd: result = arg(0) && arg(1); break;
            case IR::Opcode::LogicalOr: result = arg(0) || arg(1); break;
            case IR::Opcode::LogicalXor: result = (arg(0) != 0) != (arg(1) != 0); break;
            case IR::Opcode::LogicalNot: result = !arg(0); break;
            case IR::Opcode::SMin32: result = static_cast<u32>(std::min(static_cast<s32>(arg(0)), static_cast<s32>(arg(1)))); break;
            case IR::Opcode::SMax32: result = static_cast<u32>(std::max(static_cast<s32>(arg(0)), static_cast<s32>(arg(1)))); break;
            case IR::Opcode::SLessThan: result = static_cast<s32>(arg(0)) < static_cast<s32>(arg(1)); break;
            case IR::Opcode::ULessThan: result = arg(0) < arg(1); break;
            case IR::Opcode::ULessThanEqual: result = arg(0) <= arg(1); break;
            case IR::Opcode::UGreaterThan: result = arg(0) > arg(1); break;
            case IR::Opcode::UGreaterThanEqual: result = arg(0) >= arg(1); break;
            case IR::Opcode::IEqual: result = arg(0) == arg(1); break;
            case IR::Opcode::INotEqual: result = arg(0) != arg(1); break;
            case IR::Opcode::GetCFlag: result = carry; break;
            case IR::Opcode::SetCFlag: carry = arg(0) != 0; break;
            case IR::Opcode::GetZFlag: result = zero; break;
            case IR::Opcode::SetZFlag: zero = arg(0) != 0; break;
            case IR::Opcode::GetSFlag: result = sign; break;
            case IR::Opcode::SetSFlag: sign = arg(0) != 0; break;
            case IR::Opcode::GetOFlag: result = overflow; break;
            case IR::Opcode::SetOFlag: overflow = arg(0) != 0; break;
            case IR::Opcode::GetCarryFromOp: { const auto* sum = inst.Arg(0).Inst(); result = get(sum->Arg(0)) + get(sum->Arg(1)) > 0xffffffffULL; break; }
            case IR::Opcode::GetZeroFromOp: result = arg(0) == 0; break;
            case IR::Opcode::GetSignFromOp: result = (arg(0) & 0x80000000U) != 0; break;
            case IR::Opcode::GetOverflowFromOp: { const auto* sum = inst.Arg(0).Inst(); result = ((get(sum->Arg(0)) ^ arg(0)) & (get(sum->Arg(1)) ^ arg(0)) & 0x80000000U) != 0; break; }
            case IR::Opcode::GetCbufU8: result = constants.at((arg(0) << 32) | arg(1)) & 0xff; break;
            case IR::Opcode::GetCbufU32: result = constants.at((arg(0) << 32) | arg(1)); break;
            case IR::Opcode::GetCbufU32x2: result = constants.at((arg(0) << 32) | arg(1)) | (u64{constants.at((arg(0) << 32) | (arg(1) + 4))} << 32); break;
            case IR::Opcode::CompositeConstructU32x2: result = arg(0) | (arg(1) << 32); break;
            case IR::Opcode::PackUint2x32: case IR::Opcode::UnpackUint2x32: result = arg(0); break;
            case IR::Opcode::CompositeExtractU32x2: result = static_cast<u32>(arg(0) >> (arg(1) * 32)); break;
            case IR::Opcode::CompositeConstructU32x4:
                vectors[&inst] = {static_cast<u32>(arg(0)), static_cast<u32>(arg(1)), static_cast<u32>(arg(2)), static_cast<u32>(arg(3))};
                break;
            case IR::Opcode::WriteSharedU128:
                for (u32 component = 0; component < 4; ++component) shared[arg(0) + component * 4] = vectors.at(inst.Arg(1).Inst())[component];
                break;
            case IR::Opcode::WriteSharedU64: shared[arg(0)] = static_cast<u32>(arg(1)); shared[arg(0) + 4] = static_cast<u32>(arg(1) >> 32); break;
            case IR::Opcode::LoadSharedU64: result = shared.at(arg(0)) | (u64{shared.at(arg(0) + 4)} << 32); break;
            case IR::Opcode::LoadLocal: result = local.at(arg(0)); break;
            case IR::Opcode::WriteLocal: local[arg(0)] = static_cast<u32>(arg(1)); break;
            case IR::Opcode::LoadGlobalU16: result = global.at(arg(0)) & 0xffff; break;
            case IR::Opcode::LoadGlobal32: result = global.at(arg(0)); break;
            case IR::Opcode::LoadGlobal64: result = global.at(arg(0)) | (u64{global.at(arg(0) + 4)} << 32); break;
            case IR::Opcode::WriteGlobal64: global[arg(0)] = static_cast<u32>(arg(1)); global[arg(0) + 4] = static_cast<u32>(arg(1) >> 32); break;
            case IR::Opcode::WriteGlobalU16: global[arg(0)] = static_cast<u32>(arg(1)) & 0xffff; break;
            case IR::Opcode::WriteGlobalU8: global[arg(0)] = static_cast<u32>(arg(1)) & 0xff; break;
            case IR::Opcode::WriteGlobal32: global[arg(0)] = static_cast<u32>(arg(1)); break;
            case IR::Opcode::WriteSharedU32: shared[arg(0)] = static_cast<u32>(arg(1)); break;
            case IR::Opcode::LoadSharedU32: result = shared.at(arg(0)); break;
            case IR::Opcode::GlobalAtomicIAdd32: result = global.at(arg(0)); global[arg(0)] += static_cast<u32>(arg(1)); break;
            case IR::Opcode::GlobalAtomicUMax32: result = global.at(arg(0)); global[arg(0)] = std::max(global[arg(0)], static_cast<u32>(arg(1))); break;
            case IR::Opcode::SharedAtomicUMin32: result = shared.at(arg(0)); shared[arg(0)] = std::min(shared[arg(0)], static_cast<u32>(arg(1))); break;
            case IR::Opcode::SharedAtomicUMax32: result = shared.at(arg(0)); shared[arg(0)] = std::max(shared[arg(0)], static_cast<u32>(arg(1))); break;
            case IR::Opcode::SharedAtomicOr32: result = shared.at(arg(0)); shared[arg(0)] |= static_cast<u32>(arg(1)); break;
            case IR::Opcode::SharedAtomicIAdd32: result = shared.at(arg(0)); shared[arg(0)] += static_cast<u32>(arg(1)); break;
            case IR::Opcode::Barrier: ++workgroup_barriers; break;
            case IR::Opcode::DeviceMemoryBarrier: ++device_barriers; break;
            case IR::Opcode::WorkgroupMemoryBarrier: ++shared_memory_barriers; break;
            default: FAIL("Unexpected scalar compute IR " << IR::NameOf(inst.GetOpcode()));
            }
            values[&inst] = result;
        }
    }
};
void TranslateScalarNative(std::span<const Native> input, IR::Block& block, bool shadow = false, u32 local_size = 0) {
    const auto lowered = Shader::Ampere::LowerToMaxwell(input, {}, shadow);
    INFO("offset=" << lowered.error_offset << " error=" << lowered.error);
    REQUIRE(lowered);
    EmptyEnvironment env;
    env.local_size = local_size;
    Shader::Maxwell::TranslatorVisitor visitor{env, block};
    for (u64 word : lowered.instructions) {
        const auto opcode = Shader::Maxwell::Decode(word);
        if (opcode == Shader::Maxwell::Opcode::EXIT) continue;
        switch (opcode) {
#define INST(name, cute, encode) case Shader::Maxwell::Opcode::name: visitor.name(word); break;
#include "shader_recompiler/frontend/maxwell/maxwell.inc"
#undef INST
        }
    }
}
}


namespace {
void ExecutePredicatedScalarNative(std::span<const Native> input, ScalarNativeMachine& machine, bool shadow = false, u32 local_size = 0) {
    const auto lowered = Shader::Ampere::LowerToMaxwell(input, {}, shadow);
    INFO(lowered.error << " offset=" << lowered.error_offset);
    REQUIRE(lowered);
    for (size_t position = 0; position < lowered.instructions.size(); ++position) {
        const u64 word = lowered.instructions[position];
        const auto opcode = Shader::Maxwell::Decode(word);
        if (opcode == Shader::Maxwell::Opcode::EXIT) continue;
        const auto pred = Shader::Maxwell::Instruction{word}.Pred();
        const bool active = (pred.index == 7 ? true : machine.predicates[pred.index]) != pred.negated;
        if (!active) continue;
        if (opcode == Shader::Maxwell::Opcode::BRA) {
            const auto address = [](size_t index) { return 0x50U + index * 8 + ((index + 1) / 3) * 8; };
            const s32 relative = static_cast<s32>(static_cast<u32>((word >> 20) & 0xffffff) << 8) >> 8;
            const s64 target_address = static_cast<s64>(address(position)) + 8 + relative;
            size_t target = position + 1;
            while (target < lowered.instructions.size() && static_cast<s64>(address(target)) != target_address) ++target;
            REQUIRE(target < lowered.instructions.size());
            position = target - 1;
            continue;
        }
        Shader::ObjectPool<IR::Inst> pool;
        IR::Block block{pool};
        EmptyEnvironment env;
        env.local_size = local_size;
        Shader::Maxwell::TranslatorVisitor visitor{env, block};
        switch (opcode) {
#define INST(name, cute, encode) case Shader::Maxwell::Opcode::name: visitor.name(word); break;
#include "shader_recompiler/frontend/maxwell/maxwell.inc"
#undef INST
        }
        machine.Execute(block);
    }
}
}


TEST_CASE("Native vertex lowering preserves attribute transforms through existing shader IR", "[native-shader]") {
    const auto lowered = Shader::Ampere::LowerToMaxwell(VertexTransform);
    REQUIRE(lowered);
    REQUIRE(lowered.source_size == VertexTransform.size() * 16);
    REQUIRE(lowered.instructions.size() >= VertexTransform.size());
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    EmptyEnvironment env;
    Shader::Maxwell::TranslatorVisitor visitor{env, block};
    for (const u64 word : lowered.instructions) {
        using Shader::Maxwell::Opcode;
        switch (Shader::Maxwell::Decode(word)) {
        case Opcode::ALD: visitor.ALD(word); break;
        case Opcode::AST: visitor.AST(word); break;
        case Opcode::MOV_cbuf: visitor.MOV_cbuf(word); break;
        case Opcode::FFMA_rc: visitor.FFMA_rc(word); break;
        case Opcode::FFMA_reg: visitor.FFMA_reg(word); break;
        case Opcode::MOV_reg: visitor.MOV_reg(word); break;
        case Opcode::EXIT: break;
        default: FAIL("Unexpected lowered vertex instruction");
        }
    }
    REQUIRE(EvaluateVertex(block, {2, 4}, {3, 7, 5, 11}) == std::array<float, 2>{11, 39});
    REQUIRE(EvaluateVertex(block, {-4, 0.5f}, {-2, 4, 1, -3}) == std::array<float, 2>{9, -1});
}


TEST_CASE("Native lowering rejects unsupported forms without a partial executable", "[native-shader]") {
    auto code = VertexTransform;
    code[3].words[2] |= 1U << 12; // DNZ has no accepted lowering yet.
    auto result = Shader::Ampere::LowerToMaxwell(code);
    REQUIRE_FALSE(result);
    REQUIRE(result.error_offset == 3 * 16);
    REQUIRE(result.instructions.empty());
    code = VertexTransform;
    code[1].words[2] &= ~(15U << 8); // Partial quad-lane mask.
    result = Shader::Ampere::LowerToMaxwell(code);
    REQUIRE_FALSE(result);
    REQUIRE(result.instructions.empty());
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell({}));
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(std::span{VertexTransform}.first(6)));
}


TEST_CASE("Native instruction predicates survive lowering", "[native-shader]") {
    auto code = VertexTransform;
    code[3].words[0] = (code[3].words[0] & ~0xf000U) | 0x9000U;
    const auto result = Shader::Ampere::LowerToMaxwell(code);
    REQUIRE(result);
    const auto pred = Shader::Maxwell::Instruction{result.instructions[result.source_instruction_offsets[3]]}.Pred();
    REQUIRE(pred.index == 1);
    REQUIRE(pred.negated);
}


TEST_CASE("Captured native shader pair compiles through existing control flow and shader IR", "[.native-shader-captured]") {
    const char* directory = std::getenv("EDEN_NATIVE_SHADER_FIXTURES");
    REQUIRE(directory != nullptr);
    class FixtureEnvironment final : public EmptyEnvironment {
    public:
        FixtureEnvironment(const std::vector<u64>& code_, std::span<const u8> header, Shader::Stage stage_)
            : code{code_} {
            std::memcpy(&sph, header.data(), sizeof(sph));
            stage = stage_;
            start_address = 0;
            is_proprietary_driver = true;
        }
        u64 ReadInstruction(u32 address) override { return code.at(address / 8); }
        u32 TranslateProgramAddress(u32 address) const override {
            const auto translated = Shader::Ampere::NativeProgramAddress(code, address);
            if (!translated) throw std::logic_error("invalid private native branch target " + std::to_string(address));
            return *translated;
        }
        u32 LocalMemorySize() const override { return static_cast<u32>(sph.LocalMemorySize()) + sph.common3.shader_local_memory_crs_size; }
        u32 TextureBoundBuffer() const override { return texture_bank; }
        u32 ReadCbufValue(u32 bank, u32 offset) override {
            if (bank == 0 && !cbuf0.empty()) {
                REQUIRE(offset % 4 == 0);
                return cbuf0.at(offset / 4);
            }
            if (bank == 1 && !cbuf1.empty()) {
                REQUIRE(offset % 4 == 0);
                REQUIRE(offset / 4 < cbuf1.size());
                return cbuf1.at(offset / 4);
            }
            INFO("constant bank=" << bank << " offset=" << offset);
            if (const auto found = extra_cbufs.find(bank); found != extra_cbufs.end()) {
                REQUIRE(offset % 4 == 0);
                return found->second.at(offset / 4);
            }
            REQUIRE(bank == texture_bank);
            return 0; // Synthetic texture handle; only compilation is tested here.
        }
        Shader::TextureType ReadTextureType(u32) override { return Shader::TextureType::Color2D; }
        const std::vector<u64>& code;
        std::unordered_map<u32, std::vector<u32>> extra_cbufs;
        u32 texture_bank{2};
        std::vector<u32> cbuf0;
        std::vector<u32> cbuf1;
    };
    for (const int stage : {1, 5}) {
        const auto path = std::filesystem::path{directory} / ("native-graphics-shader-" + std::to_string(stage) + ".bin");
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        REQUIRE(file.good());
        const auto size = static_cast<size_t>(file.tellg());
        REQUIRE(size >= 0x90);
        std::vector<u8> bytes(size);
        file.seekg(0);
        file.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
        std::vector<Native> input((size - 0x80) / 16);
        std::memcpy(input.data(), bytes.data() + 0x80, input.size() * 16);
        const auto lowered = Shader::Ampere::LowerToMaxwell(input);
        INFO("stage=" << stage << " offset=" << lowered.error_offset << " error=" << lowered.error);
        REQUIRE(lowered);
        Shader::Ampere::ProgramCaptureState capture;
        u32 captured_size{};
        for (u32 i = 0; i < input.size(); ++i) {
            const auto status = capture.Observe(input[i], i);
            REQUIRE(status != Shader::Ampere::ProgramCaptureState::Result::InvalidTarget);
            if (status == Shader::Ampere::ProgramCaptureState::Result::Complete) {
                captured_size = (i + 1) * sizeof(Native);
                break;
            }
        }
        REQUIRE(captured_size == lowered.source_size);
        std::vector<u64> code(0x50 / 8);
        for (const u64 word : lowered.instructions) {
            if ((code.size() * 8) % 32 == 0) code.push_back(0);
            code.push_back(word);
        }
        code.push_back(0);
        Shader::Ampere::AppendNativeAddressMap(code, lowered.source_instruction_offsets);
        FixtureEnvironment env{code, std::span{bytes}.first(0x50), stage == 1 ? Shader::Stage::VertexB : Shader::Stage::Fragment};
        if (lowered.required_texture_buffer != ~0U) env.texture_bank = lowered.required_texture_buffer;
        auto constants_path = std::filesystem::path{directory} / ("cbuf1-" + std::to_string(stage) + ".bin");
        if (!std::filesystem::exists(constants_path)) constants_path = std::filesystem::path{directory} / "cbuf1.bin";
        std::ifstream constants(constants_path, std::ios::binary | std::ios::ate);
        if (constants) {
            env.cbuf1.resize(static_cast<size_t>(constants.tellg()) / 4);
            constants.seekg(0);
            constants.read(reinterpret_cast<char*>(env.cbuf1.data()), env.cbuf1.size() * 4);
        }
        auto roots_path = std::filesystem::path{directory} / ("cbuf0-" + std::to_string(stage) + ".bin");
        if (!std::filesystem::exists(roots_path)) roots_path = std::filesystem::path{directory} / "cbuf0.bin";
        std::ifstream roots(roots_path, std::ios::binary | std::ios::ate);
        if (roots) {
            env.cbuf0.resize(static_cast<size_t>(roots.tellg()) / 4);
            roots.seekg(0);
            roots.read(reinterpret_cast<char*>(env.cbuf0.data()), env.cbuf0.size() * 4);
        }
        for (u32 bank = 2; bank < Shader::Info::MAX_CBUFS; ++bank) {
            auto bank_path = std::filesystem::path{directory} / ("cbuf" + std::to_string(bank) + "-" + std::to_string(stage) + ".bin");
            if (!std::filesystem::exists(bank_path)) bank_path = std::filesystem::path{directory} / ("cbuf" + std::to_string(bank) + ".bin");
            std::ifstream bank_constants(bank_path, std::ios::binary | std::ios::ate);
            if (!bank_constants) continue;
            auto& words = env.extra_cbufs[bank];
            words.resize(static_cast<size_t>(bank_constants.tellg()) / 4);
            bank_constants.seekg(0);
            bank_constants.read(reinterpret_cast<char*>(words.data()), words.size() * 4);
        }
        Shader::ObjectPool<Shader::Maxwell::Flow::Block> flow_pool;
        Shader::ObjectPool<IR::Inst> inst_pool;
        Shader::ObjectPool<IR::Block> block_pool;
        Shader::Maxwell::Flow::CFG cfg{env, flow_pool, 0x50};
        Shader::HostTranslateInfo host;
        host.support_buffer_texture_handles = !env.cbuf0.empty();
        host.support_int64 = true;
        host.support_float64 = true;
        host.support_float16 = true;
        auto program = Shader::Maxwell::TranslateProgram(inst_pool, block_pool, env, cfg, host);
        if (const char* report = std::getenv("EDEN_NATIVE_DESCRIPTOR_REPORT")) {
            std::ofstream output(report, std::ios::app);
            for (const auto& descriptor : program.info.texture_descriptors) {
                if (!descriptor.buffer_source.enabled) continue;
                output << "stage=" << stage << " type=" << static_cast<u32>(descriptor.type)
                       << " query=" << descriptor.buffer_source.width_query
                       << " field=" << descriptor.buffer_source.offset
                       << " count=" << descriptor.count << '\n';
                for (const auto& guard : descriptor.buffer_source.exclusions) if (guard.mask)
                    output << " exclusion offset=" << guard.offset << " mask=" << guard.mask
                           << " value=" << guard.value << " not_equal=" << guard.not_equal << '\n';
            }
        }
        REQUIRE(program.stage == env.ShaderStage());
        REQUIRE_FALSE(program.blocks.empty());
        REQUIRE_FALSE(program.post_order_blocks.empty());
        if (std::getenv("EDEN_NATIVE_SPIRV_CHECK")) {
            Shader::Profile profile;
            profile.supported_spirv = 0x00010300;
            profile.support_int64 = true;
            const auto binary = Shader::Backend::SPIRV::EmitSPIRV(profile, program);
            REQUIRE(binary.size() > 5);
            REQUIRE(binary[0] == 0x07230203);
        }
    }
}


TEST_CASE("Native scalar texture sampling preserves coordinates and split outputs", "[native-shader]") {
    std::array<Native, 2> input{{
        {{0x04007b60, 0x30800805, 0x041e0f02, 0x100fc200}},
        VertexTransform.back(),
    }};
    const u32 binding = GENERATE(0U, 2U, 17U);
    input[0].words[1] = (input[0].words[1] & ~(31U << 22)) | (binding << 22);
    const bool predicated = GENERATE(false, true);
    if (predicated) input[0].words[0] = (input[0].words[0] & ~0xf000U) | 0x3000;
    const bool explicit_lod = GENERATE(false, true);
    if (explicit_lod) input[0].words[2] |= 1U << 23;
    const auto result = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(result);
    REQUIRE(result.required_texture_buffer == binding);
    REQUIRE(Shader::Maxwell::Instruction{result.instructions.front()}.Pred().index == (predicated ? 3U : 7U));
    REQUIRE_FALSE(Shader::Maxwell::Instruction{result.instructions.front()}.Pred().negated);
    REQUIRE(Shader::Maxwell::Decode(result.instructions.front()) == Shader::Maxwell::Opcode::TEXS);
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    EmptyEnvironment env;
    Shader::Maxwell::TranslatorVisitor visitor{env, block};
    visitor.TEXS(result.instructions.front());
    std::vector<u32> reads;
    std::vector<u32> writes;
    u32 samples = 0;
    for (const auto& inst : block.Instructions()) {
        if (inst.GetOpcode() == IR::Opcode::GetRegister) reads.push_back(static_cast<u32>(inst.Arg(0).Reg()));
        if (inst.GetOpcode() == IR::Opcode::SetRegister) writes.push_back(static_cast<u32>(inst.Arg(0).Reg()));
        if (inst.GetOpcode() == (explicit_lod ? IR::Opcode::BoundImageSampleExplicitLod : IR::Opcode::BoundImageSampleImplicitLod)) {
            ++samples;
            if (explicit_lod) REQUIRE(inst.Arg(2).F32() == 0.0f);
            const auto* coords = inst.Arg(1).Inst();
            REQUIRE(coords->GetOpcode() == IR::Opcode::CompositeConstructF32x2);
            for (size_t component = 0; component < 2; ++component) {
                const auto* cast = coords->Arg(component).Inst();
                REQUIRE(cast->GetOpcode() == IR::Opcode::BitCastF32U32);
                const auto* reg = cast->Arg(0).Inst();
                REQUIRE(reg->GetOpcode() == IR::Opcode::GetRegister);
                REQUIRE(static_cast<u32>(reg->Arg(0).Reg()) == 4 + component);
            }
            REQUIRE(inst.Arg(0).U32() == 0x20);
            REQUIRE(inst.Flags<IR::TextureInstInfo>().type == Shader::TextureType::Color2D);
        }
    }
    REQUIRE(samples == 1);
    std::ranges::sort(reads);
    REQUIRE(reads == std::vector<u32>{4, 5});
    REQUIRE(writes == std::vector<u32>{0, 1, 2, 3});
    // Reject unsupported LOD modes and sparse results.
    for (u32 bit : {89U, 81U}) {
        auto unsupported = input;
        unsupported[0].words[bit / 32] ^= 1U << (bit % 32);
        const auto rejected = Shader::Ampere::LowerToMaxwell(unsupported);
        REQUIRE_FALSE(rejected);
        REQUIRE(rejected.instructions.empty());
    }
    auto unsupported = input;
    unsupported[0].words[1] = (unsupported[0].words[1] & ~(31U << 22)) | (18U << 22);
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(unsupported));
}


TEST_CASE("Native texel fetch keeps integer coordinates and RGB outputs", "[native-shader]") {
    std::array<Native, 3> input{{
        {{0x00027305, 0x00000004, 0x0021f100, 0x001ff000}},
        {{0x02007b66, 0x30800a05, 0x049e0702, 0x101fc200}},
        VertexTransform.back(),
    }};
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(lowered);
    REQUIRE(lowered.required_texture_buffer == 2);
    REQUIRE(Shader::Maxwell::Decode(lowered.instructions[0]) == Shader::Maxwell::Opcode::F2I_reg);
    REQUIRE(Shader::Maxwell::Decode(lowered.instructions[1]) == Shader::Maxwell::Opcode::TLDS);
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    EmptyEnvironment env;
    Shader::Maxwell::TranslatorVisitor visitor{env, block};
    visitor.F2I_reg(lowered.instructions[0]);
    visitor.TLDS(lowered.instructions[1]);
    std::vector<u32> writes;
    u32 fetches = 0;
    bool truncates = false;
    bool handles_nan = false;
    for (const auto& inst : block.Instructions()) {
        if (inst.GetOpcode() == IR::Opcode::SetRegister) writes.push_back(static_cast<u32>(inst.Arg(0).Reg()));
        if (inst.GetOpcode() == IR::Opcode::FPTrunc32) {
            truncates = true;
            REQUIRE(inst.Flags<IR::FpControl>().fmz_mode == IR::FmzMode::FTZ);
        }
        if (inst.GetOpcode() == IR::Opcode::FPIsNan32) handles_nan = true;
        if (inst.GetOpcode() == IR::Opcode::BoundImageFetch) {
            ++fetches;
            REQUIRE(inst.Arg(0).U32() == 0x28);
            REQUIRE(inst.Flags<IR::TextureInstInfo>().type == Shader::TextureType::Color2D);
            const auto* coords = inst.Arg(1).Inst();
            REQUIRE(coords->GetOpcode() == IR::Opcode::CompositeConstructU32x2);
            REQUIRE(coords->Arg(0).Inst()->Arg(0).Reg() == IR::Reg{2});
            REQUIRE(coords->Arg(1).Inst()->Arg(0).Reg() == IR::Reg{5});
            REQUIRE(inst.Arg(3).U32() == 0);
        }
    }
    REQUIRE(fetches == 1);
    REQUIRE(truncates);
    REQUIRE(handles_nan);
    REQUIRE(writes == std::vector<u32>{2, 0, 1, 2});
    auto unsupported = input;
    unsupported[0].words[2] ^= 1U << 13; // NTZ is required by this conversion lowering.
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(unsupported));
    unsupported = input;
    unsupported[1].words[2] ^= 1U << 14; // multisample fetch is not implemented here.
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(unsupported));
}


TEST_CASE("Native texture component masks preserve sampled components", "[native-shader]") {
    for (const u32 mask : {1U, 2U, 4U, 8U, 3U, 9U, 10U, 12U, 7U, 11U, 13U, 14U, 15U}) {
        const bool split = std::popcount(mask) > 2;
        std::array<Native, 2> input{{
            {{0x04007b60, 0x30800805, 0x041e0f02, 0x100fc200}}, VertexTransform.back(),
        }};
        input[0].words[2] = (input[0].words[2] & ~0xfffU) | (split ? 6U : 255U) | (mask << 8);
        const auto lowered = Shader::Ampere::LowerToMaxwell(input);
        REQUIRE(lowered);
        Shader::ObjectPool<IR::Inst> pool;
        IR::Block block{pool};
        EmptyEnvironment env;
        Shader::Maxwell::TranslatorVisitor visitor{env, block};
        visitor.TEXS(lowered.instructions.front());
        std::vector<u32> extracted;
        std::vector<u32> writes;
        for (const auto& inst : block.Instructions()) {
            if (inst.GetOpcode() == IR::Opcode::CompositeExtractF32x4) extracted.push_back(inst.Arg(1).U32());
            if (inst.GetOpcode() == IR::Opcode::SetRegister) writes.push_back(static_cast<u32>(inst.Arg(0).Reg()));
        }
        std::vector<u32> expected;
        for (u32 component = 0; component < 4; ++component) if (mask & (1U << component)) expected.push_back(component);
        REQUIRE(extracted == expected);
        REQUIRE(writes.size() == expected.size());
        for (u32 i = 0; i < writes.size(); ++i) REQUIRE(writes[i] == (i < 2 ? i : i + 4));
    }
}


TEST_CASE("Native half packing and arithmetic preserve lane order numerically", "[native-shader]") {
    const std::array<Native, 15> input{{
        {{0x0102723e, 0x00000000, 0x000000ff, 0}}, // PACK_AB R2, R1, R0
        {{0x02027232, 0x00000002, 0x00000000, 0}}, // HMUL2 R2, R2, R2
        {{0xff037230, 0x20000002, 0x00004100, 0}}, // extract low half to F32 R3
        {{0xff047230, 0x30000002, 0x00004100, 0}}, // extract high half to F32 R4
        {{0xff05723e, 0x00000000, 0x00004002, 0}}, // MERGE_C R5, R0, R2
        {{0x02067230, 0x20000002, 0x00000800, 0}}, // add two low halves: (2,2)
        {{0x02077231, 0x20000002, 0x00040802, 0}}, // fused 1*1+1: (2,2)
        {{0x02087240, 0x30000002, 0x03800800, 0}}, // min of low/high: (1,1)
        {{0x02097840, 0x3c003c00, 0x07800c00, 0}}, // max of high/one: (4,4)
        {{0x000a7308, 0x00000008, 0x00001200, 0}}, // reciprocal of half one
        {{0x020b7816, 0x00005410, 0x00000007, 0}}, // concatenate low halves
        {{0xff0c7230, 0xa0000008, 0x00004100, 0}}, // extract negated low half
        {{0x020d7230, 0x30000002, 0x00000f00, 0}}, // -abs(high) + high: (0,0)
        {{0x020e7431, 0x3c003c00, 0x00400802, 0}}, // (1,1) * (1,-4) + (1,1)
        VertexTransform.back(),
    }};
    auto code = input;
    const u32 selector = GENERATE(0x5410U, 0x5432U, 0x7610U, 0x7632U);
    code[10].words[1] = selector;
    if (selector == 0x7610 || selector == 0x7632) code[10].words[2] = 2;
    const auto lowered = Shader::Ampere::LowerToMaxwell(code);
    REQUIRE(lowered);
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    EmptyEnvironment env;
    Shader::Maxwell::TranslatorVisitor visitor{env, block};
    for (const u64 word : lowered.instructions) {
        using Shader::Maxwell::Opcode;
        switch (Shader::Maxwell::Decode(word)) {
        case Opcode::F2F_reg: visitor.F2F_reg(word); break;
        case Opcode::IADD_reg: visitor.IADD_reg(word); break;
        case Opcode::ISCADD_reg: visitor.ISCADD_reg(word); break;
        case Opcode::HMUL2_reg: visitor.HMUL2_reg(word); break;
        case Opcode::HADD2_reg: visitor.HADD2_reg(word); break;
        case Opcode::HFMA2_reg: visitor.HFMA2_reg(word); break;
        case Opcode::FMNMX_reg: visitor.FMNMX_reg(word); break;
        case Opcode::MUFU: visitor.MUFU(word); break;
        case Opcode::MOV32I: visitor.MOV32I(word); break;
        case Opcode::SHR_imm: visitor.SHR_imm(word); break;
        case Opcode::LOP32I: visitor.LOP32I(word); break;
        case Opcode::MOV_reg: visitor.MOV_reg(word); break;
        case Opcode::EXIT: break;
        default: FAIL("Unexpected canonical half instruction");
        }
    }
    // Independent values with exact binary16 representations exercise packing,
    // multiplication and extraction through the actual emitted IR data flow.
    const std::array<std::pair<u16, float>, 5> halves{{{u16{0x4200}, 3.0f}, {u16{0}, 0.0f}, {u16{0x3c00}, 1.0f}, {u16{0x4000}, 2.0f}, {u16{0x4400}, 4.0f}}};
    const auto from_half = [&](u16 bits) {
        for (const auto& [encoded, value] : halves) if (encoded == (bits & 0x7fff)) return (bits & 0x8000) ? -value : value;
        throw std::logic_error("unexpected half value");
    };
    const auto to_half = [&](float value) {
        for (const auto& [encoded, reference] : halves) if (std::abs(value) == reference) return static_cast<u16>(encoded | (std::signbit(value) ? 0x8000 : 0));
        throw std::logic_error("unexpected float value");
    };
    std::array<u32, IR::NUM_REGS> regs{};
    regs[0] = std::bit_cast<u32>(1.0f);
    regs[1] = std::bit_cast<u32>(2.0f);
    std::unordered_map<const IR::Inst*, u64> values;
    const auto get = [&](const IR::Value& value) -> u64 {
        if (!value.IsImmediate()) return values.at(value.Inst());
        if (value.Type() == IR::Type::U1) return value.U1();
        if (value.Type() == IR::Type::F32) return std::bit_cast<u32>(value.F32());
        if (value.Type() == IR::Type::U16 || value.Type() == IR::Type::F16) return value.U16();
        return value.U32();
    };
    for (const auto& inst : block.Instructions()) {
        const auto arg = [&](size_t i) { return get(inst.Arg(i)); };
        switch (inst.GetOpcode()) {
        case IR::Opcode::GetRegister: values[&inst] = regs[static_cast<u32>(inst.Arg(0).Reg())]; break;
        case IR::Opcode::SetRegister: regs[static_cast<u32>(inst.Arg(0).Reg())] = static_cast<u32>(arg(1)); break;
        case IR::Opcode::BitCastF32U32:
        case IR::Opcode::BitCastU32F32: values[&inst] = arg(0); break;
        case IR::Opcode::ConvertF16F32: values[&inst] = to_half(std::bit_cast<float>(static_cast<u32>(arg(0)))); break;
        case IR::Opcode::ConvertF32F16: values[&inst] = std::bit_cast<u32>(from_half(static_cast<u16>(arg(0)))); break;
        case IR::Opcode::CompositeConstructF16x2: values[&inst] = arg(0) | (arg(1) << 32); break;
        case IR::Opcode::CompositeExtractF16x2: values[&inst] = (arg(0) >> (arg(1) * 32)) & 0xffff; break;
        case IR::Opcode::PackFloat2x16: values[&inst] = (arg(0) & 0xffff) | ((arg(0) >> 32) << 16); break;
        case IR::Opcode::UnpackFloat2x16: values[&inst] = (arg(0) & 0xffff) | ((arg(0) >> 16) << 32); break;
        case IR::Opcode::FPMul16: values[&inst] = to_half(from_half(static_cast<u16>(arg(0))) * from_half(static_cast<u16>(arg(1)))); break;
        case IR::Opcode::FPAdd16: values[&inst] = to_half(from_half(static_cast<u16>(arg(0))) + from_half(static_cast<u16>(arg(1)))); break;
        case IR::Opcode::FPFma16: values[&inst] = to_half(std::fma(from_half(static_cast<u16>(arg(0))), from_half(static_cast<u16>(arg(1))), from_half(static_cast<u16>(arg(2))))); break;
        case IR::Opcode::FPNeg16: values[&inst] = arg(0) ^ 0x8000; break;
        case IR::Opcode::FPAbs16: values[&inst] = arg(0) & 0x7fff; break;
        case IR::Opcode::FPAdd32: values[&inst] = std::bit_cast<u32>(std::bit_cast<float>(static_cast<u32>(arg(0))) + std::bit_cast<float>(static_cast<u32>(arg(1)))); break;
        case IR::Opcode::FPMin32: values[&inst] = std::bit_cast<u32>(std::fmin(std::bit_cast<float>(static_cast<u32>(arg(0))), std::bit_cast<float>(static_cast<u32>(arg(1))))); break;
        case IR::Opcode::FPMax32: values[&inst] = std::bit_cast<u32>(std::fmax(std::bit_cast<float>(static_cast<u32>(arg(0))), std::bit_cast<float>(static_cast<u32>(arg(1))))); break;
        case IR::Opcode::FPRecip32: values[&inst] = std::bit_cast<u32>(1.0f / std::bit_cast<float>(static_cast<u32>(arg(0)))); break;
        case IR::Opcode::SelectF32: values[&inst] = arg(0) ? arg(1) : arg(2); break;
        case IR::Opcode::BitwiseAnd32: values[&inst] = arg(0) & arg(1); break;
        case IR::Opcode::BitwiseXor32: values[&inst] = arg(0) ^ arg(1); break;

        case IR::Opcode::ShiftRightLogical32: values[&inst] = static_cast<u32>(arg(0)) >> arg(1); break;
        case IR::Opcode::SLessThan: values[&inst] = static_cast<s32>(arg(0)) < static_cast<s32>(arg(1)); break;
        case IR::Opcode::ULessThan: values[&inst] = static_cast<u32>(arg(0)) < static_cast<u32>(arg(1)); break;
        case IR::Opcode::SelectU32: values[&inst] = arg(0) ? arg(1) : arg(2); break;
        case IR::Opcode::IAdd32: values[&inst] = static_cast<u32>(arg(0) + arg(1)); break;
        case IR::Opcode::ShiftLeftLogical32: values[&inst] = static_cast<u32>(arg(0) << arg(1)); break;
        default: FAIL("Unexpected IR operation in half arithmetic");
        }
    }
    REQUIRE(regs[2] == 0x44003c00);
    REQUIRE(regs[5] == 0x3c003c00);
    REQUIRE(regs[6] == 0x40004000U);
    REQUIRE(regs[7] == 0x40004000U);
    REQUIRE(regs[8] == 0x3c003c00U);
    REQUIRE(regs[9] == 0x44004400U);
    REQUIRE(regs[10] == 0x3c00U);
    REQUIRE(regs[11] == (selector == 0x7632 ? 0x44004400U : selector == 0x7610 ? 0x44003c00U : selector == 0x5432 ? 0x40004400U : 0x40003c00U));
    REQUIRE(std::bit_cast<float>(regs[12]) == -1.0f);
    REQUIRE(regs[13] == 0U);
    REQUIRE(regs[14] == 0xc2004000U);

    REQUIRE(std::bit_cast<float>(regs[3]) == 1.0f);
    REQUIRE(std::bit_cast<float>(regs[4]) == 4.0f);
}


TEST_CASE("Live shader overrides require the exact original instructions", "[native-shader]") {
    const auto program = Shader::Ampere::LowerToMaxwell(VertexTransform);
    REQUIRE(program);
    const auto bytes = Shader::Ampere::EncodeProgramOverride(VertexTransform, program);
    REQUIRE_FALSE(bytes.empty());
    REQUIRE(Shader::Ampere::ProgramOverrideSourceSize(bytes) == VertexTransform.size() * 16);
    REQUIRE(Shader::Ampere::ProgramOverrideSourceSize(std::span{bytes}.first(12)) == 0);
    auto invalid_source = bytes;
    invalid_source[8] ^= 1;
    REQUIRE(Shader::Ampere::ProgramOverrideSourceSize(invalid_source) == 0);
    const auto decoded = Shader::Ampere::DecodeProgramOverride(bytes, VertexTransform);
    REQUIRE(decoded);
    REQUIRE(decoded.instructions == program.instructions);
    REQUIRE(decoded.source_instruction_offsets == program.source_instruction_offsets);
    auto invalid_map = bytes;
    const u32 invalid_target = static_cast<u32>(program.instructions.size());
    std::memcpy(invalid_map.data() + invalid_map.size() - program.source_instruction_offsets.size() * 4,
                &invalid_target, sizeof(invalid_target));
    const auto invalid_mapping = Shader::Ampere::DecodeProgramOverride(invalid_map, VertexTransform);
    REQUIRE_FALSE(invalid_mapping);
    REQUIRE(invalid_mapping.instructions.empty());

    REQUIRE(decoded.source_size == program.source_size);
    REQUIRE(decoded.required_texture_buffer == program.required_texture_buffer);
    auto different = VertexTransform;
    different[0].words[1] ^= 4;
    REQUIRE(Shader::Ampere::NativeProgramKey(different) != Shader::Ampere::NativeProgramKey(VertexTransform));
    const auto mismatch = Shader::Ampere::DecodeProgramOverride(bytes, different);
    REQUIRE_FALSE(mismatch);
    REQUIRE(mismatch.instructions.empty());
    REQUIRE_FALSE(Shader::Ampere::DecodeProgramOverride(std::span{bytes}.first(12), VertexTransform));
    REQUIRE_FALSE(Shader::Ampere::DecodeProgramOverride(std::span{bytes}.first(bytes.size() - 1), VertexTransform));
    auto wrong_version = bytes;
    wrong_version[4] ^= 1;
    REQUIRE_FALSE(Shader::Ampere::DecodeProgramOverride(wrong_version, VertexTransform));
    auto wrong_binding = bytes;
    const u32 bank = 18;
    std::memcpy(wrong_binding.data() + 16, &bank, sizeof(bank));
    REQUIRE_FALSE(Shader::Ampere::DecodeProgramOverride(wrong_binding, VertexTransform));
}


TEST_CASE("Native integer shader forms preserve signed arithmetic and immediate values", "[native-shader]") {
    // Synthetic vertex program: output (id + arithmetic_shift(id, 1), 4.0).
    const std::array<Native, 7> input{{
        {{0xff007321, 0x0002fcff, 0x00000000, 0x000e2200}},
        {{0xff017819, 0x00000001, 0x00011400, 0x000fe400}},
        {{0x00007824, 0x00000001, 0x078e0201, 0x000fe200}},
        {{0x00007245, 0x00000000, 0x00201400, 0x000fc600}},
        {{0xff017424, 0x40800000, 0x078e00ff, 0x000fe200}},
        {{0xff007322, 0x00007000, 0x000004ff, 0x000fe200}},
        VertexTransform.back(),
    }};
    auto program = input;
    const u32 mode = GENERATE(0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U);
    if (mode == 7) program[2] = Native{{0x00007217, 1, 0x03800200, 0}};
    if (mode == 8) program[2] = Native{{0x00007217, 1, 0x07800200, 0}};
    if (mode == 5) program[2] = Native{{0x00007210, 0x00000001, 0x07ffe1ff, 0}};
    if (mode == 6) program[2] = Native{{0x00007817, 0x0000003f, 0x07800200, 0}};
    if (mode == 4) program[2] = Native{{0x00007817, 0x0000003f, 0x03800200, 0}};
    if (mode == 1) program[2] = Native{{0x00007210, 0x00000001, 0x07ffe0ff, 0x000fe200}};
    if (mode == 3) program[2] = Native{{0x00007210, 0x80000001, 0x07ffe0ff, 0x000fe200}};
    if (mode == 2) program[1] = Native{{0x00017819, 0x00000001, 0x000006ff, 0x000fe200}};
    const auto lowered = Shader::Ampere::LowerToMaxwell(program);
    REQUIRE(lowered);
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    EmptyEnvironment env;
    Shader::Maxwell::TranslatorVisitor visitor{env, block};
    for (const u64 word : lowered.instructions) {
        using Shader::Maxwell::Opcode;
        switch (Shader::Maxwell::Decode(word)) {
        case Opcode::ALD: visitor.ALD(word); break;
        case Opcode::AST: visitor.AST(word); break;
        case Opcode::MOV32I: visitor.MOV32I(word); break;
        case Opcode::SHR_imm: visitor.SHR_imm(word); break;
        case Opcode::SHL_imm: visitor.SHL_imm(word); break;
        case Opcode::IMNMX_reg: visitor.IMNMX_reg(word); break;
        case Opcode::IMNMX_imm: visitor.IMNMX_imm(word); break;
        case Opcode::IADD_reg: visitor.IADD_reg(word); break;
        case Opcode::I2F_reg: visitor.I2F_reg(word); break;
        case Opcode::EXIT: break;
        default: FAIL("Unexpected lowered integer shader instruction");
        }
    }
    for (const s32 id : {0, 1, 2, -1, -3, 12345}) {
        REQUIRE(EvaluateVertex(block, {}, {}, id) ==
                std::array<float, 2>{static_cast<float>(mode == 2 ? id * 3 : mode == 3 ? id - (id >> 1) : mode == 4 ? std::min<s32>(id, 63) : mode == 5 ? -id + (id >> 1) : mode == 6 ? std::max<s32>(id, 63) : mode == 7 ? std::min<s32>(id, id >> 1) : mode == 8 ? std::max<s32>(id, id >> 1) : id + (id >> 1)), 4.0f});
    }
}


TEST_CASE("Native single-channel texel fetch writes only its selected result", "[native-shader]") {
    std::array<Native, 2> input{{
        {{0x04017b66, 0x30800805, 0x049e01ff, 0x001fc200}},
        VertexTransform.back(),
    }};
    const u32 component = GENERATE(0U, 1U, 2U, 3U);
    input[0].words[2] = (input[0].words[2] & ~0xf00U) | (1U << (8 + component));
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(lowered);
    REQUIRE(lowered.required_texture_buffer == 2);
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    EmptyEnvironment env;
    Shader::Maxwell::TranslatorVisitor visitor{env, block};
    visitor.TLDS(lowered.instructions.front());
    std::vector<u32> writes;
    u32 fetches = 0;
    for (const auto& inst : block.Instructions()) {
        if (inst.GetOpcode() == IR::Opcode::SetRegister)
            writes.push_back(static_cast<u32>(inst.Arg(0).Reg()));
        if (inst.GetOpcode() == IR::Opcode::BoundImageFetch) {
            ++fetches;
            REQUIRE(inst.Arg(0).U32() == 0x20);
            const auto* coords = inst.Arg(1).Inst();
            REQUIRE(coords->GetOpcode() == IR::Opcode::CompositeConstructU32x2);
            REQUIRE(coords->Arg(0).Inst()->Arg(0).Reg() == IR::Reg{4});
            REQUIRE(coords->Arg(1).Inst()->Arg(0).Reg() == IR::Reg{5});
        }
        if (inst.GetOpcode() == IR::Opcode::CompositeExtractF32x4)
            REQUIRE(inst.Arg(1).U32() == component);
    }
    REQUIRE(fetches == 1);
    REQUIRE(writes == std::vector<u32>{1});
}


TEST_CASE("Native ALU constants preserve high buffer slots and byte offsets", "[native-shader]") {
    const u32 bank = GENERATE(18U, 24U, 31U);
    const u32 offset = GENERATE(0U, 4U, 0xfffcU);
    std::array<Native, 2> input{{
        {{0x0d117a20, (bank << 22) | (offset << 6), 0x00410000, 0}},
        VertexTransform.back(),
    }};
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(lowered);
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    EmptyEnvironment env;
    Shader::Maxwell::TranslatorVisitor visitor{env, block};
    for (const u64 word : lowered.instructions) {
        using Shader::Maxwell::Opcode;
        switch (Shader::Maxwell::Decode(word)) {
        case Opcode::MOV_cbuf: visitor.MOV_cbuf(word); break;
        case Opcode::FMUL_reg: visitor.FMUL_reg(word); break;
        case Opcode::EXIT: break;
        default: FAIL("Unexpected high-slot constant instruction");
        }
    }
    u32 loads{};
    for (const auto& inst : block.Instructions()) {
        if (inst.GetOpcode() == IR::Opcode::GetCbufU32) {
            REQUIRE(inst.Arg(0).U32() == bank);
            REQUIRE(inst.Arg(1).U32() == offset);
            ++loads;
        }
    }
    REQUIRE(loads == 1);
    input[0].words[1] |= 1U << 6;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(input));
}


TEST_CASE("Native IMAD move loads the actual constant-buffer value", "[native-shader]") {
    const std::array<Native, 3> input{{
        {{0xff007624, 0x00c00100, 0x078e00ff, 0x000fe200}},
        {{0xff007322, 0x00007000, 0x000000ff, 0x000fe200}},
        VertexTransform.back(),
    }};
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(lowered);
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    EmptyEnvironment env;
    Shader::Maxwell::TranslatorVisitor visitor{env, block};
    REQUIRE(Shader::Maxwell::Decode(lowered.instructions.front()) == Shader::Maxwell::Opcode::MOV_cbuf);
    visitor.MOV_cbuf(lowered.instructions[0]);
    visitor.AST(lowered.instructions[1]);
    REQUIRE(EvaluateVertex(block, {}, {13, -2.5f, 99, 7}) == std::array<float, 2>{-2.5f, 0.0f});
    auto unsupported = input;
    unsupported[0].words[1] |= 1U << 30;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(unsupported));
}


TEST_CASE("Depth-stencil descriptors ignore the unused blue and alpha component types", "[native-gpu]") {
    using Tegra::Texture::ComponentType;
    using Tegra::Texture::TextureFormat;
    using VideoCore::Surface::PixelFormat;
    for (const auto unused : {ComponentType::UNORM, ComponentType::UINT}) {
        REQUIRE(VideoCommon::PixelFormatFromTextureInfo(TextureFormat::S8Z24,
            ComponentType::UNORM, ComponentType::UINT, unused, unused, false) ==
            PixelFormat::D24_UNORM_S8_UINT);
    }
}


TEST_CASE("Native floating min/max clamps actual shader input", "[native-shader]") {
    const std::array<Native, 5> input{{
        {{0xff007321, 0x000080ff, 0x00000000, 0x000e2200}},
        {{0x00007a09, 0x00c00000, 0x07810000, 0x000fe400}},
        {{0x00007a09, 0x00c00100, 0x03810000, 0x000fe400}},
        {{0xff007322, 0x00007000, 0x000000ff, 0x000fe200}},
        VertexTransform.back(),
    }};
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(lowered);
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    EmptyEnvironment env;
    Shader::Maxwell::TranslatorVisitor visitor{env, block};
    for (const auto word : lowered.instructions) {
        using Shader::Maxwell::Opcode;
        switch (Shader::Maxwell::Decode(word)) {
        case Opcode::ALD: visitor.ALD(word); break;
        case Opcode::AST: visitor.AST(word); break;
        case Opcode::MOV_cbuf: visitor.MOV_cbuf(word); break;
        case Opcode::FMNMX_reg: visitor.FMNMX_reg(word); break;
        case Opcode::EXIT: break;
        default: FAIL("Unexpected native min/max lowering");
        }
    }
    for (const float value : {-20.0f, -1.0f, 0.25f, 2.0f, 100.0f})
        REQUIRE(EvaluateVertex(block, {value, 0}, {-1, 2, 0, 0}) ==
                std::array<float, 2>{std::clamp(value, -1.0f, 2.0f), 0});
}


TEST_CASE("Native boolean float comparison preserves register moves and absolute inputs", "[native-shader]") {
    const std::array<Native, 5> input{{
        {{0xff007321, 0x000080ff, 0x00000000, 0}},
        {{0xff017224, 0x000000ff, 0x078e0000, 0}},
        {{0x01007a0a, 0x00c00000, 0x03811200, 0}},
        {{0xff007322, 0x00007000, 0x000000ff, 0}},
        VertexTransform.back(),
    }};
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(lowered);
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    EmptyEnvironment env;
    Shader::Maxwell::TranslatorVisitor visitor{env, block};
    for (const auto word : lowered.instructions) {
        using Shader::Maxwell::Opcode;
        switch (Shader::Maxwell::Decode(word)) {
        case Opcode::ALD: visitor.ALD(word); break;
        case Opcode::AST: visitor.AST(word); break;
        case Opcode::MOV_reg: visitor.MOV_reg(word); break;
        case Opcode::MOV_cbuf: visitor.MOV_cbuf(word); break;
        case Opcode::LOP32I: visitor.LOP32I(word); break;
        case Opcode::FSET_reg: visitor.FSET_reg(word); break;
        case Opcode::EXIT: break;
        default: FAIL("Unexpected native boolean float comparison lowering");
        }
    }
    for (const float value : {-3.0f, -1.0f, 0.0f, 2.0f, 4.0f})
        REQUIRE(EvaluateVertex(block, {value, 0}, {2, 0, 0, 0}) ==
                std::array<float, 2>{std::abs(value) < 2.0f ? 1.0f : 0.0f, 0});
}


TEST_CASE("Native unsigned conversion saturates, clears NaNs, and extracts the low byte", "[native-shader]") {
    std::array<Native, 5> input{{
        {{0xff007321, 0x000080ff, 0x00000000, 0}},
        {{0x00017305, 0x00000000, 0x0021f000, 0}},
        {{0x00007306, 0x00000001, 0x00001000, 0}},
        {{0xff007322, 0x00007000, 0x000000ff, 0}},
        VertexTransform.back(),
    }};
    const bool constant_source = GENERATE(false, true);
    if (constant_source) input[1] = Native{{0x00017b05, 0x00c00000, 0x0021f000, 0}};
    const u32 conversion_mode = GENERATE(0U, 1U, 2U);
    const bool short_round = conversion_mode == 1;
    if (conversion_mode == 2) input[1].words[2] = 0x00207000;
    if (short_round) input[1].words[2] = 0x00212800;
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(lowered);
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    EmptyEnvironment env;
    Shader::Maxwell::TranslatorVisitor visitor{env, block};
    for (const auto word : lowered.instructions) {
        using Shader::Maxwell::Opcode;
        switch (Shader::Maxwell::Decode(word)) {
        case Opcode::ALD: visitor.ALD(word); break;
        case Opcode::AST: visitor.AST(word); break;
        case Opcode::MOV_cbuf: visitor.MOV_cbuf(word); break;
        case Opcode::MOV32I: visitor.MOV32I(word); break;
        case Opcode::FSET_reg: visitor.FSET_reg(word); break;
        case Opcode::FMNMX_reg: visitor.FMNMX_reg(word); break;
        case Opcode::LOP_reg: visitor.LOP_reg(word); break;
        case Opcode::F2I_reg: visitor.F2I_reg(word); break;
        case Opcode::I2F_reg: visitor.I2F_reg(word); break;
        case Opcode::EXIT: break;
        default: FAIL("Unexpected unsigned conversion lowering");
        }
    }
    const std::array<std::pair<float, float>, 9> cases{{
        {std::numeric_limits<float>::quiet_NaN(), 0.0f},
        {-std::numeric_limits<float>::infinity(), 0.0f}, {-3.0f, 0.0f}, {1.9f, 1.0f},
        {300.75f, 44.0f}, {65535.0f, 255.0f}, {4294967040.0f, 0.0f},
        {4294967296.0f, 255.0f}, {std::numeric_limits<float>::infinity(), 255.0f},
    }};
    for (const auto& [value, expected] : cases) {
        const float rounded = std::isnan(value) ? 0.0f : static_cast<float>(static_cast<u32>(
            std::clamp(std::nearbyint(static_cast<double>(value)), 0.0, 65535.0)) & 255);
        REQUIRE(EvaluateVertex(block, {value, 0}, {value, 0, 0, 0}) == std::array<float, 2>{short_round ? rounded : expected, 0});
    }
}


TEST_CASE("Native depth-comparison sample preserves XY coordinates and reference depth", "[native-shader]") {
    const std::array<Native, 2> input{{
        {{0x04017b60, 0x30800806, 0x041e41ff, 0}},
        VertexTransform.back(),
    }};
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(lowered);
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    EmptyEnvironment env;
    Shader::Maxwell::TranslatorVisitor visitor{env, block};
    visitor.TEXS(lowered.instructions.front());
    u32 samples = 0;
    std::vector<u32> writes;
    for (const auto& inst : block.Instructions()) {
        if (inst.GetOpcode() == IR::Opcode::SetRegister)
            writes.push_back(static_cast<u32>(inst.Arg(0).Reg()));
        if (inst.GetOpcode() == IR::Opcode::BoundImageSampleDrefImplicitLod) {
            ++samples;
            REQUIRE(inst.Arg(0).U32() == 0x20);
            const auto* coords = inst.Arg(1).Inst();
            REQUIRE(coords->GetOpcode() == IR::Opcode::CompositeConstructF32x2);
            for (u32 i = 0; i < 2; ++i)
                REQUIRE(coords->Arg(i).Inst()->Arg(0).Inst()->Arg(0).Reg() == IR::Reg{4 + i});
            REQUIRE(inst.Arg(2).Inst()->Arg(0).Inst()->Arg(0).Reg() == IR::Reg{6});
            REQUIRE(inst.Flags<IR::TextureInstInfo>().is_depth == 1);
        }
    }
    REQUIRE(samples == 1);
    REQUIRE(writes == std::vector<u32>{1});
}


TEST_CASE("Native array texel fetch preserves layer and separates XY registers", "[native-shader]") {
    std::array<Native, 2> input{{
        {{0x06007b66, 0xb0800809, 0x009e0f02, 0}},
        VertexTransform.back(),
    }};
    const bool sparse = GENERATE(false, true);
    if (sparse) input[0].words[2] = 0x009e0b23;
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(lowered);
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    EmptyEnvironment env;
    Shader::Maxwell::TranslatorVisitor visitor{env, block};
    visitor.MOV_reg(lowered.instructions[0]);
    visitor.MOV_reg(lowered.instructions[1]);
    visitor.TLDS(lowered.instructions[2]);
    u32 fetches = 0;
    std::vector<u32> writes, components;
    std::vector<std::pair<u32, u32>> copies;
    for (const auto& inst : block.Instructions()) {
        if (inst.GetOpcode() == IR::Opcode::SetRegister && static_cast<u32>(inst.Arg(0).Reg()) < 250)
            writes.push_back(static_cast<u32>(inst.Arg(0).Reg()));
        if (inst.GetOpcode() == IR::Opcode::CompositeExtractF32x4)
            components.push_back(inst.Arg(1).U32());
        if (inst.GetOpcode() == IR::Opcode::SetRegister &&
            static_cast<u32>(inst.Arg(0).Reg()) >= 250) {
            REQUIRE(inst.Arg(1).Inst()->GetOpcode() == IR::Opcode::GetRegister);
            copies.emplace_back(static_cast<u32>(inst.Arg(0).Reg()),
                                static_cast<u32>(inst.Arg(1).Inst()->Arg(0).Reg()));
        }
        if (inst.GetOpcode() == IR::Opcode::BoundImageFetch) {
            ++fetches;
            REQUIRE(inst.Flags<IR::TextureInstInfo>().type == Shader::TextureType::ColorArray2D);
            const auto* coords = inst.Arg(1).Inst();
            REQUIRE(coords->GetOpcode() == IR::Opcode::CompositeConstructU32x3);
            REQUIRE(coords->Arg(0).Inst()->Arg(0).Reg() == IR::Reg{250});
            REQUIRE(coords->Arg(1).Inst()->Arg(0).Reg() == IR::Reg{251});
            const auto* layer = coords->Arg(2).Inst();
            REQUIRE(layer->GetOpcode() == IR::Opcode::BitFieldUExtract);
            REQUIRE(layer->Arg(0).Inst()->Arg(0).Reg() == IR::Reg{6});
            REQUIRE(layer->Arg(1).U32() == 0);
            REQUIRE(layer->Arg(2).U32() == 16);
        }
    }
    REQUIRE(fetches == 1);
    REQUIRE(writes == (sparse ? std::vector<u32>{0, 1, 35} : std::vector<u32>{0, 1, 2, 3}));
    REQUIRE(components == (sparse ? std::vector<u32>{0, 1, 3} : std::vector<u32>{0, 1, 2, 3}));
    REQUIRE(copies == std::vector<std::pair<u32, u32>>{{250, 7}, {251, 9}});
}


TEST_CASE("Native bitwise LUT forms preserve source bits without changing predicates", "[native-shader]") {
    std::array<Native, 5> input{{
        {{0x00007a02, 0x00c00000, 0x00000f00, 0}},
        {{0x00027a02, 0x00c00200, 0x00000f00, 0}},
        {{0x00007812, 0x80000000, 0x078ef802, 0}},
        {{0xff007322, 0x00007000, 0x000000ff, 0}},
        VertexTransform.back(),
    }};
    const u32 mode = GENERATE(0U, 1U, 2U, 3U);
    if (mode == 3) input[2] = Native{{0x00007812, 0x80000000, 0x078efe02, 0}};
    if (mode == 2) input[2] = Native{{0x00007812, 0x80000000, 0x078ee202, 0}};
    if (mode == 1) input[2] = Native{{0x00007212, 0x00000002, 0x078efcff, 0}};
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(lowered);
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    EmptyEnvironment env;
    Shader::Maxwell::TranslatorVisitor visitor{env, block};
    for (const auto word : lowered.instructions) {
        using Shader::Maxwell::Opcode;
        switch (Shader::Maxwell::Decode(word)) {
        case Opcode::AST: visitor.AST(word); break;
        case Opcode::MOV_cbuf: visitor.MOV_cbuf(word); break;
        case Opcode::MOV32I: visitor.MOV32I(word); break;
        case Opcode::LOP_reg: visitor.LOP_reg(word); break;
        case Opcode::EXIT: break;
        default: FAIL("Unexpected native bitwise LUT lowering");
        }
    }
    for (const auto& inst : block.Instructions()) REQUIRE(inst.GetOpcode() != IR::Opcode::SetPred);
    for (const float a : {-1.0f, 1.0f}) for (const float c : {-2.0f, 2.0f}) {
        const u32 c_bits = std::bit_cast<u32>(c);
        const u32 a_bits = std::bit_cast<u32>(a);
        const u32 expected = mode == 3 ? a_bits | c_bits | 0x80000000U : mode == 2 ? (a_bits & 0x80000000U) | (c_bits & 0x7fffffffU) :
            a_bits | (mode == 1 ? c_bits : c_bits & 0x80000000U);
        REQUIRE(EvaluateVertex(block, {}, {a, 0, c, 0}) ==
                std::array<float, 2>{std::bit_cast<float>(expected), 0});
    }
}


TEST_CASE("Native texture gathers preserve coordinates component and split result pairs", "[native-shader]") {
    const u32 binding = GENERATE(0U, 5U, 17U);
    const u32 component = GENERATE(0U, 1U, 2U, 3U);
    const std::array<Native, 2> input{{
        {{0x04047b63, 0x30000703U | (binding << 22), 0x041e0f06U | (component << 23), 0}},
        VertexTransform.back(),
    }};
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(lowered);
    REQUIRE(lowered.required_texture_buffer == binding);
    REQUIRE(Shader::Maxwell::Decode(lowered.instructions.front()) == Shader::Maxwell::Opcode::TLD4S);
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    EmptyEnvironment env;
    Shader::Maxwell::TranslatorVisitor visitor{env, block};
    visitor.TLD4S(lowered.instructions.front());
    u32 gathers{};
    std::vector<u32> writes, channels;
    for (const auto& inst : block.Instructions()) {
        if (inst.GetOpcode() == IR::Opcode::BoundImageGather) {
            ++gathers;
            REQUIRE(inst.Arg(0).U32() == 28);
            REQUIRE(inst.Flags<IR::TextureInstInfo>().type.Value() == Shader::TextureType::Color2D);
            REQUIRE(inst.Flags<IR::TextureInstInfo>().gather_component.Value() == component);
            const auto* coords = inst.Arg(1).Inst();
            REQUIRE(coords->Arg(0).Inst()->Arg(0).Inst()->Arg(0).Reg() == IR::Reg{4});
            REQUIRE(coords->Arg(1).Inst()->Arg(0).Inst()->Arg(0).Reg() == IR::Reg{3});
        }
        if (inst.GetOpcode() == IR::Opcode::SetRegister) writes.push_back(static_cast<u32>(inst.Arg(0).Reg()));
        if (inst.GetOpcode() == IR::Opcode::CompositeExtractF32x4) channels.push_back(inst.Arg(1).U32());
    }
    REQUIRE(gathers == 1);
    REQUIRE(writes == std::vector<u32>{4, 5, 6, 7});
    REQUIRE(channels == std::vector<u32>{0, 1, 2, 3});
    auto invalid = input;
    invalid[0].words[2] |= 1U << 12;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(invalid));
}


TEST_CASE("Native array image stores retain a descriptor bank distinct from texture gathers", "[native-shader]") {
    const std::array<Native, 3> input{{
        {{0x04047b63, 0x31400003, 0x041e0f06, 0}},
        {{0x00007b9b, 0x8080480c, 0x00100f00, 0}},
        VertexTransform.back(),
    }};
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(lowered);
    REQUIRE(lowered.required_texture_buffer == 5);
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    EmptyEnvironment env;
    Shader::Maxwell::TranslatorVisitor visitor{env, block};
    visitor.TLD4S(lowered.instructions[0]);
    REQUIRE(Shader::Maxwell::Decode(lowered.instructions[1]) == Shader::Maxwell::Opcode::MOV_cbuf);
    visitor.MOV_cbuf(lowered.instructions[1]);
    visitor.SUST(lowered.instructions[2]);
    u32 descriptors{}, stores{};
    for (const auto& inst : block.Instructions()) {
        if (inst.GetOpcode() == IR::Opcode::GetCbufU32) {
            ++descriptors;
            REQUIRE(inst.Arg(0).U32() == 2);
            REQUIRE(inst.Arg(1).U32() == 0x120);
        }
        if (inst.GetOpcode() == IR::Opcode::BindlessImageWrite) {
            ++stores;
            REQUIRE(inst.Flags<IR::TextureInstInfo>().type.Value() == Shader::TextureType::ColorArray2D);
            REQUIRE(inst.Arg(0).Inst()->Arg(0).Reg() == IR::Reg{250});
            REQUIRE(inst.Arg(1).Inst()->GetOpcode() == IR::Opcode::CompositeConstructU32x3);
            const auto* color = inst.Arg(2).Inst();
            for (u32 i = 0; i < 4; ++i) REQUIRE(color->Arg(i).Inst()->Arg(0).Reg() == IR::Reg{12 + i});
        }
    }
    REQUIRE(descriptors == 1);
    REQUIRE(stores == 1);
}


TEST_CASE("Native one-dimensional texel fetch preserves its integer coordinate", "[native-shader]") {
    const std::array<Native, 2> input{{
        {{0x04017b66, 0x108008ff, 0x009e01ff, 0}},
        VertexTransform.back(),
    }};
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(lowered);
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    EmptyEnvironment env;
    Shader::Maxwell::TranslatorVisitor visitor{env, block};
    visitor.TLDS(lowered.instructions.front());
    u32 fetches = 0;
    for (const auto& inst : block.Instructions()) {
        if (inst.GetOpcode() == IR::Opcode::BoundImageFetch) {
            ++fetches;
            REQUIRE(inst.Flags<IR::TextureInstInfo>().type == Shader::TextureType::Color1D);
            REQUIRE(inst.Arg(1).Inst()->GetOpcode() == IR::Opcode::GetRegister);
            REQUIRE(inst.Arg(1).Inst()->Arg(0).Reg() == IR::Reg{4});
        }
    }
    REQUIRE(fetches == 1);
}


TEST_CASE("Native scalar attribute stores accept the zero register without wrapping", "[native-shader]") {
    const std::array<Native, 2> input{{
        {{0xff007322, 0x000070ff, 0x000000ff, 0}},
        VertexTransform.back(),
    }};
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(lowered);
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    EmptyEnvironment env;
    Shader::Maxwell::TranslatorVisitor visitor{env, block};
    visitor.AST(lowered.instructions.front());
    u32 stores = 0;
    for (const auto& instruction : block.Instructions()) {
        if (instruction.GetOpcode() != IR::Opcode::SetAttribute) continue;
        ++stores;
        REQUIRE(instruction.Arg(0).Attribute() == IR::Attribute::PositionX);
    }
    REQUIRE(stores == 1);
    REQUIRE(EvaluateVertex(block, {8.0f, 9.0f}, {}) == std::array<float, 2>{0.0f, 0.0f});
    auto broken = input;
    broken[0].words[2] |= 1U << 10;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(broken));
}


TEST_CASE("Native call and return preserve values through the actual shader frontend", "[native-shader]") {
    std::array<Native, 9> input{{
        {{0x00007802, 0x40000000, 0x00000f00, 0}}, // argument 2.0
        {{0x00377802, 0x00000030, 0x00000f00, 0}}, // native return PC
        {{0x00007944, 0x00000020, 0x03c00000, 0}}, // call offset0x50
        {{0xff007322, 0x00007003, 0x000000ff, 0}},
        VertexTransform.back(),
        {{0x00037421, 0x40400000, 0x00010000, 0}}, // callee: add 3.0
        {{0x00087202, 0x00000037, 0x00000f00, 0}},
        {{0x00097802, 0x00000000, 0x00000f00, 0}},
        {{0x08007950, 0xffffff70, 0x03c3ffff, 0}},
    }};
    const bool alternate_link = GENERATE(false, true);
    if (alternate_link) {
        input[1].words[0] = 0x00397802;
        input[6] = Native{{0x00007202, 0x00000039, 0x00000f00, 0}};
        input[7].words[0] = 0x00017802;
        input[8].words[0] = 0x00007950;
    }
    const bool inplace = GENERATE(false, true);
    if (inplace) {
        const u32 link = alternate_link ? 10U : 8U;
        input[1].words[0] = 0x7802U | (link << 16);
        input[6] = Native{{0x7947, 0, 0x03800000, 0}};
        input[7].words[0] = 0x7802U | ((link + 1) << 16);
        input[8].words[0] = 0x7950U | (link << 24);
    }
    const bool synchronize = GENERATE(false, true);
    std::vector<Native> program_input(input.begin(), input.end());
    if (synchronize) {
        program_input.insert(program_input.begin(), Native{{0x27945, 64, 0x03800000, 0}});
        program_input.insert(program_input.begin() + 4, Native{{0x27941, 0, 0x03800000, 0}});
        program_input.insert(program_input.begin() + 5, Native{{0x7948, 0xffffffff, 0x03800000, 0}});
        program_input[2].words[1] = 64;
        program_input[3].words[1] = 64;
        program_input.back().words[1] = 0xffffff40;
    }
    const auto lowered = Shader::Ampere::LowerToMaxwell(program_input);
    REQUIRE(lowered);
    REQUIRE(lowered.source_size == program_input.size() * 16);
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(std::span{input}.first(5)));
    auto broken = input;
    broken[1].words[1] += 16;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(broken));
    broken = input;
    broken.back().words[1] += 16;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(broken));
    if (inplace) {
        auto corrupt = input;
        corrupt[5].words[0] = (corrupt[5].words[0] & ~0xff0000U) | (input[1].words[0] & 0xff0000U);
        REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(corrupt));
        corrupt = input;
        corrupt[6].words[1] = 16;
        REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(corrupt));
    }
    std::vector<u64> code(0x50 / 8);
    for (const auto word : lowered.instructions) {
        if ((code.size() * 8) % 32 == 0) code.push_back(0);
        code.push_back(word);
    }
    class CallEnvironment final : public EmptyEnvironment {
    public:
        explicit CallEnvironment(const std::vector<u64>& code_) : code{code_} { stage = Shader::Stage::VertexB; }
        u64 ReadInstruction(u32 address) override { return code.at(address / 8); }
        const std::vector<u64>& code;
    } env{code};
    Shader::ObjectPool<Shader::Maxwell::Flow::Block> flow_pool;
    Shader::ObjectPool<IR::Inst> inst_pool;
    Shader::ObjectPool<IR::Block> block_pool;
    Shader::Maxwell::Flow::CFG cfg{env, flow_pool, 0x50};
    REQUIRE(cfg.Functions().size() == 2);
    Shader::HostTranslateInfo host;
    const auto program = Shader::Maxwell::TranslateProgram(inst_pool, block_pool, env, cfg, host);
    u32 stores = 0;
    for (const auto* block : program.blocks) {
        for (const auto& inst : block->Instructions()) {
            if (inst.GetOpcode() != IR::Opcode::SetAttribute) continue;
            ++stores;
            REQUIRE(inst.Arg(1).IsImmediate());
            REQUIRE(inst.Arg(1).F32() == 5.0f);
        }
    }
    REQUIRE(stores == 1);
}


TEST_CASE("Native barrier cancellation exits the inner scope without revisiting it", "[native-shader]") {
    std::array<Native, 10> input{{
        {{0x7945, 128, 0x03800000, 0}},
        {{0x17945, 80, 0x03800000, 0}},
        {{0x18942, 0, 0x03800000, 0}},
        {{0xff028424, 1, 0x078e00ff, 0}},
        {{0x8947, 48, 0x03800000, 0}},
        {{0x27802, 2, 0xf00, 0}},
        {{0x17941, 0, 0x03800000, 0}},
        {{0x27802, 3, 0xf00, 0}},
        {{0x7941, 0, 0x03800000, 0}},
        VertexTransform.back(),
    }};
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(lowered);
    auto broken = input;
    broken[3].words[0] ^= 0x8000;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(broken));
    broken = input;
    broken[4].words[1] = 32;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(broken));
    broken = input;
    broken[2].words[0] = 0x8942;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(broken));
    std::vector<u64> code(0x50 / 8);
    for (const auto word : lowered.instructions) {
        if ((code.size() * 8) % 32 == 0) code.push_back(0);
        code.push_back(word);
    }
    class BarrierEnvironment final : public EmptyEnvironment {
    public:
        explicit BarrierEnvironment(const std::vector<u64>& code_) : code{code_} { stage = Shader::Stage::VertexB; }
        u64 ReadInstruction(u32 address) override { return code.at(address / 8); }
        const std::vector<u64>& code;
    } env{code};
    Shader::ObjectPool<Shader::Maxwell::Flow::Block> flow_pool;
    Shader::Maxwell::Flow::CFG cfg{env, flow_pool, 0x50};
    INFO(cfg.Dot());
    const u32 offset = lowered.source_instruction_offsets[7];
    const Shader::Maxwell::Location continuation{0x50 + offset * 8 + ((offset + 1) / 3) * 8};
    for (bool predicate : {false, true}) {
        const auto* block = &*cfg.Functions().front().blocks.begin();
        bool visited_continuation = false, exited = false;
        std::string trace;
        for (u32 step = 0; step < 64 && block; ++step) {
            trace += std::to_string(block->begin.Offset()) + ":" + std::to_string(block->end.Offset()) + " ";
            visited_continuation |= !block->begin.IsVirtual() && block->Contains(continuation);
            if (block->end_class == Shader::Maxwell::Flow::EndClass::Exit) { exited = true; break; }
            const auto [pred, inverted] = block->cond.GetPred();
            REQUIRE(block->cond.GetFlowTest() == IR::FlowTest::T);
            REQUIRE((pred == IR::Pred::P0 || pred == IR::Pred::PT));
            const bool taken = (pred == IR::Pred::PT || predicate) != inverted;
            block = taken ? block->branch_true : block->branch_false;
        }
        REQUIRE(exited);
        INFO("path=" << trace << " marker=" << continuation.Offset());
        REQUIRE(visited_continuation == predicate);
    }
}


TEST_CASE("Native cancellation of two nested barriers preserves the outer join", "[native-shader]") {
    const std::array<Native, 15> input{{
        {{0x27945, 208, 0x03800000, 0}},
        {{0x7945, 160, 0x03800000, 0}},
        {{0x17945, 96, 0x03800000, 0}},
        {{0x18942, 0, 0x03800000, 0}},
        {{0x8942, 0, 0x03800000, 0}},
        {{0x8947, 96, 0x03800000, 0}},
        {{0x27802, 2, 0xf00, 0}},
        {{0x7918, 0, 0, 0}},
        {{0x17941, 0, 0x03800000, 0}},
        {{0x27802, 3, 0xf00, 0}},
        {{0x7918, 0, 0, 0}},
        {{0x7941, 0, 0x03800000, 0}},
        {{0x37802, 4, 0xf00, 0}},
        {{0x27941, 0, 0x03800000, 0}},
        VertexTransform.back(),
    }};
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(lowered);
    auto invalid = input;
    invalid[4].words[0] ^= 0x8000;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(invalid));
    invalid = input;
    invalid[5].words[1] = 16;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(invalid));
    std::vector<u64> code(0x50 / 8);
    for (const auto word : lowered.instructions) {
        if ((code.size() * 8) % 32 == 0) code.push_back(0);
        code.push_back(word);
    }
    class NestedBarrierEnvironment final : public EmptyEnvironment {
    public:
        explicit NestedBarrierEnvironment(const std::vector<u64>& code_) : code{code_} { stage = Shader::Stage::VertexB; }
        u64 ReadInstruction(u32 address) override { return code.at(address / 8); }
        const std::vector<u64>& code;
    } env{code};
    Shader::ObjectPool<Shader::Maxwell::Flow::Block> flow_pool;
    Shader::Maxwell::Flow::CFG cfg{env, flow_pool, 0x50};
    INFO(cfg.Dot());
    const auto location = [&](u32 index) {
        const u32 offset = lowered.source_instruction_offsets[index];
        return Shader::Maxwell::Location{0x50 + offset * 8 + ((offset + 1) / 3) * 8};
    };
    for (bool predicate : {false, true}) {
        const auto* block = &*cfg.Functions().front().blocks.begin();
        bool continued{}, cleaned{}, exited{};
        for (u32 step = 0; step < 64 && block; ++step) {
            continued |= !block->begin.IsVirtual() && block->Contains(location(9));
            cleaned |= !block->begin.IsVirtual() && block->Contains(location(12));
            if (block->end_class == Shader::Maxwell::Flow::EndClass::Exit) { exited = true; break; }
            const auto [pred, inverted] = block->cond.GetPred();
            REQUIRE(block->cond.GetFlowTest() == IR::FlowTest::T);
            REQUIRE((pred == IR::Pred::P0 || pred == IR::Pred::PT));
            block = ((pred == IR::Pred::PT || predicate) != inverted) ? block->branch_true : block->branch_false;
        }
        REQUIRE(exited);
        REQUIRE(cleaned);
        REQUIRE(continued == predicate);
    }
}


TEST_CASE("Native full warp synchronization retains ordering after reconvergence", "[native-shader]") {
    std::array<Native, 4> input{{
        {{0x7945, 16, 0x03800000, 0}},
        {{0x7941, 0, 0x03800000, 0}},
        {{0x7948, 0xffffffff, 0x03800000, 0}},
        VertexTransform.back(),
    }};
    const u32 token = GENERATE(0U, 2U, 7U);
    input[0].words[0] |= token << 16;
    input[1].words[0] |= token << 16;
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(lowered);
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    EmptyEnvironment env;
    Shader::Maxwell::TranslatorVisitor visitor{env, block};
    visitor.MEMBAR(lowered.instructions.at(lowered.source_instruction_offsets[2]));
    REQUIRE(block.Instructions().front().GetOpcode() == IR::Opcode::DeviceMemoryBarrier);
    auto broken = input;
    broken[2].words[1] = 1;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(broken));
    broken = input;
    broken[0].words[1] = 32;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(broken));
    broken = input;
    broken[1].words[0] = 0x7947;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(broken));
}


TEST_CASE("Native EXIT retains its execution and branch predicates", "[native-shader]") {
    const u32 execution = GENERATE(7U, 9U);
    const std::array<Native, 3> input{{
        {{0x94dU | (execution << 12), 0, 0, 0}},
        {{0x27802, 1, 0xf00, 0}},
        VertexTransform.back(),
    }};
    Shader::Ampere::ProgramCaptureState capture;
    REQUIRE(capture.Observe(input[0], 0) == Shader::Ampere::ProgramCaptureState::Result::Continue);
    REQUIRE(capture.Observe(input[1], 1) == Shader::Ampere::ProgramCaptureState::Result::Continue);
    REQUIRE(capture.Observe(input[2], 2) == Shader::Ampere::ProgramCaptureState::Result::Complete);
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(lowered);
    REQUIRE(lowered.source_size == sizeof(input));
    std::vector<u64> code(0x50 / 8);
    for (const auto word : lowered.instructions) {
        if ((code.size() * 8) % 32 == 0) code.push_back(0);
        code.push_back(word);
    }
    class ExitEnvironment final : public EmptyEnvironment {
    public:
        explicit ExitEnvironment(const std::vector<u64>& code_) : code{code_} { stage = Shader::Stage::VertexB; }
        u64 ReadInstruction(u32 address) override { return code.at(address / 8); }
        const std::vector<u64>& code;
    } env{code};
    Shader::ObjectPool<Shader::Maxwell::Flow::Block> flow_pool;
    Shader::Maxwell::Flow::CFG cfg{env, flow_pool, 0x50};
    const u32 offset = lowered.source_instruction_offsets[1];
    const Shader::Maxwell::Location continuation{0x50 + offset * 8 + ((offset + 1) / 3) * 8};
    for (bool p0 : {false, true}) for (bool p1 : {false, true}) {
        const auto* block = &*cfg.Functions().front().blocks.begin();
        bool continued = false, exited = false;
        for (u32 step = 0; step < 32 && block; ++step) {
            continued |= !block->begin.IsVirtual() && block->Contains(continuation);
            if (block->end_class == Shader::Maxwell::Flow::EndClass::Exit) { exited = true; break; }
            const auto [pred, inverted] = block->cond.GetPred();
            REQUIRE(block->cond.GetFlowTest() == IR::FlowTest::T);
            REQUIRE((pred == IR::Pred::P0 || pred == IR::Pred::P1 || pred == IR::Pred::PT));
            const bool taken = (pred == IR::Pred::PT ? true : pred == IR::Pred::P0 ? p0 : p1) != inverted;
            block = taken ? block->branch_true : block->branch_false;
        }
        REQUIRE(exited);
        REQUIRE(continued == !(p0 && (execution == 7 || !p1)));
    }
}


TEST_CASE("Native reconvergence register names are independent of nesting depth", "[native-shader]") {
    const u32 outer = GENERATE(1U, 4U), inner = GENERATE(0U, 2U);
    const std::array<Native, 8> input{{
        {{0x27802, 0x3f800000, 0xf00, 0}},
        {{0x7945U | (outer << 16), 64, 0x03800000, 0}},
        {{0x7945U | (inner << 16), 16, 0x03800000, 0}},
        {{0x7941U | (inner << 16), 0, 0x03800000, 0}},
        {{0x27802, 0x40000000, 0xf00, 0}},
        {{0x7941U | (outer << 16), 0, 0x03800000, 0}},
        {{0xff007322, 0x7002, 0xff, 0}},
        VertexTransform.back(),
    }};
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(lowered);
    auto broken = input;
    broken[2].words[0] = 0x7945U | (outer << 16);
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(broken));
    broken = input;
    broken[3].words[0] = 0x7941U | (outer << 16);
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(broken));
    std::vector<u64> code(0x50 / 8);
    for (const auto word : lowered.instructions) {
        if ((code.size() * 8) % 32 == 0) code.push_back(0);
        code.push_back(word);
    }
    class ScopeEnvironment final : public EmptyEnvironment {
    public:
        explicit ScopeEnvironment(const std::vector<u64>& code_) : code{code_} { stage = Shader::Stage::VertexB; }
        u64 ReadInstruction(u32 address) override { return code.at(address / 8); }
        const std::vector<u64>& code;
    } env{code};
    Shader::ObjectPool<Shader::Maxwell::Flow::Block> flow_pool;
    Shader::ObjectPool<IR::Inst> inst_pool;
    Shader::ObjectPool<IR::Block> block_pool;
    Shader::Maxwell::Flow::CFG cfg{env, flow_pool, 0x50};
    Shader::HostTranslateInfo host;
    const auto program = Shader::Maxwell::TranslateProgram(inst_pool, block_pool, env, cfg, host);
    u32 stores = 0;
    for (const auto* block : program.blocks) for (const auto& instruction : block->Instructions()) {
        if (instruction.GetOpcode() != IR::Opcode::SetAttribute) continue;
        ++stores;
        REQUIRE(instruction.Arg(1).IsImmediate());
        REQUIRE(instruction.Arg(1).F32() == 2.0f);
    }
    REQUIRE(stores == 1);
}


TEST_CASE("Native scaled multiplication preserves captured power-of-two scales", "[native-shader]") {
    std::array<Native, 4> input{{
        VertexTransform.front(),
        {{0x00007220, 0x00000001, 0x00310000, 0}},
        {{0xff007322, 0x00007000, 0x000000ff, 0}},
        VertexTransform.back(),
    }};
    const u32 scale = GENERATE(1U, 2U, 3U, 4U, 5U, 6U, 7U);
    input[1].words[2] = (scale << 20) | 0x10000;
    const float factor = std::ldexp(1.0f, static_cast<int>(scale) - 4);
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(lowered);
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    EmptyEnvironment env;
    Shader::Maxwell::TranslatorVisitor visitor{env, block};
    visitor.ALD(lowered.instructions[0]);
    visitor.FMUL_reg(lowered.instructions[1]);
    visitor.AST(lowered.instructions[2]);
    REQUIRE(EvaluateVertex(block, {8, 4}, {}) == std::array<float, 2>{32 * factor, 0});
    REQUIRE(EvaluateVertex(block, {-2, 3}, {}) == std::array<float, 2>{-6 * factor, 0});
}


TEST_CASE("Native integer multiply preserves high words and modular multiply-add", "[native-shader]") {
    const u32 mode = GENERATE(0U, 1U, 2U, 3U, 4U, 5U);
    Native instruction{{0x00037827, 0xcccccccd, 0x078e00ff, 0}};
    if (mode == 1) instruction = Native{{0x00037824, 0xfffffff6, 0x078e0202, 0}};
    if (mode == 2) instruction = Native{{0x00037a24, 0x00c00000, 0x078e0202, 0}};
    if (mode == 4) instruction = Native{{0x00037811, 0x00000070, 0x078e20ff, 0}};
    if (mode == 3) instruction = Native{{0x00037224, 0x00000001, 0x078e0202, 0}};
    if (mode == 5) instruction = Native{{0x00037824, 0x00000001, 0x078e0a02, 0}};
    const std::array<Native, 2> input{{instruction, VertexTransform.back()}};
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(lowered);
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    EmptyEnvironment env;
    Shader::Maxwell::TranslatorVisitor visitor{env, block};
    for (const auto word : lowered.instructions) {
        using Shader::Maxwell::Opcode;
        switch (Shader::Maxwell::Decode(word)) {
        case Opcode::XMAD_imm: visitor.XMAD_imm(word); break;
        case Opcode::XMAD_reg: visitor.XMAD_reg(word); break;
        case Opcode::MOV32I: visitor.MOV32I(word); break;
        case Opcode::MOV_cbuf: visitor.MOV_cbuf(word); break;
        case Opcode::ISCADD_reg: visitor.ISCADD_reg(word); break;
        case Opcode::SHR_imm: visitor.SHR_imm(word); break;
        case Opcode::IADD_reg: visitor.IADD_reg(word); break;
        case Opcode::EXIT: break;
        default: FAIL("Unexpected canonical integer multiplication instruction");
        }
    }
    for (u32 a : {0U, 1U, 9U, 10U, 65535U, 65536U, 0x80000000U, 0xfffffffeU, 0xffffffffU, 0x12345678U}) {
        std::array<u32, IR::NUM_REGS> regs{};
        regs[0] = a;
        regs[1] = 0x76543210;
        regs[2] = 0xfedcba98;
        std::unordered_map<const IR::Inst*, u32> values;
        const auto get = [&](const IR::Value& value) -> u32 {
            if (!value.IsImmediate()) return values.at(value.Inst());
            return value.Type() == IR::Type::U1 ? static_cast<u32>(value.U1()) : value.U32();
        };
        for (const auto& inst : block.Instructions()) {
            const auto arg = [&](size_t i) { return get(inst.Arg(i)); };
            switch (inst.GetOpcode()) {
            case IR::Opcode::GetRegister: values[&inst] = regs[static_cast<u32>(inst.Arg(0).Reg())]; break;
            case IR::Opcode::SetRegister: regs[static_cast<u32>(inst.Arg(0).Reg())] = arg(1); break;
            case IR::Opcode::GetCbufU32:
                REQUIRE(arg(0) == 3);
                REQUIRE(arg(1) == 0);
                values[&inst] = 0x76543210;
                break;
            case IR::Opcode::BitFieldUExtract: values[&inst] = (arg(0) >> arg(1)) & ((1U << arg(2)) - 1); break;
            case IR::Opcode::INeg32: values[&inst] = 0U - arg(0); break;
            case IR::Opcode::IMul32: values[&inst] = arg(0) * arg(1); break;
            case IR::Opcode::IAdd32: values[&inst] = arg(0) + arg(1); break;
            case IR::Opcode::ShiftLeftLogical32: values[&inst] = arg(0) << arg(1); break;
            case IR::Opcode::ShiftRightLogical32: values[&inst] = arg(0) >> arg(1); break;
            case IR::Opcode::ULessThan: values[&inst] = arg(0) < arg(1); break;
            case IR::Opcode::SLessThan: values[&inst] = static_cast<s32>(arg(0)) < static_cast<s32>(arg(1)); break;
            case IR::Opcode::SelectU32: values[&inst] = arg(0) ? arg(1) : arg(2); break;
            default: FAIL("Unexpected multiplication IR operation " << IR::NameOf(inst.GetOpcode()));
            }
        }
        const u32 b = mode == 0 ? 0xcccccccd : mode == 1 ? 0xfffffff6 : 0x76543210;
        const u64 product = u64{a} * b;
        const u32 expected = mode == 5 ? a - 0xfedcba98U : mode == 4 ? (a << 4) + 0x70U : mode == 0 ? static_cast<u32>(product >> 32)
                                       : static_cast<u32>(product + 0xfedcba98U);
        REQUIRE(regs[3] == expected);
    }
}


TEST_CASE("Native uniform constants keep their indexed load and arithmetic values", "[native-shader]") {
    const std::array<Native, 7> input{{
        {{0xff007321, 0x000080ff, 0x00000000, 0}},
        {{0x00047ab9, 0x00c00000, 0x00000800, 0}},
        {{0x04047899, 0x00000002, 0x0800063f, 0}},
        {{0x04057abb, 0x00c00000, 0x08000800, 0}},
        {{0x00007c20, 0x00000005, 0x08410000, 0}},
        {{0x00007e21, 0x80000005, 0x08010000, 0}},
        VertexTransform.back(),
    }};
    auto code = input;
    // Store the computed scalar instead of ending immediately.
    std::vector<Native> program(code.begin(), code.end() - 1);
    program.push_back(Native{{0xff007322, 0x00007000, 0x000000ff, 0}});
    program.push_back(VertexTransform.back());
    const auto lowered = Shader::Ampere::LowerToMaxwell(program);
    REQUIRE(lowered);
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    EmptyEnvironment env;
    Shader::Maxwell::TranslatorVisitor visitor{env, block};
    for (const auto word : lowered.instructions) {
        using Shader::Maxwell::Opcode;
        switch (Shader::Maxwell::Decode(word)) {
        case Opcode::ALD: visitor.ALD(word); break;
        case Opcode::AST: visitor.AST(word); break;
        case Opcode::MOV_cbuf: visitor.MOV_cbuf(word); break;
        case Opcode::MOV_reg: visitor.MOV_reg(word); break;
        case Opcode::LOP32I: visitor.LOP32I(word); break;
        case Opcode::SHL_imm: visitor.SHL_imm(word); break;
        case Opcode::LDC: visitor.LDC(word); break;
        case Opcode::FMUL_reg: visitor.FMUL_reg(word); break;
        case Opcode::FADD_reg: visitor.FADD_reg(word); break;
        case Opcode::EXIT: break;
        default: FAIL("Unexpected uniform instruction lowering");
        }
    }
    const std::array<float, 4> constants{std::bit_cast<float>(1U), 3.0f, 7.0f, 9.0f};
    REQUIRE(EvaluateVertex(block, {2, 0}, constants) == std::array<float, 2>{3, 0});
    REQUIRE(EvaluateVertex(block, {4, 0}, constants) == std::array<float, 2>{9, 0});
    auto conflict = program;
    conflict[0].words[0] = 0xfff87321; // Native R248 conflicts with the reserved uniform bank.
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(conflict));
}


TEST_CASE("Native predicate-output LUTs preserve boolean bit tests", "[native-shader]") {
    const std::array<Native, 3> input{{
        {{0x00ff7812, 0x00000001, 0x0780c0ff, 0}},
        {{0x01ff7812, 0x00000001, 0x07824802, 0}},
        VertexTransform.back(),
    }};
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(lowered);
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    EmptyEnvironment env;
    Shader::Maxwell::TranslatorVisitor visitor{env, block};
    for (const auto word : lowered.instructions) {
        using Shader::Maxwell::Opcode;
        switch (Shader::Maxwell::Decode(word)) {
        case Opcode::MOV32I: visitor.MOV32I(word); break;
        case Opcode::LOP_reg: visitor.LOP_reg(word); break;
        case Opcode::EXIT: break;
        default: FAIL("Unexpected predicate LUT lowering");
        }
    }
    for (u32 a = 0; a < 4; ++a) for (u32 b = 0; b < 4; ++b) for (u32 c = 0; c < 4; ++c) {
        std::array<u32, IR::NUM_REGS> regs{};
        regs[0] = a; regs[1] = b; regs[2] = c;
        std::array<bool, 7> predicates{false, false, true, false, true, false, true};
        std::unordered_map<const IR::Inst*, u32> values;
        const auto get = [&](const IR::Value& value) -> u32 {
            if (!value.IsImmediate()) return values.at(value.Inst());
            return value.Type() == IR::Type::U1 ? static_cast<u32>(value.U1()) : value.U32();
        };
        for (const auto& inst : block.Instructions()) {
            const auto arg = [&](size_t i) { return get(inst.Arg(i)); };
            switch (inst.GetOpcode()) {
            case IR::Opcode::GetRegister: values[&inst] = regs[static_cast<u32>(inst.Arg(0).Reg())]; break;
            case IR::Opcode::SetRegister: regs[static_cast<u32>(inst.Arg(0).Reg())] = arg(1); break;
            case IR::Opcode::BitwiseAnd32: values[&inst] = arg(0) & arg(1); break;
            case IR::Opcode::BitwiseOr32: values[&inst] = arg(0) | arg(1); break;
            case IR::Opcode::BitwiseXor32: values[&inst] = arg(0) ^ arg(1); break;
            case IR::Opcode::INotEqual: values[&inst] = arg(0) != arg(1); break;
            case IR::Opcode::SetPred: predicates[static_cast<u32>(inst.Arg(0).Pred())] = arg(1) != 0; break;
            default: FAIL("Unexpected predicate LUT IR " << IR::NameOf(inst.GetOpcode()));
            }
        }
        REQUIRE(predicates[0] == ((a & 1) != 0));
        REQUIRE(predicates[1] == (((b ^ c) & 1) != 0));
        REQUIRE(std::array<bool, 5>{predicates[2], predicates[3], predicates[4], predicates[5], predicates[6]} ==
                std::array<bool, 5>{true, false, true, false, true});
    }
}


TEST_CASE("Native cube sampling preserves XYZ, explicit LOD, and split color destinations", "[native-shader]") {
    std::array<Native, 2> input{{
        {{0x04007b60, 0x70800808, 0x059e0703, 0}},
        VertexTransform.back(),
    }};
    const u32 binding = GENERATE(0U, 2U);
    input[0].words[1] = (input[0].words[1] & ~(31U << 22)) | (binding << 22);
    const bool zero_lod = GENERATE(false, true);
    const bool volume = GENERATE(false, true);
    if (volume) {
        input[0].words[1] = (input[0].words[1] & ~(7U << 29)) | (2U << 29);
        input[0].words[2] = (input[0].words[2] & ~(7U << 23)) | (1U << 23);
    }
    if (zero_lod) {
        input[0].words[2] = (input[0].words[2] & ~(7U << 23)) | (1U << 23);
        input[0].words[1] = (input[0].words[1] & ~0xffU) | 9;
    }
    const bool rgba = GENERATE(false, true);
    if (rgba) input[0].words[2] = (input[0].words[2] & ~0xffffU) | 0x0f02;
    const bool pair = GENERATE(false, true);
    if (pair) input[0].words[2] = (input[0].words[2] & ~0xffffU) | 0x03ff;
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(lowered);
    REQUIRE(lowered.required_texture_buffer == binding);
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    EmptyEnvironment env;
    Shader::Maxwell::TranslatorVisitor visitor{env, block};
    for (const auto word : lowered.instructions) {
        using Shader::Maxwell::Opcode;
        switch (Shader::Maxwell::Decode(word)) {
        case Opcode::MOV_reg: visitor.MOV_reg(word); break;
        case Opcode::TEX: visitor.TEX(word); break;
        case Opcode::EXIT: break;
        default: FAIL("Unexpected cube sampling lowering");
        }
    }
    u32 samples = 0;
    std::vector<std::pair<u32, u32>> copies;
    std::vector<u32> writes, channels;
    for (const auto& inst : block.Instructions()) {
        if (inst.GetOpcode() == IR::Opcode::CompositeExtractF32x4) channels.push_back(inst.Arg(1).U32());
        if (inst.GetOpcode() == IR::Opcode::SetRegister) {
            const u32 target = static_cast<u32>(inst.Arg(0).Reg());
            if (target < 248) writes.push_back(target);
            if (inst.Arg(1).Inst()->GetOpcode() == IR::Opcode::GetRegister)
                copies.emplace_back(target, static_cast<u32>(inst.Arg(1).Inst()->Arg(0).Reg()));
        }
        if (inst.GetOpcode() == IR::Opcode::BoundImageSampleExplicitLod) {
            ++samples;
            REQUIRE(inst.Flags<IR::TextureInstInfo>().type == (volume ? Shader::TextureType::Color3D : Shader::TextureType::ColorCube));
            const auto* coords = inst.Arg(1).Inst();
            REQUIRE(coords->GetOpcode() == IR::Opcode::CompositeConstructF32x3);
            for (u32 i = 0; i < 3; ++i)
                REQUIRE(coords->Arg(i).Inst()->Arg(0).Inst()->Arg(0).Reg() == IR::Reg{250 + i});
            if (volume || zero_lod) REQUIRE(inst.Arg(2).F32() == 0.0f);
            else REQUIRE(inst.Arg(2).Inst()->Arg(0).Inst()->Arg(0).Reg() == IR::Reg{9});
        }
    }
    REQUIRE(samples == 1);
    REQUIRE(writes == (pair ? std::vector<u32>{0, 1} : rgba ? std::vector<u32>{0, 1, 2, 3} : std::vector<u32>{0, 1, 3}));
    REQUIRE(channels == (pair ? std::vector<u32>{0, 1} : rgba ? std::vector<u32>{0, 1, 2, 3} : std::vector<u32>{0, 1, 2}));
    REQUIRE(copies == (pair ? std::vector<std::pair<u32, u32>>{{250, 4}, {251, 5}, {252, zero_lod ? 9U : 8U}, {0, 250}, {1, 251}} : rgba ? std::vector<std::pair<u32, u32>>{{250, 4}, {251, 5}, {252, zero_lod ? 9U : 8U}, {0, 250}, {1, 251}, {2, 252}, {3, 253}} :
                             std::vector<std::pair<u32, u32>>{{250, 4}, {251, 5}, {252, zero_lod ? 9U : 8U}, {0, 250}, {1, 251}, {3, 252}}));
}


TEST_CASE("Native branch maps survive code caching and reject invalid addresses", "[native-shader]") {
    std::vector<u64> code(64);
    const std::array<u32, 4> offsets{0, 2, 4, 8};
    Shader::Ampere::AppendNativeAddressMap(code, offsets);
    REQUIRE(Shader::Ampere::HasNativeAddressMap(code));
    REQUIRE(Shader::Ampere::NativeProgramAddress(code, 0) == 0x50U);
    REQUIRE(Shader::Ampere::NativeProgramAddress(code, 16) == 0x68U);
    const auto restored = code;
    REQUIRE(Shader::Ampere::NativeProgramAddress(restored, 48) == 0xa8U);
    REQUIRE_FALSE(Shader::Ampere::NativeProgramAddress(code, 1));
    REQUIRE_FALSE(Shader::Ampere::NativeProgramAddress(code, 64));
    code[code.size() - 2] = 100000;
    REQUIRE_FALSE(Shader::Ampere::NativeProgramAddress(code, 0));
}


TEST_CASE("Native computed jumps retain table values while mapping code destinations", "[native-shader]") {
    const std::array<Native, 12> input{{
        {{0xff007321, 0x0002fcff, 0x00000000, 0}},
        {{0x00007817, 0x00000001, 0x03800000, 0}},
        {{0x00027819, 0x00000002, 0x000006ff, 0}},
        {{0x02007b82, 0x00400000, 0x00000800, 0}},
        {{0xff037421, 0x3f000000, 0x00010000, 0}},
        {{0x00007949, 0xffffffa0, 0x0383ffff, 0}},
        {{0x00007802, 0x3f800000, 0x00000f00, 0}},
        {{0x00007947, 0x00000020, 0x03800000, 0}},
        {{0x00007802, 0x40000000, 0x00000f00, 0}},
        {{0x00007947, 0x00000000, 0x03800000, 0}},
        {{0xff007322, 0x00007000, 0x000000ff, 0}},
        VertexTransform.back(),
    }};
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(lowered);
    std::vector<u64> code(0x50 / 8);
    for (const auto word : lowered.instructions) {
        if ((code.size() * 8) % 32 == 0) code.push_back(0);
        code.push_back(word);
    }
    code.push_back(0);
    Shader::Ampere::AppendNativeAddressMap(code, lowered.source_instruction_offsets);
    class BranchEnvironment final : public EmptyEnvironment {
    public:
        explicit BranchEnvironment(const std::vector<u64>& code_) : code{code_} { stage = Shader::Stage::VertexB; }
        u64 ReadInstruction(u32 address) override { return code.at(address / 8); }
        u32 ReadCbufValue(u32 bank, u32 offset) override {
            REQUIRE(bank == 1);
            REQUIRE((offset == 0 || offset == 4));
            return offset == 0 ? 0x60 : 0x80;
        }
        u32 TranslateProgramAddress(u32 address) const override {
            const auto target = Shader::Ampere::NativeProgramAddress(code, address);
            if (!target) throw std::logic_error("invalid synthetic branch target");
            return *target;
        }
        const std::vector<u64>& code;
    } env{code};
    Shader::ObjectPool<Shader::Maxwell::Flow::Block> flow_pool;
    Shader::ObjectPool<IR::Inst> inst_pool;
    Shader::ObjectPool<IR::Block> block_pool;
    Shader::Maxwell::Flow::CFG cfg{env, flow_pool, 0x50};
    u32 jumps = 0;
    for (const auto& block : cfg.Functions().front().blocks) {
        if (block.end_class != Shader::Maxwell::Flow::EndClass::IndirectBranch) continue;
        ++jumps;
        REQUIRE(block.branch_offset == 0);
        REQUIRE(block.indirect_branches.size() == 2);
        for (const auto& branch : block.indirect_branches) {
            REQUIRE((branch.address == 0x60 || branch.address == 0x80));
            REQUIRE(branch.block->begin.Offset() == env.TranslateProgramAddress(branch.address));
            REQUIRE(branch.block->begin.Offset() != branch.address);
        }
    }
    REQUIRE(jumps == 1);
    Shader::HostTranslateInfo host;
    const auto program = Shader::Maxwell::TranslateProgram(inst_pool, block_pool, env, cfg, host);
    REQUIRE_FALSE(program.blocks.empty());
}


TEST_CASE("Native forward branches retain paths beyond an early unconditional exit", "[native-shader]") {
    const Native exit{{0x0000794d, 0, 0x03800000, 0x000fea00}};
    const std::array<Native, 4> source{{
        {{0x00000947, 0x20, 0x03800000, 0x000fea00}},
        exit,
        {{0x00017802, 42, 0x00000f00, 0x000fe400}},
        exit,
    }};
    const auto lowered = Shader::Ampere::LowerToMaxwell(source);
    REQUIRE(lowered);
    REQUIRE(lowered.source_size == sizeof(source));
    REQUIRE(lowered.source_instruction_offsets.size() == source.size());
    REQUIRE(lowered.instructions.size() == 4);
    const auto truncated = Shader::Ampere::LowerToMaxwell(std::span{source}.first(2));
    REQUIRE_FALSE(truncated);
    REQUIRE(truncated.instructions.empty());
}


TEST_CASE("Native register-to-predicate writes only masked predicate bits", "[native-shader]") {
    const u32 byte = GENERATE(0U, 1U, 2U, 3U);
    const std::array<Native, 2> source{{{{0x05007804, 7, byte << 12, 0x000fe200}}, VertexTransform.back()}};
    const auto lowered = Shader::Ampere::LowerToMaxwell(source);
    REQUIRE(lowered);
    REQUIRE(Shader::Maxwell::Decode(lowered.instructions[0]) == Shader::Maxwell::Opcode::R2P_imm);
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    EmptyEnvironment env;
    Shader::Maxwell::TranslatorVisitor visitor{env, block};
    visitor.R2P_imm(lowered.instructions[0]);
    for (u32 value = 0; value < 128; ++value) {
        std::array<bool, 7> predicates{true, false, true, false, true, false, true};
        std::unordered_map<const IR::Inst*, u32> values;
        const auto get = [&](const IR::Value& v) -> u32 {
            if (!v.IsImmediate()) return values.at(v.Inst());
            return v.Type() == IR::Type::U1 ? static_cast<u32>(v.U1()) : v.U32();
        };
        for (const auto& inst : block.Instructions()) {
            const auto arg = [&](size_t i) { return get(inst.Arg(i)); };
            switch (inst.GetOpcode()) {
            case IR::Opcode::GetRegister:
                REQUIRE(static_cast<u32>(inst.Arg(0).Reg()) == 5);
                values[&inst] = (value << (byte * 8)) | (0xa5a5a5a5U & ~(0xffU << (byte * 8))); break;
            case IR::Opcode::GetPred: values[&inst] = predicates[static_cast<u32>(inst.Arg(0).Pred())]; break;
            case IR::Opcode::SetPred: predicates[static_cast<u32>(inst.Arg(0).Pred())] = arg(1) != 0; break;
            case IR::Opcode::BitFieldUExtract: values[&inst] = (arg(0) >> arg(1)) & ((1U << arg(2)) - 1); break;
            case IR::Opcode::GetZeroFromOp: values[&inst] = arg(0) == 0; break;
            case IR::Opcode::IEqual: values[&inst] = arg(0) == arg(1); break;
            case IR::Opcode::LogicalNot: values[&inst] = !arg(0); break;
            case IR::Opcode::SelectU1: values[&inst] = arg(0) ? arg(1) : arg(2); break;
            default: FAIL("Unexpected R2P IR " << IR::NameOf(inst.GetOpcode()));
            }
        }
        for (u32 bit = 0; bit < 7; ++bit)
            REQUIRE(predicates[bit] == (bit < 3 ? bool(value & (1U << bit)) : bool(bit % 2 == 0)));
    }
    auto invalid = source;
    invalid[0].words[2] = 1;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(invalid));
}


TEST_CASE("Native 2D explicit LOD sampling preserves its paired coordinates", "[native-shader]") {
    const std::array<Native, 2> input{{{{0xc007b60, 0x30800812, 0x59e070b, 0x100f4200}}, VertexTransform.back()}};
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(lowered);
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    EmptyEnvironment env;
    Shader::Maxwell::TranslatorVisitor visitor{env, block};
    visitor.TEXS(lowered.instructions[0]);
    u32 samples = 0;
    for (const auto& inst : block.Instructions()) {
        if (inst.GetOpcode() != IR::Opcode::BoundImageSampleExplicitLod) continue;
        ++samples;
        REQUIRE(inst.Flags<IR::TextureInstInfo>().type == Shader::TextureType::Color2D);
        const auto* coords = inst.Arg(1).Inst();
        REQUIRE(coords->GetOpcode() == IR::Opcode::CompositeConstructF32x2);
        for (u32 i = 0; i < 2; ++i)
            REQUIRE(coords->Arg(i).Inst()->Arg(0).Inst()->Arg(0).Reg() == IR::Reg{12 + i});
        REQUIRE(inst.Arg(2).Inst()->Arg(0).Inst()->Arg(0).Reg() == IR::Reg{18});
    }
    REQUIRE(samples == 1);
}


TEST_CASE("Native warp uniform transfers use the first selected active lane and preserve predicates", "[native-shader]") {
    const u32 operation = GENERATE(0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U);
    const Native instruction = operation < 2
        ? Native{{operation == 0 ? 0x020513c2U : 0x020593c2U, 0, 0x000e0000, 0}}
        : operation < 4 ? Native{{0x003f7886, 0, operation == 2 ? 0x00800100U : 0x00800000U, 0}}
        : Native{{0x0000781c, 0, operation == 4 ? 0x03f2f008U : operation == 5 ? 0x03f0f070U : operation == 6 ? 0x03f0e170U : 0x00f41070U, 0}};
    const std::array<Native, 2> source{{instruction, VertexTransform.back()}};
    const auto lowered = Shader::Ampere::LowerToMaxwell(source);
    REQUIRE(lowered);
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    EmptyEnvironment env;
    Shader::Maxwell::TranslatorVisitor visitor{env, block};
    for (const auto word : lowered.instructions) {
        using Shader::Maxwell::Opcode;
        switch (Shader::Maxwell::Decode(word)) {
        case Opcode::PSETP: visitor.PSETP(word); break;
        case Opcode::SHR_imm: visitor.SHR_imm(word); break;
        case Opcode::SHL_imm: visitor.SHL_imm(word); break;
        case Opcode::MOV32I: visitor.MOV32I(word); break;
        case Opcode::P2R_imm: visitor.P2R_imm(word); break;
        case Opcode::R2P_imm: visitor.R2P_imm(word); break;
        case Opcode::VOTE: visitor.VOTE(word); break;
        case Opcode::IADD_reg: visitor.IADD_reg(word); break;
        case Opcode::LOP_reg: visitor.LOP_reg(word); break;
        case Opcode::FLO_reg: visitor.FLO_reg(word); break;
        case Opcode::SHFL: visitor.SHFL(word); break;
        case Opcode::SEL_reg: visitor.SEL_reg(word); break;
        case Opcode::EXIT: break;
        default: FAIL("Unexpected warp-uniform lowering");
        }
    }
    double DecodeTestHalf(u16 bits);
u16 EncodeTestHalf(double value);
using Warp = std::array<u32, 32>;
    for (const u32 active : {1U, 0x80000000U, 0x80000001U, 0x55555555U, 0xffffffffU}) {
        for (const u32 selected : {0U, 1U, 0x80000000U, 0xaaaaaaaaU, 0xffffffffU}) {
            std::array<std::array<u32, IR::NUM_REGS>, 32> registers{};
            std::array<std::array<bool, 7>, 32> predicates{};
            for (u32 lane = 0; lane < 32; ++lane) {
                registers[lane][2] = 100 + lane * 17;
                registers[lane][221] = 0xcafebeef;
                registers[lane][239] = (selected & (1U << lane)) ? 64 : 0;
                for (u32 p = 0; p < 7; ++p) predicates[lane][p] = ((lane + p) & 1) != 0;
                predicates[lane][1] = ((selected & (1U << lane)) != 0) != (operation == 4);
            }
            const auto original_predicates = predicates;
            ExecuteNativeWarp(block, active, registers, predicates);
            const u32 ballot = active & (operation == 1 ? ~selected : selected);
            const u32 first = ballot ? std::countr_zero(ballot) : 0;
            for (u32 lane = 0; lane < 32; ++lane) {
                if (!(active & (1U << lane))) continue;
                auto expected_predicates = original_predicates[lane];
                if (operation == 7) expected_predicates[2] = original_predicates[lane][1] && original_predicates[lane][0];
                if (operation == 4) expected_predicates[1] = (selected & (1U << lane)) != 0;
                if (operation == 5 || operation == 6) expected_predicates[0] = operation == 5;
                REQUIRE(predicates[lane] == expected_predicates);
                if (operation < 2) REQUIRE(registers[lane][221] == (ballot ? 100 + first * 17 : 0xcafebeef));
                else if (operation < 4) REQUIRE(registers[lane][239] == (operation == 2 ? (ballot ? 64U : 0U) : (ballot == active ? 64U : 0U)));
            }
        }
    }
}


TEST_CASE("Native uniform 64-bit loads populate consecutive registers", "[native-shader]") {
    const std::array<Native, 6> input{{
        {{0x00057ab9, 0x00c00000, 0x00000800, 0}},
        {{0x05067abb, 0x00c00100, 0x08000a00, 0}},
        {{0x00007c02, 6, 0x08000f00, 0}},
        {{0x00017c02, 7, 0x08000f00, 0}},
        {{0xff007322, 0x00007000, 0x000004ff, 0}},
        VertexTransform.back(),
    }};
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(lowered);
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    EmptyEnvironment env;
    Shader::Maxwell::TranslatorVisitor visitor{env, block};
    for (const auto word : lowered.instructions) {
        using Shader::Maxwell::Opcode;
        switch (Shader::Maxwell::Decode(word)) {
        case Opcode::MOV_cbuf: visitor.MOV_cbuf(word); break;
        case Opcode::MOV_reg: visitor.MOV_reg(word); break;
        case Opcode::LDC: visitor.LDC(word); break;
        case Opcode::AST: visitor.AST(word); break;
        case Opcode::EXIT: break;
        default: FAIL("Unexpected uniform pair lowering");
        }
    }
    REQUIRE(EvaluateVertex(block, {}, {std::bit_cast<float>(0U), 3, 7, 9}) == std::array<float, 2>{3, 7});
}


TEST_CASE("Native texture dimension queries retain mip input and width-height outputs", "[native-shader]") {
    const u32 binding = GENERATE(0U, 2U, 17U);
    const std::array<Native, 2> source{{{{0x03027b6f, 0x00000800U | (binding << 22), 0x000003ff, 0}}, VertexTransform.back()}};
    const auto lowered = Shader::Ampere::LowerToMaxwell(source);
    REQUIRE(lowered);
    REQUIRE(lowered.required_texture_buffer == binding);
    REQUIRE(Shader::Maxwell::Decode(lowered.instructions[0]) == Shader::Maxwell::Opcode::TXQ);
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    EmptyEnvironment env;
    Shader::Maxwell::TranslatorVisitor visitor{env, block};
    visitor.TXQ(lowered.instructions[0]);
    std::vector<std::pair<u32, u32>> outputs;
    u32 queries = 0;
    for (const auto& inst : block.Instructions()) {
        if (inst.GetOpcode() == IR::Opcode::BoundImageQueryDimensions) {
            ++queries;
            REQUIRE(inst.Arg(0).U32() == 32);
            REQUIRE(inst.Arg(1).Inst()->Arg(0).Reg() == IR::Reg{3});
            REQUIRE(inst.Arg(2).U1());
        }
        if (inst.GetOpcode() == IR::Opcode::SetRegister) {
            const auto* extract = inst.Arg(1).Inst();
            REQUIRE(extract->GetOpcode() == IR::Opcode::CompositeExtractU32x4);
            outputs.emplace_back(static_cast<u32>(inst.Arg(0).Reg()), extract->Arg(1).U32());
        }
    }
    REQUIRE(queries == 1);
    REQUIRE(outputs == std::vector<std::pair<u32, u32>>{{2, 0}, {3, 1}});
    auto invalid = source;
    invalid[0].words[1] |= 1U << 30;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(invalid));
    invalid = source;
    invalid[0].words[1] = (invalid[0].words[1] & ~(31U << 22)) | (18U << 22);
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(invalid));
    const std::array<Native, 3> conflict{{source[0], source[0], source[1]}};
    auto mixed = conflict;
    mixed[1].words[1] ^= 1U << 22;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(mixed));
}


TEST_CASE("Native scalar global stores retain both uniform address words and source data", "[native-shader]") {
    std::array<Native, 2> input{{
        {{0xff007986, 7, 0x0c10a904, 0}},
        VertexTransform.back(),
    }};
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(lowered);
    REQUIRE(Shader::Maxwell::Decode(lowered.instructions.front()) == Shader::Maxwell::Opcode::STG);
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    EmptyEnvironment env;
    Shader::Maxwell::TranslatorVisitor visitor{env, block};
    visitor.STG(lowered.instructions.front());
    std::vector<u32> registers;
    u32 stores{};
    for (const auto& inst : block.Instructions()) {
        if (inst.GetOpcode() == IR::Opcode::GetRegister)
            registers.push_back(static_cast<u32>(inst.Arg(0).Reg()));
        if (inst.GetOpcode() == IR::Opcode::CompositeConstructU32x2) {
            REQUIRE(inst.Arg(0).Inst()->Arg(0).Reg() == IR::Reg::R220);
            REQUIRE(inst.Arg(1).Inst()->Arg(0).Reg() == IR::Reg::R221);
        }
        if (inst.GetOpcode() == IR::Opcode::WriteGlobal32) {
            REQUIRE(inst.Arg(1).Inst()->Arg(0).Reg() == IR::Reg::R7);
            ++stores;
        }
    }
    std::sort(registers.begin(), registers.end());
    REQUIRE(registers == std::vector<u32>{7, 220, 221});
    REQUIRE(stores == 1);
    input[0].words[2] |= 1; // An unaligned uniform address pair is unsupported.
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(input));
    input[0].words[2] = 0x0c10ab04; // A different store width must not become scalar.
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(input));
}


TEST_CASE("Captured native compute compiles with shared memory and atomics", "[.native-compute-captured]") {
    const char* directory = std::getenv("EDEN_NATIVE_SHADER_FIXTURES");
    REQUIRE(directory != nullptr);
    std::ifstream file(std::filesystem::path{directory} / "compute.source.bin", std::ios::binary | std::ios::ate);
    REQUIRE(file.good());
    const auto size = static_cast<size_t>(file.tellg());
    REQUIRE(size % 16 == 0);
    std::vector<Native> input(size / 16);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(input.data()), size);
    std::vector<u32> traps;
    for (u32 index = 0; index < input.size(); ++index)
        if (input[index].Bits(0, 12) == 0x95c) traps.push_back(index * sizeof(Native));
    if (!traps.empty()) {
        std::ifstream constants(std::filesystem::path{directory} / "compute.guard-constants.bin", std::ios::binary);
        REQUIRE(constants.good());
        std::unordered_map<u64, u32> values;
        std::array<u32, 3> record{};
        while (constants.read(reinterpret_cast<char*>(record.data()), sizeof(record)))
            values[(u64{record[0]} << 32) | record[1]] = record[2];
        const Shader::Ampere::TrapGuardReader read = [&](u32 bank, u32 offset) -> std::optional<u32> {
            const auto it = values.find((u64{bank} << 32) | offset);
            return it == values.end() ? std::nullopt : std::optional<u32>{it->second};
        };
        REQUIRE(Shader::Ampere::VerifyTrapGuards(std::span{input}.first(traps.back() / sizeof(Native) + 1), read));
    }
    const auto lowered = Shader::Ampere::LowerToMaxwell(input, traps, true);
    INFO("offset=" << lowered.error_offset << " error=" << lowered.error);
    REQUIRE(lowered);
    std::vector<u64> code(0x50 / 8);
    for (u64 word : lowered.instructions) {
        if ((code.size() * 8) % 32 == 0) code.push_back(0);
        code.push_back(word);
    }
    code.push_back(0);
    Shader::Ampere::AppendNativeAddressMap(code, lowered.source_instruction_offsets);
    std::array<u32, 64> qmd{};
    std::ifstream descriptor(std::filesystem::path{directory} / "compute.qmd.bin", std::ios::binary);
    REQUIRE(descriptor.good());
    descriptor.read(reinterpret_cast<char*>(qmd.data()), sizeof(qmd));
    const std::array<u32, 3> group{qmd[18] >> 16, qmd[19] & 0xffff, qmd[19] >> 16};
    const u32 shared_size = qmd[17] & 0x3ffff;
    class ComputeFixtureEnvironment final : public EmptyEnvironment {
    public:
        ComputeFixtureEnvironment(const std::vector<u64>& code_, u32 shared_, std::array<u32, 3> group_, Shader::TextureType texture_, u32 local_)
            : code{code_}, shared{shared_}, group{group_}, texture{texture_} {
            local_size = local_;
            stage = Shader::Stage::Compute;
            start_address = 0x50;
            is_proprietary_driver = true;
        }
        u64 ReadInstruction(u32 address) override { return code.at(address / 8); }
        u32 TranslateProgramAddress(u32 address) const override {
            const auto translated = Shader::Ampere::NativeProgramAddress(code, address);
            if (!translated) throw std::logic_error("invalid compute branch target");
            return *translated;
        }
        bool IsCbufStorageBufferPointer(u32 bank, u32 offset) override {
            if (storage_cbuf.empty()) return true;
            if (bank != 0 || offset + 8 > storage_cbuf.size()) return false;
            u64 pointer{};
            std::memcpy(&pointer, storage_cbuf.data() + offset, sizeof(pointer));
            return std::find(mapped_addresses.begin(), mapped_addresses.end(), pointer) != mapped_addresses.end();
        }
        std::vector<char> storage_cbuf;
        std::vector<u64> mapped_addresses;
        u32 ReadCbufValue(u32, u32) override { return 0; }
        u32 TextureBoundBuffer() const override { return 0; }
        u32 SharedMemorySize() const override { return shared; }
        std::array<u32, 3> WorkgroupSize() const override { return group; }
        Shader::TextureType ReadTextureType(u32) override { return texture; }
        const std::vector<u64>& code;
        u32 shared;
        std::array<u32, 3> group;
        Shader::TextureType texture;
    } env{code, shared_size, group,
        std::any_of(input.begin(), input.end(), [](const auto& instruction) {
            return instruction.Bits(0, 12) == 0xb66 && instruction.Bits(61, 3) == 2;
        }) ? Shader::TextureType::Color3D : std::any_of(input.begin(), input.end(), [](const auto& instruction) {
            return (instruction.Bits(0, 12) == 0xb66 && instruction.Bits(61, 3) == 1) ||
                   instruction.Bits(0, 12) == 0xb97 || instruction.Bits(0, 12) == 0xb9b;
        }) ? Shader::TextureType::Color2D : Shader::TextureType::Color1D, (qmd[23] & 0xffffffU) + (qmd[50] & 0xffffffU)};
    std::ifstream storage_cbuf_file(std::filesystem::path{directory} / "compute.cbuf0.bin", std::ios::binary);
    env.storage_cbuf.assign(std::istreambuf_iterator<char>{storage_cbuf_file}, {});
    std::ifstream mapped_file(std::filesystem::path{directory} / "mapped-addresses.txt");
    u64 mapped_address{};
    while (mapped_file >> std::hex >> mapped_address) env.mapped_addresses.push_back(mapped_address);
    Shader::ObjectPool<Shader::Maxwell::Flow::Block> flow_pool;
    Shader::ObjectPool<IR::Inst> inst_pool;
    Shader::ObjectPool<IR::Block> block_pool;
    Shader::Maxwell::Flow::CFG cfg{env, flow_pool, env.StartAddress()};
    Shader::HostTranslateInfo host;
    host.support_int64 = true;
    host.support_float64 = true;
    host.support_float16 = true;
    host.support_conditional_barrier = true;
    const auto program = Shader::Maxwell::TranslateProgram(inst_pool, block_pool, env, cfg, host);
    REQUIRE(program.stage == Shader::Stage::Compute);
    std::ifstream expected_storage(std::filesystem::path{directory} / "storage-pointers.txt");
    if (expected_storage) {
        std::vector<u32> offsets;
        u32 offset{};
        while (expected_storage >> std::hex >> offset) offsets.push_back(offset);
        REQUIRE_FALSE(offsets.empty());
        REQUIRE_FALSE(program.info.storage_buffers_descriptors.empty());
        for (const auto* block : program.blocks) for (const auto& inst : block->Instructions()) {
            REQUIRE(inst.GetOpcode() != IR::Opcode::LoadGlobal32);
            REQUIRE(inst.GetOpcode() != IR::Opcode::LoadGlobal64);
            REQUIRE(inst.GetOpcode() != IR::Opcode::LoadGlobal128);
            REQUIRE(inst.GetOpcode() != IR::Opcode::WriteGlobal32);
            REQUIRE(inst.GetOpcode() != IR::Opcode::WriteGlobal64);
            REQUIRE(inst.GetOpcode() != IR::Opcode::WriteGlobal128);
        }
        for (const auto& storage_descriptor : program.info.storage_buffers_descriptors) {
            INFO("storage bank=" << storage_descriptor.cbuf_index << " offset=" << storage_descriptor.cbuf_offset);
            REQUIRE(storage_descriptor.cbuf_index == 0);
            REQUIRE(std::find(offsets.begin(), offsets.end(), storage_descriptor.cbuf_offset) != offsets.end());
        }
    }
    REQUIRE_FALSE(program.blocks.empty());
    u32 barriers{}, shared_atomics{}, global_atomics{}, stores{}, image_stores{}, image_loads{};
    for (const auto* block : program.blocks) for (const auto& inst : block->Instructions()) {
        image_loads += inst.GetOpcode() == IR::Opcode::ImageRead || inst.GetOpcode() == IR::Opcode::BoundImageRead;
        barriers += inst.GetOpcode() == IR::Opcode::Barrier;
        shared_atomics += inst.GetOpcode() == IR::Opcode::SharedAtomicIAdd32;
        global_atomics += inst.GetOpcode() == IR::Opcode::GlobalAtomicIAdd32 || inst.GetOpcode() == IR::Opcode::StorageAtomicIAdd32;
        stores += inst.GetOpcode() == IR::Opcode::WriteGlobal32 || inst.GetOpcode() == IR::Opcode::WriteStorage32 ||
                  inst.GetOpcode() == IR::Opcode::WriteGlobalU8 || inst.GetOpcode() == IR::Opcode::WriteStorageU8 ||
                  inst.GetOpcode() == IR::Opcode::WriteGlobalU16 || inst.GetOpcode() == IR::Opcode::WriteStorageU16 ||
                  inst.GetOpcode() == IR::Opcode::WriteGlobal64 || inst.GetOpcode() == IR::Opcode::WriteStorage64;
        image_stores += inst.GetOpcode() == IR::Opcode::ImageWrite || inst.GetOpcode() == IR::Opcode::BoundImageWrite;
    }
    const bool surface = std::any_of(input.begin(), input.end(), [](const auto& instruction) { return instruction.Bits(0, 12) == 0xb9b; });
    if (surface) {
        const auto expected_barriers = std::count_if(input.begin(), input.end(), [](const auto& instruction) {
            return instruction.Bits(0, 12) == 0xb1d;
        });
        REQUIRE(barriers >= expected_barriers);
        REQUIRE(image_stores >= 1);
        const auto expected_loads = std::count_if(input.begin(), input.end(), [](const auto& instruction) {
            return instruction.Bits(0, 12) == 0xb97;
        });
        REQUIRE(image_loads >= expected_loads);
    } else if (std::any_of(input.begin(), input.end(), [](const auto& instruction) { return instruction.Bits(0, 12) == 0xf8c; })) {
        REQUIRE(barriers >= 2);
        REQUIRE(shared_atomics >= 4);
        REQUIRE(global_atomics >= 2);
    } else if (std::any_of(input.begin(), input.end(), [](const auto& instruction) { const u32 op = instruction.Bits(0, 12); return op == 0xfae || op == 0x83b || op == 0x23c || (op == 0x388 && instruction.Bits(32, 8) == 255 && instruction.Bits(73, 3) >= 5); })) {
        const auto expected_barriers = std::count_if(input.begin(), input.end(), [](const auto& instruction) {
            return instruction.Bits(0, 12) == 0xb1d;
        });
        REQUIRE(barriers >= expected_barriers);
        REQUIRE(stores >= 1);
    } else if (!traps.empty()) {
        REQUIRE(barriers == 0);
        REQUIRE(stores >= 1);
    } else if (shared_size) {
        REQUIRE(barriers >= 5);
        REQUIRE(shared_atomics >= 2);
        REQUIRE(global_atomics >= 1);
        REQUIRE(stores >= 8);
    } else {
        REQUIRE(barriers == 0);
        const auto expected_stores = std::count_if(input.begin(), input.end(), [](const auto& instruction) {
            return instruction.Bits(0, 12) == 0x986;
        });
        REQUIRE(expected_stores >= 1);
        REQUIRE(stores >= expected_stores);
    }
}


TEST_CASE("Native mixed-bank texel fetches retain descriptor coordinates and split channels", "[native-shader]") {
    const u32 dimension = GENERATE(0U, 1U, 2U, 5U);
    const u32 mask = GENERATE(1U, 3U, 7U, 15U);
    const u32 second = mask >= 7 ? 12U : 255U;
    const std::array<Native, 3> input{{
        {{0x03027b6f, 0x01c00800, 0x000003ff, 0}},
        {{0x04107b66, (dimension << 29) | 0x10c00808U, 0x049e0000U | (mask << 8) | second, 0}},
        VertexTransform.back(),
    }};
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(lowered);
    REQUIRE(lowered.required_texture_buffer == 7);
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    u32 handles{}, fetches{};
    std::vector<std::pair<u32, u32>> copies;
    std::vector<u32> channels;
    for (const auto& inst : block.Instructions()) {
        if (inst.GetOpcode() == IR::Opcode::GetCbufU32) {
            REQUIRE(inst.Arg(0).U32() == 3);
            REQUIRE(inst.Arg(1).U32() == 32);
            ++handles;
        }
        if (inst.GetOpcode() == IR::Opcode::BindlessImageFetch) {
            ++fetches;
            REQUIRE(inst.Arg(0).Inst()->Arg(0).Reg() == IR::Reg{253});
            const auto type = dimension == 0 ? Shader::TextureType::Color1D :
                dimension == 1 ? Shader::TextureType::Color2D :
                dimension == 2 ? Shader::TextureType::Color3D : Shader::TextureType::ColorArray2D;
            REQUIRE(inst.Flags<IR::TextureInstInfo>().type.Value() == type);
            REQUIRE(inst.Arg(3).U32() == 0);
        }
        if (inst.GetOpcode() == IR::Opcode::CompositeExtractF32x4) channels.push_back(inst.Arg(1).U32());
        if (inst.GetOpcode() == IR::Opcode::SetRegister && !inst.Arg(1).IsImmediate() &&
            inst.Arg(1).Inst()->GetOpcode() == IR::Opcode::GetRegister)
            copies.emplace_back(static_cast<u32>(inst.Arg(0).Reg()), static_cast<u32>(inst.Arg(1).Inst()->Arg(0).Reg()));
    }
    REQUIRE(handles == 1);
    REQUIRE(fetches == 1);
    REQUIRE(copies.front() == std::pair<u32, u32>{250, 4});
    if (dimension == 1) REQUIRE(copies[1] == std::pair<u32, u32>{251, 8});
    if (dimension == 2 || dimension == 5) {
        REQUIRE(copies[1] == std::pair<u32, u32>{251, 5});
        REQUIRE(copies[2] == std::pair<u32, u32>{252, 8});
    }
    std::vector<u32> expected_channels;
    for (u32 i = 0; i < 4; ++i) if (mask & (1U << i)) expected_channels.push_back(i);
    REQUIRE(channels == expected_channels);
    for (u32 i = 0; i < expected_channels.size(); ++i)
        REQUIRE(copies[copies.size() - expected_channels.size() + i] ==
                std::pair<u32, u32>{i < 2 ? 16U + i : 12U + i - 2, 250U + i});
}


TEST_CASE("Native mixed-bank samples preserve 2D and cube sampling modes", "[native-shader]") {
    const bool cube = GENERATE(false, true);
    const bool explicit_lod = GENERATE(false, true);
    const bool rgba = GENERATE(false, true);
    const u32 mask = rgba ? 15U : 3U;
    const std::array<Native, 3> input{{
        {{0x03027b6f, 0x01c00800, 0x000003ff, 0}},
        {{0x04107b60, (cube ? 0x70c00808U : 0x30c00808U),
          0x041e0000U | ((cube ? explicit_lod ? 3U : 1U : explicit_lod ? 1U : 0U) << 23) | (mask << 8) | (rgba ? 12U : 255U), 0}},
        VertexTransform.back(),
    }};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    u32 samples{}, handles{};
    for (const auto& inst : block.Instructions()) {
        if (inst.GetOpcode() == IR::Opcode::GetCbufU32) {
            REQUIRE(inst.Arg(0).U32() == 3);
            REQUIRE(inst.Arg(1).U32() == 32);
            ++handles;
        }
        if (inst.GetOpcode() == IR::Opcode::BindlessImageSampleExplicitLod ||
            inst.GetOpcode() == IR::Opcode::BindlessImageSampleImplicitLod) {
            REQUIRE(inst.GetOpcode() == (cube || explicit_lod ? IR::Opcode::BindlessImageSampleExplicitLod : IR::Opcode::BindlessImageSampleImplicitLod));
            ++samples;
            REQUIRE(inst.Arg(0).Inst()->Arg(0).Reg() == IR::Reg{cube ? 250U : 253U});
            REQUIRE(inst.Flags<IR::TextureInstInfo>().type.Value() ==
                    (cube ? Shader::TextureType::ColorCube : Shader::TextureType::Color2D));
            if (cube && explicit_lod)
                REQUIRE(inst.Arg(2).Inst()->Arg(0).Inst()->Arg(0).Reg() == IR::Reg{251});
            else if (cube || explicit_lod) REQUIRE(inst.Arg(2).F32() == 0.0f);
        }
    }
    REQUIRE(samples == 1);
    REQUIRE(handles == 1);
}


TEST_CASE("Native zero-high logical shifts preserve bits at every immediate boundary", "[native-shader]") {
    const bool right = GENERATE(false, true);
    const u32 destination = GENERATE(0U, 8U, 12U);
    std::vector<u32> values{0, 1, 0xffffffff, 0x80000000, 0xaaaaaaaa, 0x55555555};
    for (u32 bit = 0; bit < 32; ++bit) values.push_back(1U << bit);
    for (u32 shift = 0; shift <= 32; ++shift) {
        const Native instruction{{0x08007819U | (destination << 16), shift,
                                  0x000006ffU | (right ? 0x1000U : 0), 0}};
        const std::array<Native, 2> input{instruction, VertexTransform.back()};
        Shader::ObjectPool<IR::Inst> pool;
        IR::Block block{pool};
        TranslateScalarNative(input, block);
        for (u32 value : values) {
            ScalarNativeMachine machine;
            machine.regs.fill(0x12345678);
            machine.regs[8] = value;
            machine.predicates = {true, false, true, false, false, true, true};
            auto expected = machine.regs;
            expected[destination] = shift == 32 ? 0 : right ? value >> shift : value << shift;
            const auto predicates = machine.predicates;
            machine.Execute(block);
            REQUIRE(machine.regs == expected);
            REQUIRE(machine.predicates == predicates);
        }
    }
    for (const Native invalid : {
            Native{{0x08007819, 8, 0x00001609, 0}},
            Native{{0x08007819, 33, 0x000016ff, 0}},
            Native{{0x08007819, 8, 0x000014ff, 0}},
            Native{{0x08007819, 8, 0x00001eff, 0}}}) {
        const std::array<Native, 2> input{invalid, VertexTransform.back()};
        REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(input));
    }
}


TEST_CASE("Native CTA-strong global loads retain memory ordering around the read", "[native-shader]") {
    const std::array<Native, 2> input{{{{0xff247981, 4, 0x081ead00, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    u32 barriers{}, loads{};
    for (const auto& inst : block.Instructions()) {
        if (inst.GetOpcode() == IR::Opcode::DeviceMemoryBarrier) {
            REQUIRE(loads == barriers);
            ++barriers;
        }
        if (inst.GetOpcode() == IR::Opcode::LoadGlobal128) {
            REQUIRE(barriers == 1);
            ++loads;
        }
    }
    REQUIRE(loads == 1);
    REQUIRE(barriers == 2);
}


TEST_CASE("Native C8 bitwise LUT preserves each bit and predicate output", "[native-shader]") {
    const std::array<Native, 2> input{{{{0x00037212, 1, 0x0782c802, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u32 a : {0U, 1U, 0xaaaaaaaaU, 0xffffffffU})
        for (u32 b : {0U, 1U, 0x55555555U, 0xffffffffU})
            for (u32 c : {0U, 1U, 0xccccccccU, 0xffffffffU}) {
                ScalarNativeMachine machine;
                machine.regs[0] = a; machine.regs[1] = b; machine.regs[2] = c;
                machine.predicates = {true, false, true, false, true, false, true};
                auto expected = machine.predicates;
                expected[1] = ((a | c) & b) != 0;
                machine.Execute(block);
                REQUIRE(machine.regs[3] == ((a | c) & b));
                REQUIRE(machine.predicates == expected);
            }
}


TEST_CASE("Native extended equality comparisons retain the low-word predicate", "[native-shader]") {
    const bool equal = GENERATE(false, true);
    const u32 comparison = equal ? 2U : 5U;
    const std::array<Native, 3> input{{
        {{0x0000720c, 2, 0x03f00000U | (comparison << 12), 0}},
        {{0x0100720c, 3, 0x03f00100U | (comparison << 12), 0}},
        VertexTransform.back(),
    }};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u64 a : {0ULL, 1ULL, 0xffffffffULL, 0x100000000ULL, 0xffffffffffffffffULL})
        for (u64 b : {0ULL, 1ULL, 0xffffffffULL, 0x100000000ULL, 0xffffffffffffffffULL}) {
            ScalarNativeMachine machine;
            machine.regs[0] = static_cast<u32>(a); machine.regs[1] = static_cast<u32>(a >> 32);
            machine.regs[2] = static_cast<u32>(b); machine.regs[3] = static_cast<u32>(b >> 32);
            machine.predicates = {false, true, false, true, false, true, false};
            auto expected = machine.predicates;
            expected[0] = equal ? a == b : a != b;
            machine.Execute(block);
            REQUIRE(machine.predicates == expected);
            REQUIRE(machine.regs[0] == static_cast<u32>(a));
            REQUIRE(machine.regs[1] == static_cast<u32>(a >> 32));
        }
    auto invalid = input;
    invalid[1].words[2] ^= 1U << 23;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(invalid));
}


TEST_CASE("Native bindless dimension queries keep handle mip and overlapping destinations", "[native-shader]") {
    const u32 mask = GENERATE(1U, 3U, 8U);
    const std::array<Native, 2> input{{{{0x1c1d7370, 0x08000000, 0xffU | (mask << 8), 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    u32 queries{};
    std::vector<u32> writes, channels;
    for (const auto& inst : block.Instructions()) {
        if (inst.GetOpcode() == IR::Opcode::BindlessImageQueryDimensions) {
            ++queries;
            REQUIRE(inst.Arg(0).Inst()->Arg(0).Reg() == IR::Reg{28});
            REQUIRE(inst.Arg(1).Inst()->Arg(0).Reg() == IR::Reg{29});
            REQUIRE(inst.Arg(2).U1() == (mask != 8));
        }
        if (inst.GetOpcode() == IR::Opcode::SetRegister) {
            writes.push_back(static_cast<u32>(inst.Arg(0).Reg()));
            channels.push_back(inst.Arg(1).Inst()->Arg(1).U32());
        }
    }
    REQUIRE(queries == 1);
    REQUIRE(writes == (mask == 3 ? std::vector<u32>{29, 30} : std::vector<u32>{29}));
    REQUIRE(channels == (mask == 3 ? std::vector<u32>{0, 1} : mask == 8 ? std::vector<u32>{3} : std::vector<u32>{0}));
}


TEST_CASE("Native scalar bindless cube sampling keeps XYZ and the trailing handle", "[native-shader]") {
    const std::array<Native, 2> input{{{{0x28707361, 0x7800002a, 0x041e01ff, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    std::vector<std::pair<u32, u32>> copies;
    u32 samples{};
    for (const auto& inst : block.Instructions()) {
        if (inst.GetOpcode() == IR::Opcode::BindlessImageSampleImplicitLod) {
            ++samples;
            REQUIRE(inst.Flags<IR::TextureInstInfo>().type.Value() == Shader::TextureType::ColorCube);
            REQUIRE(inst.Arg(0).Inst()->Arg(0).Reg() == IR::Reg{250});
            const auto* coords = inst.Arg(1).Inst();
            REQUIRE(coords->GetOpcode() == IR::Opcode::CompositeConstructF32x3);
            for (u32 i = 0; i < 3; ++i)
                REQUIRE(coords->Arg(i).Inst()->Arg(0).Inst()->Arg(0).Reg() == IR::Reg{252 + i});
        }
        if (inst.GetOpcode() == IR::Opcode::SetRegister && inst.Arg(1).Inst()->GetOpcode() == IR::Opcode::GetRegister)
            copies.emplace_back(static_cast<u32>(inst.Arg(0).Reg()), static_cast<u32>(inst.Arg(1).Inst()->Arg(0).Reg()));
    }
    REQUIRE(samples == 1);
    REQUIRE(copies == std::vector<std::pair<u32, u32>>{{252, 40}, {253, 41}, {254, 42}, {250, 43}, {112, 250}});
}


TEST_CASE("Native scalar bindless 2D sampling retains packed offsets and handle aliases", "[native-shader]") {
    const bool offset = GENERATE(false, true);
    const std::array<Native, 2> input{{{{0x1c1e7361, 0x3800001e, 0x041e01ffU | (u32{offset} << 12), 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    u32 samples{};
    for (const auto& inst : block.Instructions()) {
        if (inst.GetOpcode() == IR::Opcode::BindlessImageSampleImplicitLod) {
            ++samples;
            REQUIRE(inst.Flags<IR::TextureInstInfo>().type.Value() == Shader::TextureType::Color2D);
            REQUIRE(inst.Arg(0).Inst()->Arg(0).Reg() == IR::Reg{30});
            const auto* coords = inst.Arg(1).Inst();
            for (u32 i = 0; i < 2; ++i)
                REQUIRE(coords->Arg(i).Inst()->Arg(0).Inst()->Arg(0).Reg() == IR::Reg{28 + i});
            if (offset) {
                const auto* offsets = inst.Arg(3).Inst();
                REQUIRE(offsets->GetOpcode() == IR::Opcode::CompositeConstructU32x2);
                for (u32 i = 0; i < 2; ++i) {
                    const auto* field = offsets->Arg(i).Inst();
                    REQUIRE(field->GetOpcode() == IR::Opcode::BitFieldSExtract);
                    REQUIRE(field->Arg(0).Inst()->Arg(0).Reg() == IR::Reg{31});
                    REQUIRE(field->Arg(1).U32() == i * 4);
                    REQUIRE(field->Arg(2).U32() == 4);
                }
            } else REQUIRE(inst.Arg(3).IsEmpty());
        }
    }
    REQUIRE(samples == 1);
}


TEST_CASE("Native bindless cube sampling retains packed channels and explicit LOD", "[native-shader]") {
    const u32 form = GENERATE(0U, 1U, 2U, 3U, 4U);
    const u32 mask = GENERATE(1U, 3U, 7U, 15U);
    const bool scalar = form < 2;
    const u32 lod = form == 2 ? 3U : form == 1 || form == 4 ? 1U : 0U;
    const u32 second = mask == 7 ? 19U : mask == 15 ? 18U : 255U;
    const std::array<Native, 2> input{{
        {{0x04107361, (scalar ? 0x78000008U : 0x68000008U),
          0x041e0000U | (lod << 23) | (mask << 8) | second, 0}}, VertexTransform.back(),
    }};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    std::vector<std::pair<u32, u32>> copies;
    std::vector<u32> channels;
    u32 samples{};
    for (const auto& inst : block.Instructions()) {
        if (inst.GetOpcode() == IR::Opcode::BindlessImageSampleImplicitLod ||
            inst.GetOpcode() == IR::Opcode::BindlessImageSampleExplicitLod) {
            ++samples;
            REQUIRE(inst.GetOpcode() == (lod ? IR::Opcode::BindlessImageSampleExplicitLod : IR::Opcode::BindlessImageSampleImplicitLod));
            REQUIRE(inst.Flags<IR::TextureInstInfo>().type.Value() == Shader::TextureType::ColorCube);
            REQUIRE(inst.Arg(0).Inst()->Arg(0).Reg() == IR::Reg{250});
            if (lod == 3) REQUIRE(inst.Arg(2).Inst()->Arg(0).Inst()->Arg(0).Reg() == IR::Reg{251});
            else if (lod == 1) REQUIRE(inst.Arg(2).F32() == 0.0f);
        }
        if (inst.GetOpcode() == IR::Opcode::SetRegister && inst.Arg(1).Inst()->GetOpcode() == IR::Opcode::GetRegister)
            copies.emplace_back(static_cast<u32>(inst.Arg(0).Reg()), static_cast<u32>(inst.Arg(1).Inst()->Arg(0).Reg()));
        if (inst.GetOpcode() == IR::Opcode::CompositeExtractF32x4) channels.push_back(inst.Arg(1).U32());
    }
    REQUIRE(samples == 1);
    REQUIRE(copies[0] == std::pair<u32, u32>{252, 4});
    REQUIRE(copies[1] == std::pair<u32, u32>{253, 5});
    REQUIRE(copies[2] == std::pair<u32, u32>{254, scalar ? 8U : 6U});
    REQUIRE(copies[3] == std::pair<u32, u32>{250, scalar ? 9U : 8U});
    if (lod == 3) REQUIRE(copies[4] == std::pair<u32, u32>{251, 9});
    std::vector<u32> expected;
    for (u32 i = 0; i < 4; ++i) if (mask & (1U << i)) expected.push_back(i);
    REQUIRE(channels == expected);
    for (u32 i = 0; i < expected.size(); ++i)
        REQUIRE(copies[copies.size() - expected.size() + i] ==
                std::pair<u32, u32>{i < 2 ? 16U + i : second + i - 2, 250U + i});
}


TEST_CASE("Native predicated loop exits retain cleanup and the active reconvergence", "[native-shader]") {
    std::array<Native, 11> input{{
        {{0x27945, 144, 0x03800000, 0}},
        {{0x00007202, 1, 0xf00, 0}},
        {{0x8944, 16, 0x03c00000, 0}},
        {{0x7947, 0xffffffd0, 0x0383ffff, 0}},
        {{0x00057202, 0, 0xf00, 0}},
        {{0x7947, 48, 0x03800000, 0}},
        {{0x00057202, 1, 0xf00, 0}},
        {{0x00057202, 1, 0xf00, 0}},
        {{0x00057202, 1, 0xf00, 0}},
        {{0x27941, 0, 0x03800000, 0}}, VertexTransform.back(),
    }};
    if (GENERATE(false, true)) {
        input[4] = Native{{0x05057220, 6, 0x00410000, 0}};
        input[6] = Native{{0x05057223, 6, 0x00010007, 0}};
    }
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(lowered);
    auto invalid = input;
    invalid[5].words[1] = 0xffffffe0;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(invalid));
    invalid = input;
    invalid[3].words[1] = 0xffffffc0;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(invalid));
    invalid = input;
    invalid[4].words[0] = 0x00057802;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(invalid));
    std::vector<u64> code(0x50 / 8);
    for (const u64 word : lowered.instructions) {
        if ((code.size() * 8) % 32 == 0) code.push_back(0);
        code.push_back(word);
    }
    class LoopEnvironment final : public EmptyEnvironment {
    public:
        explicit LoopEnvironment(const std::vector<u64>& code_) : code{code_} { stage = Shader::Stage::VertexB; }
        u64 ReadInstruction(u32 address) override { return code.at(address / 8); }
        const std::vector<u64>& code;
    } env{code};
    Shader::ObjectPool<Shader::Maxwell::Flow::Block> flow_pool;
    Shader::Maxwell::Flow::CFG cfg{env, flow_pool, 0x50};
    INFO(cfg.Dot());
    const auto location = [&](u32 index) {
        const u32 offset = lowered.source_instruction_offsets[index];
        return Shader::Maxwell::Location{0x50 + offset * 8 + ((offset + 1) / 3) * 8};
    };
    for (bool initial : {false, true}) {
        bool predicate = initial, cleaned{}, joined{}, exited{}, repeated{};
        const auto* block = &*cfg.Functions().front().blocks.begin();
        for (u32 step = 0; step < 32 && block; ++step) {
            if (block->begin == location(3)) { repeated = true; predicate = false; }
            cleaned |= block->Contains(location(4));
            if (block->begin == location(9)) {
                joined = true;
                REQUIRE(block->stack.Peek(Shader::Maxwell::Flow::Token::SSY).has_value());
                REQUIRE(block->branch_true != nullptr);
                REQUIRE(block->branch_true->begin == location(10));
                REQUIRE_FALSE(block->branch_true->stack.Peek(Shader::Maxwell::Flow::Token::SSY).has_value());
            }
            if (block->end_class == Shader::Maxwell::Flow::EndClass::Exit) { exited = true; break; }
            const auto [pred, inverted] = block->cond.GetPred();
            REQUIRE(block->cond.GetFlowTest() == IR::FlowTest::T);
            REQUIRE((pred == IR::Pred::P0 || pred == IR::Pred::PT));
            block = ((pred == IR::Pred::PT || predicate) != inverted) ? block->branch_true : block->branch_false;
        }
        REQUIRE(exited);
        REQUIRE(cleaned);
        REQUIRE(joined);
        REQUIRE(repeated == initial);
    }
}


TEST_CASE("Native shared atomic extrema retain unsigned values and optional old results", "[native-shader]") {
    const bool minimum = GENERATE(false, true);
    const bool discard = GENERATE(false, true);
    const std::array<Native, 2> input{{{{discard ? 0xffff738cU : 0xff05738cU, 0x801,
        minimum ? 0x00800000U : 0x01000000U, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u32 old : {0U, 1U, 0x80000000U, 0xffffffffU})
        for (u32 value : {0U, 1U, 0x80000000U, 0xffffffffU}) {
            ScalarNativeMachine machine;
            machine.shared[8] = old;
            machine.regs[1] = value;
            machine.regs[5] = 0x12345678;
            machine.Execute(block);
            REQUIRE(machine.shared[8] == (minimum ? std::min(old, value) : std::max(old, value)));
            REQUIRE(machine.regs[1] == value);
            REQUIRE(machine.regs[5] == (discard ? 0x12345678U : old));
        }
    auto guarded = input;
    guarded[0].words[0] = (guarded[0].words[0] & ~0xf000U) | 0x3000;
    const auto lowered = Shader::Ampere::LowerToMaxwell(guarded);
    REQUIRE(lowered);
    REQUIRE(Shader::Maxwell::Instruction{lowered.instructions.front()}.Pred().index == 3);
}


TEST_CASE("Texture handle tracking cannot substitute a buffer address for loaded data", "[native-shader]") {
    const u32 mode = GENERATE(0U, 1U, 2U, 3U, 4U);
    class HandleEnvironment final : public EmptyEnvironment {
    public:
        u32 reads{};
        u32 ReadCbufValue(u32 bank, u32 offset) override {
            ++reads;
            REQUIRE(bank == 0);
            REQUIRE(offset == 0x510);
            return 7;
        }
        Shader::TextureType ReadTextureType(u32 handle) override {
            REQUIRE(handle == 7);
            return Shader::TextureType::Color2D;
        }
    } env;
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    IR::IREmitter ir{block};
    const IR::U32 pointer{ir.GetCbuf(ir.Imm32(0U), ir.Imm32(0x510U))};
    IR::U32 handle = pointer;
    const auto storage_load = [&](IR::Opcode opcode) {
        const auto it = block.PrependNewInst(block.end(), opcode, {ir.Imm32(0U), pointer});
        return IR::Value{&*it};
    };
    if (mode == 1 || mode == 4) handle = IR::U32{storage_load(IR::Opcode::LoadStorage32)};
    if (mode == 2) handle = IR::U32{ir.CompositeExtract(storage_load(IR::Opcode::LoadStorage64), 0)};
    if (mode == 3) {
        const IR::U64 address{ir.PackUint2x32(ir.CompositeConstruct(pointer, ir.Imm32(0x19U)))};
        handle = ir.LoadGlobal32(address);
    }
    if (mode == 4) handle = ir.BitwiseOr(handle, pointer);
    IR::TextureInstInfo info{};
    info.type.Assign(Shader::TextureType::Color2D);
    (void)ir.ImageSampleImplicitLod(handle, ir.CompositeConstruct(ir.Imm32(0.0f), ir.Imm32(0.0f)), {}, {}, {}, info);
    IR::Program program;
    program.blocks.push_back(&block);
    program.post_order_blocks.push_back(&block);
    Shader::HostTranslateInfo host;
    if (mode == 0) {
        REQUIRE_NOTHROW(Shader::Optimization::TexturePass(env, program, host));
        REQUIRE(program.info.texture_descriptors.size() == 1);
        REQUIRE(program.info.texture_descriptors[0].cbuf_index == 0);
        REQUIRE(program.info.texture_descriptors[0].cbuf_offset == 0x510);
        REQUIRE(env.reads == 1);
    } else {
        REQUIRE_THROWS_AS(Shader::Optimization::TexturePass(env, program, host), Shader::NotImplementedException);
        REQUIRE(env.reads == 0);
    }
}


TEST_CASE("Buffer-backed texture arrays retain record indices and root descriptor bounds", "[native-shader]") {
    const bool decomposed = GENERATE(false, true);
    const bool constant_index = GENERATE(false, true);
    const bool query = GENERATE(false, true);
    const u32 failure = GENERATE(0U, 1U, 2U, 3U, 4U);
    class BufferEnvironment final : public EmptyEnvironment {
    public:
        u32 size{40960};
        u32 high{};
        u32 ReadCbufValue(u32 bank, u32 offset) override {
            REQUIRE(bank == 0);
            if (offset == 0x518) return size;
            REQUIRE(offset == 0x51c);
            return high;
        }
        Shader::TextureType ReadTextureType(u32) override {
            FAIL("A buffer address must not be resolved as a texture handle");
            return {};
        }
    } env;
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    IR::IREmitter ir{block};
    const IR::U32 index = constant_index ? ir.Imm32(4U) : ir.GetReg(IR::Reg::R1);
    const auto constant = [&](u32 value) { return ir.Imm32(value); };
    IR::U32 product = ir.IMul(index, constant(160));
    if (decomposed) {
        const auto low = ir.BitFieldExtract(index, constant(0), constant(16));
        const auto high = ir.BitFieldExtract(index, constant(16), constant(16));
        product = ir.IAdd(ir.ShiftLeftLogical(ir.IAdd(ir.IMul(low, constant(0)), ir.IMul(high, constant(160))), constant(16)),
                         ir.IMul(low, constant(160)));
    }
    const IR::U32 pointer{ir.CompositeExtract(ir.GetCbuf(constant(0), constant(0x510), 64, false), 0)};
    const auto relative = ir.IAdd(product, constant(152));
    const auto offset = ir.ISub(ir.IAdd(pointer, relative), ir.BitwiseAnd(ir.GetCbuf(constant(0), constant(0x510)), constant(0xfffffff0)));
    const auto load = block.PrependNewInst(block.end(), IR::Opcode::LoadStorage64, {constant(0), offset});
    const IR::U32 handle{ir.CompositeExtract(IR::Value{&*load}, 0)};
    IR::TextureInstInfo flags{};
    flags.type.Assign(Shader::TextureType::ColorCube);
    const auto coordinates = ir.CompositeConstruct(ir.Imm32(0.0f), ir.Imm32(1.0f), ir.Imm32(0.0f));
    if (query) {
        const auto dimensions = ir.ImageQueryDimension(handle, constant(0), ir.Imm1(true));
        ir.SetReg(IR::Reg::R2, IR::U32{ir.CompositeExtract(dimensions, 0)});
    } else {
        (void)ir.ImageSampleExplicitLod(handle, coordinates, ir.Imm32(0.0f), {}, flags);
    }
    IR::Program program;
    program.stage = Shader::Stage::Fragment;
    program.blocks.push_back(&block);
    program.post_order_blocks.push_back(&block);
    program.info.storage_buffers_descriptors.push_back({0, 0x510, 1, false});
    const auto tracked = Shader::Optimization::BufferTexture::Track(handle, program.info);
    REQUIRE(tracked);
    REQUIRE(tracked->index.Resolve() == index.Resolve());
    REQUIRE(tracked->stride == 160);
    REQUIRE(tracked->offset == 152);
    Shader::HostTranslateInfo host;
    host.ApplyDescriptorLimitPolicy();
    host.support_buffer_texture_handles = true;
    if (failure == 1) env.size -= 1;
    if (failure == 2) env.high = 1;
    if (failure == 3) host.max_per_stage_descriptor_sampled_images = 255;
    if (failure == 4) host.support_buffer_texture_handles = false;
    if (failure) {
        REQUIRE_THROWS_AS(Shader::Optimization::TexturePass(env, program, host), Shader::NotImplementedException);
    } else {
        REQUIRE_NOTHROW(Shader::Optimization::TexturePass(env, program, host));
        REQUIRE(program.info.texture_descriptors.size() == 1);
        const auto& desc = program.info.texture_descriptors.front();
        REQUIRE(desc.count == 256);
        REQUIRE(desc.cbuf_index == 0);
        REQUIRE(desc.cbuf_offset == 0x510);
        REQUIRE(desc.buffer_source.enabled);
        REQUIRE(desc.buffer_source.offset == 152);
        REQUIRE(desc.buffer_source.stride == 160);
        REQUIRE(desc.buffer_source.size == 40960);
        REQUIRE(desc.buffer_source.width_query == query);
        for (const auto& inst : block.Instructions()) {
            if (inst.GetOpcode() == IR::Opcode::ImageSampleExplicitLod || inst.GetOpcode() == IR::Opcode::ImageQueryDimensions)
                REQUIRE(inst.Arg(0).Resolve() == index.Resolve());
        }
    }
}


TEST_CASE("Buffer texture choices preserve their select and Phi indices", "[native-shader]") {
    const bool phi_choice = GENERATE(false, true);
    const u32 invalid = GENERATE(0U, 1U, 2U);
    class RootEnvironment final : public EmptyEnvironment {
    public:
        u32 ReadCbufValue(u32 bank, u32 offset) override {
            REQUIRE(bank == 0);
            if (offset == 0x518) return 40960;
            REQUIRE(offset == 0x51c);
            return 0;
        }
    } env;
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block left{pool}, right{pool}, merge{pool};
    IR::Value first_index, second_index;
    const auto make_handle = [&](IR::Block& block, IR::Reg reg, u32 field, IR::Value& original_index) {
        IR::IREmitter ir{block};
        const auto pointer = ir.GetCbuf(ir.Imm32(0U), ir.Imm32(0x510U));
        const auto index = ir.GetReg(reg);
        original_index = index;
        const auto relative = ir.IAdd(ir.IMul(index, ir.Imm32(160U)), ir.Imm32(field));
        const auto offset = ir.ISub(ir.IAdd(pointer, relative), ir.BitwiseAnd(pointer, ir.Imm32(0xfffffff0U)));
        const auto load = block.PrependNewInst(block.end(), IR::Opcode::LoadStorage64, {ir.Imm32(0U), offset});
        return IR::U32{ir.CompositeExtract(IR::Value{&*load}, 0)};
    };
    const auto first = make_handle(left, IR::Reg::R1, 152, first_index);
    const auto second = make_handle(phi_choice ? right : left, IR::Reg::R2, invalid == 2 ? 144 : 152, second_index);
    IR::IREmitter ir{merge};
    IR::IREmitter other_ir{phi_choice ? right : merge};
    const IR::U32 other = invalid == 1 ? other_ir.GetReg(IR::Reg::R8) : second;
    IR::U32 handle;
    if (phi_choice) {
        const auto phi = merge.PrependNewInst(merge.begin(), IR::Opcode::Phi);
        phi->SetFlags(IR::Type::U32);
        phi->AddPhiOperand(&left, first);
        phi->AddPhiOperand(&right, other);
        handle = IR::U32{IR::Value{&*phi}};
    } else handle = IR::U32{ir.Select(ir.GetPred(IR::Pred::P0), first, other)};
    IR::TextureInstInfo flags{};
    flags.type.Assign(Shader::TextureType::ColorCube);
    (void)ir.ImageSampleExplicitLod(handle,
        ir.CompositeConstruct(ir.Imm32(0.0f), ir.Imm32(1.0f), ir.Imm32(0.0f)), ir.Imm32(0.0f), {}, flags);
    IR::Program program;
    program.stage = Shader::Stage::Fragment;
    program.blocks = {&left, &right, &merge};
    program.post_order_blocks = {&merge, &right, &left};
    program.info.storage_buffers_descriptors.push_back({0, 0x510, 1, false});
    Shader::HostTranslateInfo host;
    host.ApplyDescriptorLimitPolicy();
    host.support_buffer_texture_handles = true;
    if (invalid) REQUIRE_THROWS_AS(Shader::Optimization::TexturePass(env, program, host), Shader::NotImplementedException);
    else {
        REQUIRE_NOTHROW(Shader::Optimization::TexturePass(env, program, host));
        u32 samples{};
        for (const auto& inst : merge.Instructions()) if (inst.GetOpcode() == IR::Opcode::ImageSampleExplicitLod) {
            ++samples;
            const auto* choice = inst.Arg(0).InstRecursive();
            REQUIRE(choice->GetOpcode() == (phi_choice ? IR::Opcode::Phi : IR::Opcode::SelectU32));
            if (phi_choice) {
                REQUIRE(choice->PhiBlock(0) == &left);
                REQUIRE(choice->PhiBlock(1) == &right);
                REQUIRE(choice->Arg(0).Resolve() == first_index.Resolve());
                REQUIRE(choice->Arg(1).Resolve() == second_index.Resolve());
            } else {
                REQUIRE(choice->Arg(0).InstRecursive()->GetOpcode() == IR::Opcode::GetPred);
                REQUIRE(choice->Arg(1).Resolve() == first_index.Resolve());
                REQUIRE(choice->Arg(2).Resolve() == second_index.Resolve());
            }
        }
        REQUIRE(samples == 1);
    }
}


TEST_CASE("Native uniform comparisons and selects preserve independent predicate banks", "[native-shader]") {
    const std::array<Native, 7> input{{
        {{0x09057891, 0x40, 0x0f8e303f, 0}},
        {{0x0500728c, 7, 0x0bf21070, 0}},
        {{0x05057287, 7, 0x08800000, 0}},
        {{0x05057290, 7, 0x0fffe13f, 0}},
        {{0x0400728c, 6, 0x0bf01070, 0}},
        {{0x04047287, 6, 0x08000000, 0}},
        VertexTransform.back(),
    }};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u32 width : {100U, 256U}) {
        ScalarNativeMachine machine;
        machine.regs[225] = 2;
        machine.regs[223] = width;
        machine.regs[220] = 40;
        machine.regs[222] = 32;
        for (u32 p = 0; p < 7; ++p) machine.predicates[p] = (p & 1) != 0;
        const auto original = machine.predicates;
        machine.Execute(block);
        REQUIRE(machine.regs[221] == width - std::min(192U, width));
        REQUIRE(machine.regs[220] == 32);
        REQUIRE(((machine.regs[239] & 0x20) != 0) == (192U < width));
        REQUIRE((machine.regs[239] & 0x40) == 0);
        REQUIRE(machine.predicates == original);
    }
}


TEST_CASE("Native global stores carry a register offset into the high address word", "[native-shader]") {
    const std::array<Native, 2> input{{{{0x02007986, 9, 0x0810a906, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    ScalarNativeMachine machine;
    machine.regs[222] = 0xfffffff0;
    machine.regs[223] = 0x21;
    machine.regs[2] = 0x30;
    machine.regs[9] = 0xdeadbeef;
    machine.Execute(block);
    REQUIRE(machine.global == std::unordered_map<u64, u32>{{0x2200000020ULL, 0xdeadbeef}});
}


TEST_CASE("Native high LEA adds the base after unsigned scaling", "[native-shader]") {
    const bool uniform = GENERATE(false, true);
    const bool signed_scale = GENERATE(false, true);
    std::array<Native, 2> input{{
        {{0x00007211, 1, 0x078ff8ff, 0}},
        VertexTransform.back(),
    }};
    if (uniform) {
        input[0].words[0] = 0x7c11;
        input[0].words[1] = 4;
        input[0].words[2] |= 0x08000000;
    }
    if (signed_scale) input[0].words[2] |= 0x200;
    const u32 shift = GENERATE(0U, 1U, 16U, 28U, 31U);
    input[0].words[2] = (input[0].words[2] & ~(31U << 11)) | (shift << 11);
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u32 value : {0U, 1U, 0x80000000U, 0xffffffffU}) {
        for (u32 base : {0U, 1U, 0xffffffffU}) {
            ScalarNativeMachine machine;
            machine.regs[0] = value;
            machine.regs[uniform ? 220 : 1] = base;
            machine.Execute(block);
            const u64 extended = signed_scale ? static_cast<u64>(static_cast<s64>(static_cast<s32>(value))) : u64{value};
            REQUIRE(machine.regs[0] == static_cast<u32>((extended << shift) >> 32) + base);
        }
    }
}


TEST_CASE("Native LEA predicate carry reaches the high pointer word", "[native-shader]") {
    const std::array<Native, 4> input{{
        {{0x00007a11, 0x800, 0x078010ff, 0}},
        {{0xff017a10, 0x900, 0x007fe4ff, 0}},
        {{0x00007386, 7, 0x0010a900, 0}},
        VertexTransform.back(),
    }};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u32 base : {0x100U, 0xfffffff0U}) {
        ScalarNativeMachine machine;
        machine.regs[0] = 8;
        machine.regs[7] = 0x12345678;
        machine.constants[0x20] = base;
        machine.constants[0x24] = 0x21;
        machine.Execute(block);
        REQUIRE(machine.global == std::unordered_map<u64, u32>{{(u64{0x21} << 32) + base + 32, 0x12345678}});
    }
}


TEST_CASE("Native shared atomics and barriers retain real shared-memory effects", "[native-shader]") {
    const std::array<Native, 6> input{{
        {{0x03007388, 0x407, 0x4800, 0}},
        {{0x03097984, 0x400, 0x4800, 0}},
        {{0xff0a738c, 0x7fc09, 0, 0}},
        {{0x00007992, 0, 0x5000, 0}},
        {{0x00007b1d, 0, 0, 0}},
        VertexTransform.back(),
    }};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    ScalarNativeMachine machine;
    machine.regs[3] = 3;
    machine.regs[7] = 5;
    machine.shared[0x7fc] = 100;
    machine.Execute(block);
    REQUIRE(machine.shared.at(16) == 5);
    REQUIRE(machine.shared.at(0x7fc) == 105);
    REQUIRE(machine.regs[10] == 100);
    REQUIRE(machine.workgroup_barriers == 1);
    REQUIRE(machine.device_barriers == 1);
}


TEST_CASE("Native uniform high shifts and multiply operands retain numeric values", "[native-shader]") {
    std::array<Native, 7> input{{
        {{0x00057882, 123, 0, 0}},
        {{0x3f047899, 5, 0x08010605, 0}},
        {{0x05057899, 5, 0x0800063f, 0}},
        {{0x01027c24, 5, 0x0f8e0202, 0}},
        {{0x00007c17, 4, 0x0b800200, 0}},
        {{0x000a7424, 4, 0x078e0205, 0}},
        VertexTransform.back(),
    }};
    for (u32 value : {0U, 1U, 123U, 0x07ffffffU, 0xffffffffU}) {
        input[0].words[1] = value;
        Shader::ObjectPool<IR::Inst> pool;
        IR::Block block{pool};
        TranslateScalarNative(input, block);
        ScalarNativeMachine machine;
        machine.regs[0] = 5000;
        machine.regs[1] = 11;
        machine.regs[2] = 7;
        machine.regs[5] = 3;
        machine.Execute(block);
        const u32 shifted = value << 5;
        const u32 minimum = static_cast<u32>(std::min(5000, static_cast<s32>(shifted)));
        REQUIRE(machine.regs[220] == shifted);
        REQUIRE(machine.regs[221] == shifted);
        REQUIRE(machine.regs[2] == 11U * shifted + 7);
        REQUIRE(machine.regs[0] == minimum);
        REQUIRE(machine.regs[10] == minimum * 3U + 4);
    }
}


TEST_CASE("Native uniform immediate multiply preserves wraparound and aliased operands", "[native-shader]") {
    const u32 multiplier = GENERATE(0U, 1U, 5U, 48U, 0xffffffffU);
    const std::array<Native, 2> input{{
        {{0x040478a4, multiplier, 0x0f8e023f, 0}}, VertexTransform.back(),
    }};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u32 value : {0U, 1U, 123U, 0x80000000U, 0xffffffffU}) {
        ScalarNativeMachine machine;
        machine.regs[220] = value;
        machine.regs[221] = 0xcafebeef;
        machine.predicates.fill(true);
        machine.Execute(block);
        REQUIRE(machine.regs[220] == value * multiplier);
        REQUIRE(machine.regs[221] == 0xcafebeef);
        REQUIRE(std::ranges::all_of(machine.predicates, [](bool p) { return p; }));
    }
}


TEST_CASE("Native integer absolute values preserve the minimum integer and source aliases", "[native-shader]") {
    const u32 destination = GENERATE(5U, 17U);
    const std::array<Native, 2> input{{
        {{0x7213U | (destination << 16), 17, 0, 0}}, VertexTransform.back(),
    }};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u32 bits : {0U, 1U, 0xffffffffU, 0x80000000U, 0x7fffffffU, 0xfedcba98U}) {
        ScalarNativeMachine machine;
        machine.regs[17] = bits;
        machine.regs[18] = 0x12345678;
        machine.Execute(block);
        const u32 expected = bits & 0x80000000U ? 0U - bits : bits;
        REQUIRE(machine.regs[destination] == expected);
        REQUIRE(machine.regs[17] == (destination == 17 ? expected : bits));
        REQUIRE(machine.regs[18] == 0x12345678);
    }
}


TEST_CASE("Native constant integer absolute values preserve signed boundaries", "[native-shader]") {
    const u32 destination = GENERATE(5U, 17U);
    const std::array<Native, 2> input{{
        {{0x7a13U | (destination << 16), 0x00c00100, 0, 0}}, VertexTransform.back(),
    }};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u32 bits : {0U, 1U, 0xffffffffU, 0x80000000U, 0x7fffffffU, 0xfedcba98U}) {
        ScalarNativeMachine machine;
        machine.constants[(u64{3} << 32) | 4] = bits;
        machine.regs[18] = 0x12345678;
        machine.Execute(block);
        REQUIRE(machine.regs[destination] == ((bits & 0x80000000U) ? 0U - bits : bits));
        REQUIRE(machine.regs[18] == 0x12345678);
    }
    for (const Native invalid : {
             Native{{0x00057a13, 0x00c00140, 0, 0}}, // Unaligned constant address.
             Native{{0x00057a13, 0x40c00100, 0, 0}}, // Unsupported source modifier.
             Native{{0x01057a13, 0x00c00100, 0, 0}}, // Reserved source register.
             Native{{0x00057a13, 0x00c00100, 1, 0}}}) {
        const std::array<Native, 2> rejected{{invalid, VertexTransform.back()}};
        REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(rejected));
    }
}


TEST_CASE("Native predicate AND with an inverted aliased input preserves other predicates", "[native-shader]") {
    const std::array<Native, 2> input{{
        {{0x781c, 0, 0x00f20170, 0}}, VertexTransform.back(),
    }};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (bool p0 : {false, true}) for (bool p1 : {false, true}) {
        ScalarNativeMachine machine;
        for (u32 i = 0; i < 7; ++i) machine.predicates[i] = (i & 1) != 0;
        machine.predicates[0] = p0;
        machine.predicates[1] = p1;
        auto expected = machine.predicates;
        expected[1] = p0 && !p1;
        machine.Execute(block);
        REQUIRE(machine.predicates == expected);
    }
}


TEST_CASE("Native bit reversal and register shifts preserve FFT indexing", "[native-shader]") {
    const std::array<Native, 6> input{{
        {{0x00037301, 19, 0, 0}},
        {{0x03017211, 255, 0x078f50ff, 0}},
        {{0xff087219, 4, 0x00011613, 0}},
        {{0x0f0f7219, 12, 0x00000eff, 0}},
        {{0xff047224, 255, 0x078e0a0c, 0}},
        VertexTransform.back(),
    }};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u32 shift : {0U, 1U, 10U, 31U, 32U, 33U, 63U}) {
        ScalarNativeMachine machine;
        machine.regs[19] = 0x12345678;
        machine.regs[4] = shift;
        machine.regs[12] = shift;
        machine.regs[15] = 0x9abcdef0;
        machine.Execute(block);
        REQUIRE(machine.regs[3] == 0x1e6a2c48);
        REQUIRE(machine.regs[1] == (0x1e6a2c48U >> 22));
        REQUIRE(machine.regs[8] == (shift < 32 ? 0x12345678U >> shift : 0));
        REQUIRE(machine.regs[15] == (0x9abcdef0U << (shift & 31)));
        REQUIRE(machine.regs[4] == 0U - shift);
    }
}


TEST_CASE("Native constant shift counts wrap at the word width", "[native-shader]") {
    const std::array<Native, 2> input{{
        {{0xff097a19, 0x01003500, 0x00011e0a, 0}}, VertexTransform.back(),
    }};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u32 shift : {0U, 1U, 10U, 31U, 32U, 33U, 63U, 0xffffffffU}) {
        ScalarNativeMachine machine;
        machine.regs[10] = 0xfedcba98;
        machine.constants[(u64{4} << 32) | 0xd4] = shift;
        machine.Execute(block);
        REQUIRE(machine.regs[9] == (0xfedcba98U >> (shift & 31)));
        REQUIRE(machine.regs[10] == 0xfedcba98);
    }
}


TEST_CASE("Native global vector loads preserve both address words and each result channel", "[native-shader]") {
    const bool pair = GENERATE(false, true);
    const u32 offset = GENERATE(0U, 16U, 0xfffff0U);
    const u32 destination = GENERATE(8U, 16U);
    const std::array<Native, 2> input{{
        {{0x08007381U | (destination << 16), offset << 8, pair ? 0x001e8b00U : 0x001e8d00U, 0}},
        VertexTransform.back(),
    }};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    std::vector<u32> reads, writes, channels;
    u32 loads{};
    for (const auto& inst : block.Instructions()) {
        if (inst.GetOpcode() == IR::Opcode::GetRegister) reads.push_back(static_cast<u32>(inst.Arg(0).Reg()));
        if (inst.GetOpcode() == (pair ? IR::Opcode::LoadGlobal64 : IR::Opcode::LoadGlobal128)) {
            ++loads;
            const auto* address = inst.Arg(0).Inst();
            REQUIRE(address->GetOpcode() == IR::Opcode::IAdd64);
            REQUIRE(address->Arg(1).U64() == static_cast<u64>(static_cast<s64>(static_cast<s32>(offset << 8) >> 8)));
        }
        if (inst.GetOpcode() == IR::Opcode::SetRegister) {
            writes.push_back(static_cast<u32>(inst.Arg(0).Reg()));
            const auto* extract = inst.Arg(1).Inst();
            REQUIRE(extract->GetOpcode() == (pair ? IR::Opcode::CompositeExtractU32x2 : IR::Opcode::CompositeExtractU32x4));
            channels.push_back(extract->Arg(1).U32());
        }
    }
    REQUIRE(loads == 1);
    std::ranges::sort(reads);
    REQUIRE(reads == std::vector<u32>{8, 9});
    REQUIRE(writes == (pair ? std::vector<u32>{destination, destination + 1} : std::vector<u32>{destination, destination + 1, destination + 2, destination + 3}));
    REQUIRE(channels == (pair ? std::vector<u32>{0, 1} : std::vector<u32>{0, 1, 2, 3}));
    auto invalid = input;
    invalid[0].words[0] |= 1U << 24;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(invalid));
}


TEST_CASE("Native shared atomic OR retains the shared offset and source bits", "[native-shader]") {
    const std::array<Native, 2> input{{
        {{0xffff738c, 0x6807, 0x03000000, 0}}, VertexTransform.back(),
    }};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u32 initial : {0U, 0x12345678U, 0xffffffffU}) {
        for (u32 operand : {0U, 0x87654321U, 0xffffffffU}) {
            ScalarNativeMachine machine;
            machine.regs[7] = operand;
            machine.shared[0x68] = initial;
            machine.Execute(block);
            REQUIRE(machine.shared.at(0x68) == (initial | operand));
            REQUIRE(machine.regs[7] == operand);
        }
    }
}


TEST_CASE("Native immediate high-word shifts preserve wrapped and clamped bit masks", "[native-shader]") {
    const bool wrap = GENERATE(false, true);
    const u32 value = GENERATE(1U, 0x87654321U, 0xffffffffU);
    const std::array<Native, 2> input{{
        {{0xff057419, value, 0x00010603U | (u32{wrap} << 11), 0}}, VertexTransform.back(),
    }};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u32 shift : {0U, 1U, 10U, 31U, 32U, 33U, 63U, 0xffffffffU}) {
        ScalarNativeMachine machine;
        machine.regs[3] = shift;
        machine.Execute(block);
        REQUIRE(machine.regs[5] == (wrap ? (value << (shift & 31)) :
                                             (shift < 32 ? value << shift : 0U)));
        REQUIRE(machine.regs[3] == shift);
    }
}


TEST_CASE("Native scalar global loads retain address carry and signed offsets", "[native-shader]") {
    const u32 offset = GENERATE(0U, 16U, 0xfffff0U);
    const std::array<Native, 2> input{{
        {{0x001d7381, offset << 8, 0x001e8900, 0}}, VertexTransform.back(),
    }};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    ScalarNativeMachine machine;
    machine.regs[0] = 0xfffffff8;
    machine.regs[1] = 0x21;
    const u64 displacement = static_cast<u64>(static_cast<s64>(static_cast<s32>(offset << 8) >> 8));
    machine.global[0x21fffffff8ULL + displacement] = 0x87654321;
    machine.Execute(block);
    REQUIRE(machine.regs[29] == 0x87654321);
    REQUIRE(machine.regs[0] == 0xfffffff8);
    REQUIRE(machine.regs[1] == 0x21);
}


TEST_CASE("Native global loads combine a uniform pointer with the scalar byte displacement", "[native-shader]") {
    const bool wide_offset = GENERATE(false, true);
    const u32 offset = GENERATE(0U, 16U, 0xfffff0U);
    const u32 source = wide_offset ? 32U : 33U;
    const std::array<Native, 2> input{{
        {{0x001d7981U | (source << 24), 4U | (offset << 8),
          0x081e8900U | (u32{wide_offset} << 26), 0}}, VertexTransform.back(),
    }};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    ScalarNativeMachine machine;
    machine.regs[220] = 0xfffffff8;
    machine.regs[221] = 0x21;
    machine.regs[32] = 16;
    machine.regs[33] = wide_offset ? 1 : 16;
    const u64 displacement = static_cast<u64>(static_cast<s64>(static_cast<s32>(offset << 8) >> 8));
    machine.global[0x21fffffff8ULL + (wide_offset ? 0x100000010ULL : 16ULL) + displacement] = 0x87654321;
    machine.Execute(block);
    REQUIRE(machine.regs[29] == 0x87654321);
    REQUIRE(machine.regs[220] == 0xfffffff8);
    REQUIRE(machine.regs[221] == 0x21);
    REQUIRE(machine.regs[source] == 16);
}


TEST_CASE("Native paired shared values keep both words under scaled addressing", "[native-shader]") {
    const std::array<Native, 3> input{{
        {{0x13007388, 4, 0x8a00, 0}},
        {{0x13027984, 0, 0x8a00, 0}},
        VertexTransform.back(),
    }};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    ScalarNativeMachine machine;
    machine.regs[19] = 17;
    machine.regs[4] = 0x12345678;
    machine.regs[5] = 0x9abcdef0;
    machine.Execute(block);
    REQUIRE(machine.shared.at(17 * 8) == 0x12345678);
    REQUIRE(machine.shared.at(17 * 8 + 4) == 0x9abcdef0);
    REQUIRE(machine.regs[2] == 0x12345678);
    REQUIRE(machine.regs[3] == 0x9abcdef0);
}


TEST_CASE("Native formatted surface stores retain coordinates channels and descriptor", "[native-shader]") {
    const bool strong = GENERATE(false, true);
    const std::array<Native, 2> input{{{{0x00007b9b, 0x60000a04, strong ? 0x0010af00U : 0x00100f00U, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    u32 writes{}, barriers{};
    for (const auto& inst : block.Instructions()) {
        if (inst.GetOpcode() == IR::Opcode::DeviceMemoryBarrier) {
            REQUIRE(barriers == writes);
            ++barriers;
        }
        if (inst.GetOpcode() == IR::Opcode::BoundImageWrite) {
        REQUIRE(barriers == static_cast<u32>(strong));
        REQUIRE(inst.Arg(0).U32() == 40);
        const auto info = inst.Flags<IR::TextureInstInfo>();
        REQUIRE(info.type.Value() == Shader::TextureType::Color2D);
        const auto* coordinates = inst.Arg(1).Inst();
        REQUIRE(coordinates->Arg(0).Inst()->Arg(0).Reg() == IR::Reg::R0);
        REQUIRE(coordinates->Arg(1).Inst()->Arg(0).Reg() == IR::Reg::R1);
        const auto* color = inst.Arg(2).Inst();
        for (u32 component = 0; component < 4; ++component)
            REQUIRE(static_cast<u32>(color->Arg(component).Inst()->Arg(0).Reg()) == 4 + component);
        ++writes;
    }
    }
    REQUIRE(writes == 1);
    REQUIRE(barriers == (strong ? 2U : 0U));
}


TEST_CASE("Native predicated predicate copies retain their guard and source", "[native-shader]") {
    const std::array<Native, 2> input{{{{0x0000281c, 0, 0x01f0f070, 0}}, VertexTransform.back()}};
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(lowered);
    const auto predicate = Shader::Maxwell::Instruction{lowered.instructions.front()}.Pred();
    REQUIRE(predicate.index == 2);
    REQUIRE_FALSE(predicate.negated);
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (bool source : {false, true}) {
        ScalarNativeMachine machine;
        machine.predicates[3] = source;
        machine.Execute(block);
        REQUIRE(machine.predicates[0] == source);
    }
}


TEST_CASE("Native NTZ nearest conversion rounds ties to even and clears NaNs", "[native-shader]") {
    const std::array<Native, 2> input{{{{0x00117305, 2, 0x00203100, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    u32 rounds{};
    for (const auto& inst : block.Instructions()) if (inst.GetOpcode() == IR::Opcode::FPRoundEven32) {
        REQUIRE(inst.Flags<IR::FpControl>().fmz_mode == IR::FmzMode::None);
        ++rounds;
    }
    REQUIRE(rounds == 1);
    for (const auto [value, expected] : std::array<std::pair<float, s32>, 9>{{
        {0.5f, 0}, {1.5f, 2}, {2.5f, 2}, {-0.5f, 0}, {-1.5f, -2}, {-2.5f, -2},
        {0.9f, 1}, {-0.9f, -1}, {std::numeric_limits<float>::quiet_NaN(), 0},
    }}) {
        ScalarNativeMachine machine;
        machine.regs[2] = std::bit_cast<u32>(value);
        machine.Execute(block);
        REQUIRE(machine.regs[17] == static_cast<u32>(expected));
    }
    auto unsupported = input;
    unsupported[0].words[2] ^= 1U << 13;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(unsupported));
}


TEST_CASE("Native predicate packing merges only selected predicate bits", "[native-shader]") {
    for (u32 mask : {0U, 8U, 16U, 0x55U, 0x7fU}) {
        const std::array<Native, 2> input{{{{0x02137803, mask, 0, 0}}, VertexTransform.back()}};
        Shader::ObjectPool<IR::Inst> pool;
        IR::Block block{pool};
        TranslateScalarNative(input, block);
        for (u32 bits = 0; bits < 128; ++bits) {
            ScalarNativeMachine machine;
            machine.regs[2] = 0xa5a5a5a5U;
            for (u32 i = 0; i < 7; ++i) machine.predicates[i] = (bits >> i) & 1;
            const auto saved = machine.predicates;
            machine.Execute(block);
            REQUIRE(machine.regs[19] == ((0xa5a5a5a5U & ~mask) | (bits & mask)));
            REQUIRE(machine.predicates == saved);
        }
    }
}


TEST_CASE("Native signed NTZ floor conversion rounds negative fractions down", "[native-shader]") {
    const std::array<Native, 2> input{{{{0x00237305, 0x24, 0x00207100, 0x000e2400}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (const auto [value, expected] : std::array<std::pair<float, s32>, 8>{{
        {1.9f, 1}, {-1.1f, -2}, {-0.1f, -1}, {0.9f, 0},
        {0.0f, 0}, {-2.0f, -2}, {12345.5f, 12345},
        {std::numeric_limits<float>::quiet_NaN(), 0},
    }}) {
        ScalarNativeMachine machine;
        machine.regs[36] = std::bit_cast<u32>(value);
        machine.Execute(block);
        REQUIRE(machine.regs[35] == static_cast<u32>(expected));
    }
}


TEST_CASE("Native dependency waits retain the full six-bit pending-operation count", "[native-shader]") {
    for (u32 count : {0U, 1U, 15U, 22U, 63U}) {
        const std::array<Native, 2> input{{{{0x0000791a, 0xd000U | (count << 6), 0, 0}}, VertexTransform.back()}};
        const auto lowered = Shader::Ampere::LowerToMaxwell(input);
        REQUIRE(lowered);
        REQUIRE(lowered.instructions.size() == 1);
    }
    const std::array<Native, 2> unsupported{{{{0x0000791a, 0xc000, 0, 0}}, VertexTransform.back()}};
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(unsupported));
}


TEST_CASE("Native histogram warp match compares both address words and preserves predicates", "[native-shader]") {
    const bool wide = GENERATE(false, true);
    const u32 target = GENERATE(0U, 1U, 5U);
    const u32 source = wide ? 0U : 3U;
    const std::array<Native, 2> input{{{{0x00ff73a1U | (source << 24), 0,
        (target << 17) | (wide ? 0x200U : 0U), 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u32 active : {1U, 0x80000000U, 0x80000001U, 0x55555555U, 0xffffffffU}) {
        for (u32 mismatch : {0U, 1U, 2U}) {
            std::array<std::array<u32, IR::NUM_REGS>, 32> registers{};
            std::array<std::array<bool, 7>, 32> predicates{};
            const u32 first = std::countr_zero(active);
            for (u32 lane = 0; lane < 32; ++lane) {
                registers[lane][source] = 0xfffffff0U + (mismatch == 1 && lane != first ? 4 : 0);
                registers[lane][source + 1] = 0x21U + (mismatch == 2 && lane != first ? 1 : 0);
                for (u32 p = 0; p < 7; ++p) predicates[lane][p] = ((lane + p) & 1) != 0;
            }
            const auto original = predicates;
            ExecuteNativeWarp(block, active, registers, predicates);
            for (u32 lane = 0; lane < 32; ++lane) {
                auto expected = original[lane];
                if (active & (1U << lane)) expected[target] = mismatch == 0 || (!wide && mismatch == 2) || std::popcount(active) == 1;
                REQUIRE(predicates[lane] == expected);
            }
        }
    }
}


TEST_CASE("Native warp sum OR and unsigned extrema reduce only active lanes", "[native-shader]") {
    const u32 operation = GENERATE(0U, 1U, 2U, 3U);
    std::array<Native, 2> input{{{{0x030573c4, 0, 0x0000c000, 0}}, VertexTransform.back()}};
    if (operation == 1) input[0].words[2] = 0x4000;
    if (operation == 2) input[0].words[2] = 0x10000;
    if (operation == 3) input[0].words[2] = 0x14000;
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u32 active : {1U, 0x80000000U, 0x80000001U, 0x55555555U, 0xffffffffU}) {
        std::array<std::array<u32, IR::NUM_REGS>, 32> registers{};
        std::array<std::array<bool, 7>, 32> predicates{};
        u32 sum = operation == 2 ? 0xffffffffU : 0U;
        for (u32 lane = 0; lane < 32; ++lane) {
            registers[lane][3] = 0xfffffff0U + lane * 17;
            registers[lane][221] = 0xcafebeef;
            if (active & (1U << lane)) sum = operation == 3 ? std::max(sum, registers[lane][3]) : operation == 2 ? std::min(sum, registers[lane][3]) : operation == 1 ? sum | registers[lane][3] : sum + registers[lane][3];
            for (u32 p = 0; p < 7; ++p) predicates[lane][p] = ((lane + p) & 1) != 0;
        }
        const auto original = predicates;
        ExecuteNativeWarp(block, active, registers, predicates);
        REQUIRE(predicates == original);
        for (u32 lane = 0; lane < 32; ++lane)
            REQUIRE(registers[lane][221] == ((active & (1U << lane)) ? sum : 0xcafebeef));
    }
}


TEST_CASE("Native histogram pointer carry and global reduction retain the address and addend", "[native-shader]") {
    const std::array<Native, 4> input{{
        {{0x00007a10, 0x00001000, 0x07f1e0ff, 0}},
        {{0xff017624, 0x00001100, 0x000e06ff, 0}},
        {{0x0000798e, 3, 0x0010e100, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u32 low : {0U, 0xfffffff0U, 0xffffffffU}) {
        ScalarNativeMachine machine;
        machine.regs[0] = 32;
        machine.regs[3] = 17;
        machine.constants[0x40] = low;
        machine.constants[0x44] = 0x21;
        const u64 address = 0x2100000000ULL + low + 32;
        machine.global[address] = 0xfffffff8U;
        machine.Execute(block);
        REQUIRE(machine.regs[0] == static_cast<u32>(address));
        REQUIRE(machine.regs[1] == address >> 32);
        REQUIRE(machine.predicates[0] == (low > 0xffffffdfU));
        REQUIRE(machine.global == std::unordered_map<u64, u32>{{address, 9}});
    }
}


TEST_CASE("Native histogram shared increment scales its address and increments once", "[native-shader]") {
    const std::array<Native, 2> input{{{{0x03ff7f8c, 0, 0x0d00403f, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    ScalarNativeMachine machine;
    machine.regs[3] = 63;
    machine.shared[252] = 0xffffffffU;
    machine.Execute(block);
    REQUIRE(machine.shared == std::unordered_map<u64, u32>{{252, 0}});
}


TEST_CASE("Native histogram uniform leading bit and comparison select the highest active lane", "[native-shader]") {
    const std::array<Native, 3> input{{{{0x000472bd, 4, 0x080e0000, 0}},
        {{0x02007c0c, 4, 0x0bf02070, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u32 mask : {1U, 0x80000000U, 0x55555555U, 0xffffffffU}) {
        for (u32 lane = 0; lane < 32; ++lane) {
            ScalarNativeMachine machine;
            machine.regs[220] = mask;
            machine.regs[2] = lane;
            machine.Execute(block);
            REQUIRE(machine.regs[220] == 31U - std::countl_zero(mask));
            REQUIRE(machine.predicates[0] == (lane == 31U - std::countl_zero(mask)));
        }
    }
}


TEST_CASE("Native formatted surface loads retain selected channels coordinates descriptor and guard", "[native-shader]") {
    const Native instruction = GENERATE(Native{{0x400a0b97, 0x60001200, 0x001e0100, 0}},
                                        Native{{0x12250b97, 0x60001200, 0x001e0100, 0}},
                                        Native{{0x06087b97, 0x60002000, 0x001e0f00, 0}},
                                        Native{{0x06047b97, 0x60000800, 0x001e0300, 0}});
    const std::array<Native, 2> input{{instruction, VertexTransform.back()}};
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(lowered);
    REQUIRE(lowered.required_texture_buffer == 0);
    const auto guard = Shader::Maxwell::Instruction{lowered.instructions.front()}.Pred();
    REQUIRE(guard.index == instruction.Bits(12, 3));
    REQUIRE_FALSE(guard.negated);
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    u32 loads{}, writes{};
    for (const auto& inst : block.Instructions()) {
        if (inst.GetOpcode() == IR::Opcode::BoundImageRead) {
            REQUIRE(inst.Arg(0).U32() == instruction.Bits(40, 14) * 4);
            REQUIRE(inst.Flags<IR::TextureInstInfo>().type.Value() == Shader::TextureType::Color2D);
            const auto* coords = inst.Arg(1).Inst();
            REQUIRE(static_cast<u32>(coords->Arg(0).Inst()->Arg(0).Reg()) == instruction.Bits(24, 8));
            REQUIRE(static_cast<u32>(coords->Arg(1).Inst()->Arg(0).Reg()) == instruction.Bits(24, 8) + 1);
            ++loads;
        }
        if (inst.GetOpcode() == IR::Opcode::SetRegister) {
            REQUIRE(static_cast<u32>(inst.Arg(0).Reg()) == instruction.Bits(16, 8) + writes);
            const auto* red = inst.Arg(1).Inst();
            REQUIRE(red->GetOpcode() == IR::Opcode::CompositeExtractU32x4);
            REQUIRE(red->Arg(1).U32() == writes);
            REQUIRE(red->Arg(0).Inst()->GetOpcode() == IR::Opcode::BoundImageRead);
            ++writes;
        }
    }
    REQUIRE(loads == 1);
    REQUIRE(writes == static_cast<u32>(std::popcount(instruction.Bits(72, 4))));
    auto invalid = input;
    invalid[0].words[2] |= 0x1000; // Unsupported surface-load modifier.
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(invalid));
}


TEST_CASE("Native volume texel fetch preserves XYZ LOD descriptor and split RGBA outputs", "[native-shader]") {
    const std::array<Native, 2> input{{{{0x00087b66, 0x50000a02, 0x049e0f0a, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    u32 fetches{};
    std::vector<u32> writes;
    for (const auto& inst : block.Instructions()) {
        if (inst.GetOpcode() == IR::Opcode::BoundImageFetch) {
            REQUIRE(inst.Arg(0).U32() == 40);
            REQUIRE(inst.Flags<IR::TextureInstInfo>().type.Value() == Shader::TextureType::Color3D);
            const auto* coords = inst.Arg(1).Inst();
            for (u32 i = 0; i < 3; ++i)
                REQUIRE(static_cast<u32>(coords->Arg(i).Inst()->Arg(0).Reg()) == i);
            REQUIRE(inst.Arg(3).U32() == 0);
            ++fetches;
        }
        if (inst.GetOpcode() == IR::Opcode::SetRegister) {
            writes.push_back(static_cast<u32>(inst.Arg(0).Reg()));
            const auto* extract = inst.Arg(1).Inst()->Arg(0).Inst();
            REQUIRE(extract->GetOpcode() == IR::Opcode::CompositeExtractF32x4);
            REQUIRE(extract->Arg(1).U32() == writes.size() - 1);
        }
    }
    REQUIRE(fetches == 1);
    REQUIRE(writes == std::vector<u32>{8, 9, 10, 11});
    auto invalid = input;
    invalid[0].words[0] |= 1U << 24;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(invalid));
}


TEST_CASE("Native volume surface store retains XYZ and all four data channels", "[native-shader]") {
    const std::array<Native, 2> input{{{{0x00007b9b, 0xa0000c04, 0x00100f00, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    u32 stores{};
    for (const auto& inst : block.Instructions()) if (inst.GetOpcode() == IR::Opcode::BoundImageWrite) {
        REQUIRE(inst.Arg(0).U32() == 48);
        REQUIRE(inst.Flags<IR::TextureInstInfo>().type.Value() == Shader::TextureType::Color3D);
        for (u32 i = 0; i < 3; ++i)
            REQUIRE(static_cast<u32>(inst.Arg(1).Inst()->Arg(i).Inst()->Arg(0).Reg()) == i);
        for (u32 i = 0; i < 4; ++i)
            REQUIRE(static_cast<u32>(inst.Arg(2).Inst()->Arg(i).Inst()->Arg(0).Reg()) == 4 + i);
        ++stores;
    }
    REQUIRE(stores == 1);
}


TEST_CASE("Native half FMA ReLU preserves fusion values above one and canonical NaNs", "[native-shader]") {
    const std::array<Native, 2> input{{{{0x0d127231, 0x3000000d, 0x03848d10, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    const std::array<std::array<u16, 3>, 9> cases{{
        {{0x3c00, 0x4400, 0x4200}}, // -1*1+4 = 3, not saturated to one
        {{0x4000, 0x3c00, 0x0000}}, // -2*2+1 < 0
        {{0x3c00, 0x3c00, 0x0000}},
        {{0x0000, 0x0001, 0x0001}}, // smallest positive subnormal survives
        {{0x7c00, 0x0000, 0x0000}},
        {{0x0000, 0x7c00, 0x7c00}},
        {{0x7e01, 0x3c00, 0x7fff}},
        {{0xfe01, 0x3c00, 0x7fff}},
        {{0x8000, 0x8000, 0x0000}},
    }};
    for (const auto& values : cases) {
        ScalarNativeMachine machine;
        machine.regs[13] = (u32{values[0]} << 16) | 0x4400;
        machine.regs[16] = 0xc0000000 | values[1];
        machine.predicates = {true, false, true, false, true, false, true};
        const auto predicates = machine.predicates;
        machine.Execute(block);
        REQUIRE(machine.regs[18] == (u32{values[2]} | (u32{values[2]} << 16)));
        REQUIRE(machine.predicates == predicates);
    }
    // A separate register operand permits a fused product residual that would
    // vanish if the multiplication rounded before adding its third operand.
    auto fused = input;
    fused[0].words[1] = 0x3000000e;
    IR::Block fused_block{pool};
    TranslateScalarNative(fused, fused_block);
    ScalarNativeMachine machine;
    machine.regs[13] = 0x3c013c01;
    machine.regs[14] = 0x3bfe3bfe;
    machine.regs[16] = 0x3c003c00;
    machine.Execute(fused_block);
    REQUIRE(machine.regs[18] == 0x00100010); // 2^-20 in each half
}


TEST_CASE("Native half FMA ReLU accepts packed immediates without losing input aliases", "[native-shader]") {
    const std::array<Native, 2> input{{{{0x02027831, 0x3c003c00, 0x03968805, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (const auto [source, addend, expected] : std::array<std::array<u16, 3>, 4>{{
        {{0x4000, 0x3c00, 0x3c00}}, {{0x3c00, 0x4400, 0x0000}},
        {{0x4400, 0x3c00, 0x4200}}, {{0x7e01, 0x3c00, 0x7fff}},
    }}) {
        ScalarNativeMachine machine;
        machine.regs[2] = source | 0xc4000000U;
        machine.regs[5] = (u32{addend} << 16) | 0x4800;
        machine.Execute(block);
        REQUIRE(machine.regs[2] == (u32{expected} | (u32{expected} << 16)));
    }
}


TEST_CASE("Native quad butterfly and swizzled add retain horizontal and vertical differences", "[native-shader]") {
    const bool vertical = GENERATE(false, true);
    const std::array<Native, 3> input{{
        {{0x05107f89, vertical ? 0x0c5c0300U : 0x0c3c0300U, 0x000e0000, 0}},
        {{0x100c7822, vertical ? 0x5aU : 0x66U, 5, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    std::array<std::array<u32, IR::NUM_REGS>, 32> registers{};
    std::array<std::array<bool, 7>, 32> predicates{};
    for (u32 lane = 0; lane < 32; ++lane)
        registers[lane][5] = std::bit_cast<u32>(static_cast<float>(lane / 4 * 100 + (lane & 1) * 3 + ((lane >> 1) & 1) * 11));
    ExecuteNativeWarp(block, 0xffffffffU, registers, predicates);
    for (u32 lane = 0; lane < 32; ++lane) {
        REQUIRE(registers[lane][16] == registers[lane ^ (vertical ? 2 : 1)][5]);
        REQUIRE(std::bit_cast<float>(registers[lane][12]) == (vertical ? 11.0f : 3.0f));
    }
}


TEST_CASE("Native array gradient fetch retains array index XY derivatives descriptor and RGBA", "[native-shader]") {
    const std::array<Native, 2> input{{{{0x04047b6c, 0xa080080c, 0x001e0f06, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    u32 samples{};
    std::vector<u32> writes;
    for (const auto& inst : block.Instructions()) {
        if (inst.GetOpcode() == IR::Opcode::BoundImageGradient) {
            REQUIRE(inst.Arg(0).U32() == 32);
            REQUIRE(inst.Flags<IR::TextureInstInfo>().type.Value() == Shader::TextureType::ColorArray2D);
            REQUIRE(inst.Flags<IR::TextureInstInfo>().num_derivatives.Value() == 2);
            const auto* coords = inst.Arg(1).Inst();
            REQUIRE(coords->Arg(0).Inst()->Arg(0).Inst()->Arg(0).Reg() == IR::Reg{4});
            REQUIRE(coords->Arg(1).Inst()->Arg(0).Inst()->Arg(0).Reg() == IR::Reg{5});
            const auto* layer = coords->Arg(2).Inst()->Arg(0).Inst();
            REQUIRE(layer->GetOpcode() == IR::Opcode::BitFieldUExtract);
            REQUIRE(layer->Arg(0).Inst()->Arg(0).Reg() == IR::Reg{6});
            REQUIRE(layer->Arg(2).U32() == 16);
            for (u32 component = 0; component < 4; ++component)
                REQUIRE(inst.Arg(2).Inst()->Arg(component).Inst()->Arg(0).Inst()->Arg(0).Reg() == IR::Reg{12 + component});
            ++samples;
        }
        if (inst.GetOpcode() == IR::Opcode::SetRegister) writes.push_back(static_cast<u32>(inst.Arg(0).Reg()));
    }
    REQUIRE(samples == 1);
    REQUIRE(writes == std::vector<u32>{4, 5, 6, 7});
}


TEST_CASE("Native capture distinguishes forward branches from call targets beyond main EXIT", "[native-shader]") {
    using Capture = Shader::Ampere::ProgramCaptureState;
    Capture calls;
    REQUIRE(calls.Observe(Native{{0x7944, 48, 0x03c00000, 0}}, 1) == Capture::Result::Continue);
    REQUIRE(calls.Observe(VertexTransform.back(), 2) == Capture::Result::Continue);
    REQUIRE(calls.Observe(Native{{0x08007950, 0, 0, 0}}, 6) == Capture::Result::Complete);
    Capture branches;
    REQUIRE(branches.Observe(Native{{0x7947, 48, 0x03800000, 0}}, 0) == Capture::Result::Continue);
    REQUIRE(branches.Observe(VertexTransform.back(), 1) == Capture::Result::Continue);
    REQUIRE(branches.Observe(VertexTransform.back(), 4) == Capture::Result::Complete);
    Capture multiple;
    REQUIRE(multiple.Observe(Native{{0x7944, 96, 0x03c00000, 0}}, 1) == Capture::Result::Continue);
    REQUIRE(multiple.Observe(Native{{0x7944, 128, 0x03c00000, 0}}, 3) == Capture::Result::Continue);
    REQUIRE(multiple.Observe(VertexTransform.back(), 4) == Capture::Result::Continue);
    REQUIRE(multiple.Observe(Native{{0x08007950, 0, 0, 0}}, 9) == Capture::Result::Continue);
    REQUIRE(multiple.Observe(Native{{0x08007950, 0, 0, 0}}, 13) == Capture::Result::Complete);
    Capture invalid;
    REQUIRE(invalid.Observe(Native{{0x7947, 1, 0x03800000, 0}}, 0) == Capture::Result::InvalidTarget);
}


TEST_CASE("Native implicit volume sample preserves XYZ and all four split channels", "[native-shader]") {
    const std::array<Native, 2> input{{{{0x0a0a7b60, 0x5080400e, 0x041e0f0c, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    u32 samples{};
    std::vector<std::pair<u32, u32>> copies;
    for (const auto& inst : block.Instructions()) {
        if (inst.GetOpcode() == IR::Opcode::BoundImageSampleImplicitLod) {
            REQUIRE(inst.Arg(0).U32() == 256);
            REQUIRE(inst.Flags<IR::TextureInstInfo>().type.Value() == Shader::TextureType::Color3D);
            for (u32 i = 0; i < 3; ++i)
                REQUIRE(inst.Arg(1).Inst()->Arg(i).Inst()->Arg(0).Inst()->Arg(0).Reg() == IR::Reg{250 + i});
            ++samples;
        }
        if (inst.GetOpcode() == IR::Opcode::SetRegister && inst.Arg(1).Inst()->GetOpcode() == IR::Opcode::GetRegister)
            copies.emplace_back(static_cast<u32>(inst.Arg(0).Reg()), static_cast<u32>(inst.Arg(1).Inst()->Arg(0).Reg()));
    }
    REQUIRE(samples == 1);
    REQUIRE(copies == std::vector<std::pair<u32, u32>>{{250, 10}, {251, 11}, {252, 14}, {10, 250}, {11, 251}, {12, 252}, {13, 253}});
}


TEST_CASE("Native array shadow sampling retains layer XY depth reference and scalar destination", "[native-shader]") {
    const bool zero_lod = GENERATE(false, true);
    const u32 destination = GENERATE(8U, 10U, 46U);
    const std::array<Native, 2> input{{{{0x08007b60U | (destination << 16), 0xb080140a,
        zero_lod ? 0x009e41ffU : 0x001e41ffU, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    u32 samples{};
    std::vector<std::pair<u32, u32>> copies;
    for (const auto& inst : block.Instructions()) {
        if (inst.GetOpcode() == (zero_lod ? IR::Opcode::BoundImageSampleDrefExplicitLod : IR::Opcode::BoundImageSampleDrefImplicitLod)) {
            REQUIRE(inst.Arg(0).U32() == 80);
            REQUIRE(inst.Flags<IR::TextureInstInfo>().type.Value() == Shader::TextureType::ColorArray2D);
            REQUIRE(inst.Flags<IR::TextureInstInfo>().is_depth.Value() == 1);
            REQUIRE(inst.Arg(1).Inst()->Arg(0).Inst()->Arg(0).Inst()->Arg(0).Reg() == IR::Reg{251});
            REQUIRE(inst.Arg(1).Inst()->Arg(1).Inst()->Arg(0).Inst()->Arg(0).Reg() == IR::Reg{252});
            REQUIRE(inst.Arg(2).Inst()->Arg(0).Inst()->Arg(0).Reg() == IR::Reg{11});
            if (zero_lod) REQUIRE(inst.Arg(3).F32() == 0.0f);
            ++samples;
        }
        if (inst.GetOpcode() == IR::Opcode::SetRegister && inst.Arg(1).Inst()->GetOpcode() == IR::Opcode::GetRegister)
            copies.emplace_back(static_cast<u32>(inst.Arg(0).Reg()), static_cast<u32>(inst.Arg(1).Inst()->Arg(0).Reg()));
    }
    REQUIRE(samples == 1);
    REQUIRE(copies == std::vector<std::pair<u32, u32>>{{250, 8}, {251, 9}, {252, 10}});
    REQUIRE(block.Instructions().back().Arg(0).Reg() == IR::Reg{destination});
}


TEST_CASE("Native immediate uniform comparison and select preserve predicates at unsigned boundaries", "[native-shader]") {
    const std::array<Native, 3> input{{{{0x0400788c, 6, 0x0bf01070, 0}}, {{0x04057887, 6, 0x08000000, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u32 value : {0U, 5U, 6U, 7U, 0x80000000U, 0xffffffffU}) {
        ScalarNativeMachine machine;
        machine.regs[220] = value;
        for (u32 i = 0; i < 7; ++i) machine.predicates[i] = (i & 1) != 0;
        const auto saved = machine.predicates;
        machine.Execute(block);
        REQUIRE(((machine.regs[239] >> 6) & 1) == (value < 6));
        REQUIRE(machine.regs[221] == std::min(value, 6U));
        REQUIRE(machine.predicates == saved);
    }
}


TEST_CASE("Native uniform signed right shift sign extends the high word", "[native-shader]") {
    const std::array<Native, 2> input{{{{0x3f057899, 31, 0x08011404, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u32 value : {0U, 1U, 0x7fffffffU, 0x80000000U, 0xffffffffU}) {
        ScalarNativeMachine machine;
        machine.regs[220] = value;
        machine.Execute(block);
        REQUIRE(machine.regs[221] == ((value & 0x80000000U) ? 0xffffffffU : 0U));
    }
}


TEST_CASE("Native indexed constant load adds a signed byte displacement", "[native-shader]") {
    const std::array<Native, 2> input{{{{0x30077b82, 0x00fe0500, 0x00000800, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    ScalarNativeMachine machine;
    machine.regs[48] = 0x800;
    machine.constants[(u64{3} << 32) | 0x14] = 0x12345678;
    machine.Execute(block);
    REQUIRE(machine.regs[7] == 0x12345678);
}


TEST_CASE("Native local spill retains byte address data and execution predicate", "[native-shader]") {
    const u32 offset = GENERATE(0U, 4U, 28U);
    std::array<Native, 3> input{{{{0xff001387, 85, 0x00100800, 0}},
        {{0xff2a7983, 0, 0x00300800, 0}}, VertexTransform.back()}};
    input[0].words[1] |= offset << 8;
    input[1].words[1] |= offset << 8;
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(lowered);
    REQUIRE(Shader::Maxwell::Decode(lowered.instructions[0]) == Shader::Maxwell::Opcode::STL);
    REQUIRE(Shader::Maxwell::Instruction{lowered.instructions[0]}.Pred().index == 1);
    REQUIRE((lowered.instructions[0] & 255) == 85);
    REQUIRE((lowered.instructions[1] & 255) == 42);
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    class LocalEnvironment : public EmptyEnvironment {
    public:
        u32 LocalMemorySize() const override { return 32; }
    } env;
    Shader::Maxwell::TranslatorVisitor visitor{env, block};
    visitor.STL(lowered.instructions[0]);
    visitor.LDL(lowered.instructions[1]);
    u32 loads{}, stores{};
    for (const auto& inst : block.Instructions()) {
        if (inst.GetOpcode() == IR::Opcode::LoadLocal) {
            REQUIRE(inst.Arg(0).U32() == offset / 4);
            ++loads;
        }
        if (inst.GetOpcode() == IR::Opcode::WriteLocal) {
            REQUIRE(inst.Arg(0).U32() == offset / 4);
            REQUIRE(inst.Arg(1).Inst()->Arg(0).Reg() == IR::Reg{85});
            ++stores;
        }
    }
    REQUIRE(loads == 1);
    REQUIRE(stores == 1);
    input[0].words[1] |= 1U << 8;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(input));
}


TEST_CASE("Native scalar half conversion applies absolute value before rounding", "[native-shader]") {
    const bool negate = GENERATE(false, true);
    const std::array<Native, 2> input{{{{0x00047304, negate ? 0x80000000U : 0x40000000U, 0x00200800, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (const auto [value, expected] : std::array<std::pair<float, u32>, 7>{{
        {-1.0f, 0x3c00}, {-0.0f, 0}, {-2.5f, 0x4100},
        {1.00048828125f, 0x3c00}, {1.00146484375f, 0x3c02},
        {-0x1p-24f, 1}, {-std::numeric_limits<float>::infinity(), 0x7c00},
    }}) {
        ScalarNativeMachine machine;
        machine.regs[0] = std::bit_cast<u32>(value);
        machine.Execute(block);
        REQUIRE(machine.regs[4] == (expected | (negate && !std::signbit(value) ? 0x8000U : 0U)));
    }
}


TEST_CASE("Native predicated half ReLU guards its complete expansion including predicate scratch", "[native-shader]") {
    for (u32 pred : {1U, 6U, 9U, 14U}) {
        const std::array<Native, 2> input{{{{0x12140231U | (pred << 12), 0x20000012, 0x03848914, 0}}, VertexTransform.back()}};
        const auto lowered = Shader::Ampere::LowerToMaxwell(input);
        REQUIRE(lowered);
        REQUIRE(Shader::Maxwell::Decode(lowered.instructions.front()) == Shader::Maxwell::Opcode::BRA);
        REQUIRE(((lowered.instructions.front() >> 16) & 15) == (pred ^ 8));
        Shader::ObjectPool<IR::Inst> pool;
        IR::Block block{pool};
        EmptyEnvironment env;
        Shader::Maxwell::TranslatorVisitor visitor{env, block};
        for (size_t i = 1; i + 1 < lowered.instructions.size(); ++i) {
            const u64 word = lowered.instructions[i];
            REQUIRE(((word >> 16) & 15) == 7);
            switch (Shader::Maxwell::Decode(word)) {
#define INST(name, cute, encode) case Shader::Maxwell::Opcode::name: visitor.name(word); break;
#include "shader_recompiler/frontend/maxwell/maxwell.inc"
#undef INST
            }
        }
        for (bool value : {false, true}) {
            ScalarNativeMachine machine;
            machine.regs[18] = 0x3c003c00;
            machine.regs[20] = 0x44004400;
            machine.predicates.fill(true);
            machine.predicates[pred & 7] = value;
            const auto saved = machine.predicates;
            if (value != ((pred & 8) != 0)) machine.Execute(block);
            REQUIRE(machine.regs[20] == (value != ((pred & 8) != 0) ? 0x42004200U : 0x44004400U));
            REQUIRE(machine.predicates == saved);
        }
    }
}


TEST_CASE("Native unsigned half conversion truncates and saturates NTZ edge cases", "[native-shader]") {
    const std::array<Native, 2> input{{{{0x000e7305, 13, 0x0010f000, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (const auto [value, expected] : std::array<std::pair<u32, u32>, 8>{{
        {0x3e00, 1}, {0x4100, 2}, {0xbe00, 0}, {0x0001, 0},
        {0x7bff, 65504}, {0x7c00, 0xffffffffU}, {0xfc00, 0}, {0x7e00, 0},
    }}) {
        ScalarNativeMachine machine;
        machine.regs[13] = 0xdead0000 | value;
        machine.Execute(block);
        REQUIRE(machine.regs[14] == expected);
    }
}


TEST_CASE("Native predicate LUT OR combines three inputs while preserving other predicates", "[native-shader]") {
    const std::array<Native, 3> input{{{{0x0000781c, 0, 0x01f25f16, 0}}, {{0x0000781c, 0, 0x00703570, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u32 bits = 0; bits < 128; ++bits) {
        ScalarNativeMachine machine;
        for (u32 i = 0; i < 7; ++i) machine.predicates[i] = (bits >> i) & 1;
        auto expected = machine.predicates;
        expected[1] = expected[1] || expected[2] || expected[3];
        expected[0] = expected[0] || expected[1];
        machine.Execute(block);
        REQUIRE(machine.predicates == expected);
    }
}


TEST_CASE("Native half FMA absolute source clears each half sign before broadcast", "[native-shader]") {
    for (bool broadcast : {false, true}) {
        const std::array<Native, 2> input{{{{0x1a087231, 0x20000008,
            broadcast ? 0x00040a0dU : 0x0004020dU, 0}}, VertexTransform.back()}};
        Shader::ObjectPool<IR::Inst> pool;
        IR::Block block{pool};
        TranslateScalarNative(input, block);
        ScalarNativeMachine machine;
        machine.regs[26] = 0xc400c000;
        machine.regs[8] = 0x40003c00;
        machine.regs[13] = 0x44004200;
        machine.Execute(block);
        REQUIRE(machine.regs[8] == (broadcast ? 0x45004500U : 0x47004500U));
    }
}


TEST_CASE("Native constant interpolation retains attribute and flat input behavior", "[native-shader]") {
    const std::array<Native, 2> input{{{{0x00047326, 255, 0x000e403c, 0}}, VertexTransform.back()}};
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(lowered);
    REQUIRE(((lowered.instructions[0] >> 54) & 3) == 2);
    class FlatEnvironment : public EmptyEnvironment {
    public:
        FlatEnvironment() { sph.ps.imap_generic_vector[7].raw = 0x55; }
    } env;
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    Shader::Maxwell::TranslatorVisitor visitor{env, block};
    visitor.IPA(lowered.instructions[0]);
    u32 reads{};
    for (const auto& inst : block.Instructions()) {
        REQUIRE(inst.GetOpcode() != IR::Opcode::FPMul32);
        if (inst.GetOpcode() == IR::Opcode::GetAttribute) {
            REQUIRE(inst.Arg(0).Attribute() == IR::Attribute{60});
            ++reads;
        }
    }
    REQUIRE(reads == 1);
}


TEST_CASE("Native uniform predicate comparisons preserve all seven predicates and accumulation", "[native-shader]") {
    for (u32 output = 0; output < 7; ++output) {
        for (u32 combine = 0; combine < 3; ++combine) {
            for (bool invert : {false, true}) {
                std::array<Native, 2> input{{{{0x0700788c, 1, 0x0bfa5070, 0}}, VertexTransform.back()}};
                auto& w = input[0].words[2];
                w = (w & ~((7U << 17) | (15U << 23) | (3U << 10))) |
                    (output << 17) | (output << 23) | (u32{invert} << 26) | (combine << 10);
                Shader::ObjectPool<IR::Inst> pool;
                IR::Block block{pool};
                TranslateScalarNative(input, block);
                for (u32 bank : {0U, 0x2aU, 0x55U, 0x7fU}) {
                    for (u32 value : {0U, 1U, 2U, 0xffffffffU}) {
                        ScalarNativeMachine machine;
                        machine.regs[223] = value;
                        machine.regs[239] = bank;
                        for (u32 p = 0; p < 7; ++p) machine.predicates[p] = (p & 1) != 0;
                        const auto original = machine.predicates;
                        machine.Execute(block);
                        const bool a = value != 1;
                        const bool b = ((bank & (1U << (6 - output))) != 0) != invert;
                        const bool expected = combine == 0 ? a && b : combine == 1 ? a || b : a != b;
                        const u32 mask = 1U << (6 - output);
                        REQUIRE(machine.regs[239] == ((bank & ~mask) | (expected ? mask : 0U)));
                        REQUIRE(machine.predicates == original);
                        REQUIRE(machine.regs[223] == value);
                    }
                }
            }
        }
    }
}


TEST_CASE("Native uniform predicate LUT and ordinary copies keep separate banks", "[native-shader]") {
    for (u32 output = 0; output < 7; ++output) {
        for (u32 lut : {0x80U, 0x40U}) {
            std::array<Native, 2> input{{{{0x0000789c, 0, 0x00fce820, 0}}, VertexTransform.back()}};
            auto& w = input[0].words[2];
            w = (w & ~((7U << 17) | 7U | (31U << 8))) | (output << 17) | (lut & 7) | ((lut >> 3) << 8);
            Shader::ObjectPool<IR::Inst> pool;
            IR::Block block{pool};
            TranslateScalarNative(input, block);
            for (u32 bank = 0; bank < 128; ++bank) {
                ScalarNativeMachine machine;
                machine.regs[239] = bank;
                for (u32 p = 0; p < 7; ++p) machine.predicates[p] = (p & 1) != 0;
                const auto original = machine.predicates;
                machine.Execute(block);
                const bool expected = (bank & (1U << 5)) && (((bank & (1U << 4)) != 0) != (lut == 0x40));
                const u32 mask = 1U << (6 - output);
                REQUIRE(machine.regs[239] == ((bank & ~mask) | (expected ? mask : 0U)));
                REQUIRE(machine.predicates == original);
            }
        }
    }
    for (u32 source = 0; source < 7; ++source) {
        for (bool invert : {false, true}) {
            std::array<Native, 2> input{{{{0x0000781c, 0, 0x03f4f058, 0}}, VertexTransform.back()}};
            auto& w = input[0].words[2];
            const u32 lut = invert ? 0x40U : 0x80U;
            w = (w & ~((15U << 4) | 7U | (31U << 8))) | (source << 4) | (lut & 7) | ((lut >> 3) << 8);
            Shader::ObjectPool<IR::Inst> pool;
            IR::Block block{pool};
            TranslateScalarNative(input, block);
            for (u32 bank : {0U, 0x2aU, 0x55U, 0x7fU}) {
                ScalarNativeMachine machine;
                machine.regs[239] = bank;
                machine.predicates.fill(true);
                machine.Execute(block);
                REQUIRE(machine.predicates[2] == (((bank & (1U << (6 - source))) != 0) != invert));
                REQUIRE(machine.regs[239] == bank);
                for (u32 p = 0; p < 7; ++p) if (p != 2) REQUIRE(machine.predicates[p]);
            }
        }
    }
}


TEST_CASE("Native predicated uniform address arithmetic uses the selected uniform predicate", "[native-shader]") {
    const std::array<Native, 2> unsupported{{{{0x0405f891, 1, 0x0f8e103f, 0}}, VertexTransform.back()}};
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(unsupported));
    for (u32 condition = 0; condition < 7; ++condition) {
        for (bool invert : {false, true}) {
            std::array<Native, 2> input{{{{0x04057891, 1, 0x0f8e103f, 0}}, VertexTransform.back()}};
            input[0].words[0] = (input[0].words[0] & ~0xf000U) | ((condition | (invert ? 8U : 0U)) << 12);
            const auto lowered = Shader::Ampere::LowerToMaxwell(input);
            REQUIRE(lowered);
            // The scalar evaluator ignores execution predicates, so check the real emitted guards separately.
            REQUIRE(((lowered.instructions[4] >> 16) & 15) == (6U | (invert ? 8U : 0U)));
            Shader::ObjectPool<IR::Inst> pool;
            IR::Block block{pool};
            const std::span prefix{lowered.instructions.data(), size_t{4}};
            EmptyEnvironment env;
            Shader::Maxwell::TranslatorVisitor visitor{env, block};
            visitor.P2R_imm(prefix[0]);
            visitor.SHR_imm(prefix[1]);
            visitor.SHL_imm(prefix[2]);
            visitor.R2P_imm(prefix[3]);
            for (u32 bank : {0U, 0x2aU, 0x55U, 0x7fU}) {
                ScalarNativeMachine machine;
                machine.regs[239] = bank;
                machine.predicates.fill(true);
                machine.Execute(block);
                REQUIRE(machine.predicates[6] == ((bank & (1U << (6 - condition))) != 0));
                REQUIRE(machine.regs[239] == bank);
            }
        }
    }
}


TEST_CASE("Native unsigned clamp tracks the selected uniform predicate", "[native-shader]") {
    for (u32 condition = 0; condition < 7; ++condition) {
        std::array<Native, 3> input{{
            {{0x0400788c, 2, 0x0bf01070, 0}},
            {{0x04047287, 2, 0x08000000, 0}},
            VertexTransform.back(),
        }};
        input[1].words[0] = (input[1].words[0] & ~0xfffU) | 0x887;
        input[0].words[2] |= condition << 17;
        input[1].words[2] |= condition << 23;
        const auto lowered = Shader::Ampere::LowerToMaxwell(input);
        REQUIRE(lowered);
        REQUIRE(Shader::Maxwell::Decode(lowered.instructions[lowered.source_instruction_offsets[1]]) ==
                Shader::Maxwell::Opcode::IMNMX_imm);
        Shader::ObjectPool<IR::Inst> pool;
        IR::Block block{pool};
        TranslateScalarNative(input, block);
        for (u32 value : {0U, 1U, 2U, 3U, 0xffffffffU}) {
            ScalarNativeMachine machine;
            machine.regs[220] = value;
            machine.regs[239] = 0x55;
            machine.predicates.fill(true);
            machine.Execute(block);
            REQUIRE(machine.regs[220] == std::min(value, 2U));
            for (bool pred : machine.predicates) REQUIRE(pred);
        }
        input[1].words[2] ^= 1U << 23;
        if (condition != 6) {
            const auto mismatch = Shader::Ampere::LowerToMaxwell(input);
            REQUIRE(mismatch);
            REQUIRE(Shader::Maxwell::Decode(mismatch.instructions[mismatch.source_instruction_offsets[1]]) !=
                    Shader::Maxwell::Opcode::IMNMX_imm);
        }
    }
}


TEST_CASE("Native predicate LUT masks its inverted middle input", "[native-shader]") {
    const std::array<Native, 2> input{{{{0x0000781c, 0, 0x01fac470, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u32 bits = 0; bits < 128; ++bits) {
        ScalarNativeMachine machine;
        for (u32 p = 0; p < 7; ++p) machine.predicates[p] = ((bits >> p) & 1) != 0;
        auto expected = machine.predicates;
        expected[5] = expected[3] && !expected[6];
        machine.Execute(block);
        REQUIRE(machine.predicates == expected);
    }
}


TEST_CASE("Native cancellation retains its enclosing scopes at a parent join", "[native-shader]") {
    std::array<Native, 14> input{{
        {{0x27945, 192, 0x03800000, 0}},
        {{0x17945, 160, 0x03800000, 0}},
        {{0x7945, 128, 0x03800000, 0}},
        {{0x37945, 64, 0x03800000, 0}},
        {{0x38942, 0, 0x03800000, 0}},
        {{0x8947, 64, 0x03800000, 0}},
        {{0x27802, 1, 0xf00, 0}},
        {{0x37941, 0, 0x03800000, 0}},
        {{0x27802, 3, 0xf00, 0}},
        {{0x7918, 0, 0, 0}},
        {{0x7941, 0, 0x03800000, 0}},
        {{0x17941, 0, 0x03800000, 0}},
        {{0x27941, 0, 0x03800000, 0}},
        VertexTransform.back(),
    }};
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(lowered);
    auto broken = input;
    broken[5].words[1] = 48;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(broken));
    broken = input;
    broken[4].words[0] = 0x18942;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(broken));
    std::vector<u64> code(0x50 / 8);
    for (const auto word : lowered.instructions) {
        if ((code.size() * 8) % 32 == 0) code.push_back(0);
        code.push_back(word);
    }
    class BarrierEnvironment final : public EmptyEnvironment {
    public:
        explicit BarrierEnvironment(const std::vector<u64>& code_) : code{code_} { stage = Shader::Stage::VertexB; }
        u64 ReadInstruction(u32 address) override { return code.at(address / 8); }
        const std::vector<u64>& code;
    } env{code};
    Shader::ObjectPool<Shader::Maxwell::Flow::Block> flow_pool;
    Shader::Maxwell::Flow::CFG cfg{env, flow_pool, 0x50};
    INFO(cfg.Dot());
    const u32 offset = lowered.source_instruction_offsets[8];
    const Shader::Maxwell::Location continuation{0x50 + offset * 8 + ((offset + 1) / 3) * 8};
    for (bool predicate : {false, true}) {
        const auto* block = &*cfg.Functions().front().blocks.begin();
        bool visited_continuation = false, exited = false;
        std::string trace;
        for (u32 step = 0; step < 64 && block; ++step) {
            trace += std::to_string(block->begin.Offset()) + ":" + std::to_string(block->end.Offset()) + " ";
            visited_continuation |= !block->begin.IsVirtual() && block->Contains(continuation);
            if (block->end_class == Shader::Maxwell::Flow::EndClass::Exit) { exited = true; break; }
            const auto [pred, inverted] = block->cond.GetPred();
            REQUIRE(block->cond.GetFlowTest() == IR::FlowTest::T);
            REQUIRE((pred == IR::Pred::P0 || pred == IR::Pred::PT));
            const bool taken = (pred == IR::Pred::PT || predicate) != inverted;
            block = taken ? block->branch_true : block->branch_false;
        }
        REQUIRE(exited);
        INFO("path=" << trace << " marker=" << continuation.Offset());
        REQUIRE(visited_continuation == predicate);
    }
}


TEST_CASE("Native cube array shadow sample preserves layer XYZ reference and destination", "[native-shader]") {
    std::array<Native, 2> input{{{{0x20217b60, 0xe0801a44, 0x001e41ff, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    std::vector<u32> reads;
    u32 samples{};
    for (const auto& inst : block.Instructions()) {
        if (inst.GetOpcode() == IR::Opcode::GetRegister) reads.push_back(static_cast<u32>(inst.Arg(0).Reg()));
        if (inst.GetOpcode() == IR::Opcode::BoundImageSampleDrefImplicitLod) {
            REQUIRE(inst.Arg(0).U32() == 104);
            REQUIRE(inst.Flags<IR::TextureInstInfo>().type.Value() == Shader::TextureType::ColorArrayCube);
            REQUIRE(inst.Flags<IR::TextureInstInfo>().is_depth.Value() == 1);
            REQUIRE(inst.Arg(2).Inst()->Arg(0).Inst()->Arg(0).Reg() == IR::Reg{68});
            ++samples;
        }
    }
    REQUIRE(samples == 1);
    std::sort(reads.begin(), reads.end());
    REQUIRE(reads == std::vector<u32>{32, 33, 34, 35, 68});
    REQUIRE(block.Instructions().back().Arg(0).Reg() == IR::Reg{33});
    input[0].words[1] |= 1U << 28;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(input));
}


TEST_CASE("Native computed entry cannot bypass the sync before a warp memory barrier", "[native-shader]") {
    std::vector<u64> code(64);
    const std::array<u32, 4> offsets{0, 2, 4, 8};
    code[0x50 / 8] = 0xf0f800000007000fULL;
    code[0x68 / 8] = 0xef98000000070100ULL;
    Shader::Ampere::AppendNativeAddressMap(code, offsets);
    REQUIRE(Shader::Ampere::NativeProgramAddress(code, 0) == 0x50U);
    REQUIRE_FALSE(Shader::Ampere::NativeProgramAddress(code, 16));
    REQUIRE(Shader::Ampere::NativeProgramAddress(code, 32) == 0x78U);
    const auto restored = code;
    REQUIRE_FALSE(Shader::Ampere::NativeProgramAddress(restored, 16));
}


TEST_CASE("Native float predicate comparisons retain uniform values and source modifiers", "[native-shader]") {
    const auto ur = [](u32 index) { return 216U - (index / 16) * 16 + index % 16; };
    const bool absolute = GENERATE(false, true);
    const bool negate = GENERATE(false, true);
    for (u32 source : {0U, 5U, 15U, 16U, 17U, 18U, 30U, 32U, 46U, 48U, 62U, 63U}) {
        std::array<Native, 2> input{{{{0xff007c0b, source, 0x0bf53000, 0}}, VertexTransform.back()}};
        input[0].words[1] |= (u32{absolute} << 30) | (u32{negate} << 31);
        Shader::ObjectPool<IR::Inst> pool;
        IR::Block block{pool};
        TranslateScalarNative(input, block);
        for (float value : {-2.0f, -0.0f, 0.0f, 3.0f, std::numeric_limits<float>::infinity(),
                           -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
            ScalarNativeMachine machine;
            if (source != 63) machine.regs[ur(source)] = std::bit_cast<u32>(value);
            for (u32 p = 0; p < 7; ++p) machine.predicates[p] = (p & 1) != 0;
            auto expected = machine.predicates;
            float operand = source == 63 ? 0.0f : value;
            if (absolute) operand = std::abs(operand);
            if (negate) operand = -operand;
            expected[2] = 0.0f <= operand;
            machine.Execute(block);
            REQUIRE(machine.predicates == expected);
            if (source != 63) REQUIRE(machine.regs[ur(source)] == std::bit_cast<u32>(value));
        }
        input[0].words[1] = 64;
        REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(input));
    }
}


TEST_CASE("Native fused multiply add retains its uniform multiplier and aliased accumulator", "[native-shader]") {
    const bool saturate = GENERATE(false, true);
    std::array<Native, 2> input{{{{0x05127c23, 8, 0x08012012, 0}}, VertexTransform.back()}};
    if (!saturate) input[0].words[2] &= ~(1U << 13);
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (const auto& values : std::array<std::array<float, 3>, 5>{{
        {{-1, 3, 2}}, {{3, 2, 0}}, {{0.25f, 2, 0.25f}}, {{-2, -3, -5}},
        {{1.0f + 0x1p-23f, 1.0f - 0x1p-23f, -1.0f}},
    }}) {
        ScalarNativeMachine machine;
        machine.regs[5] = std::bit_cast<u32>(values[0]);
        machine.regs[224] = std::bit_cast<u32>(values[1]);
        machine.regs[18] = std::bit_cast<u32>(values[2]);
        machine.predicates.fill(true);
        float expected = std::fma(values[0], values[1], values[2]);
        if (saturate) expected = std::clamp(expected, 0.0f, 1.0f);
        machine.Execute(block);
        REQUIRE(machine.regs[18] == std::bit_cast<u32>(expected));
        REQUIRE(machine.regs[5] == std::bit_cast<u32>(values[0]));
        REQUIRE(machine.regs[224] == std::bit_cast<u32>(values[1]));
        for (bool p : machine.predicates) REQUIRE(p);
    }
}


TEST_CASE("Native high LEA retains its constant base and high scaled word", "[native-shader]") {
    const u32 shift = GENERATE(0U, 1U, 16U, 30U, 31U);
    for (u32 destination : {5U, 7U}) {
        std::array<Native, 2> input{{{{0x05077a11, 0x00c00000, 0x078ff0ff, 0}}, VertexTransform.back()}};
        input[0].words[0] = (input[0].words[0] & ~0xff0000U) | (destination << 16);
        input[0].words[2] = (input[0].words[2] & ~(31U << 11)) | (shift << 11);
        Shader::ObjectPool<IR::Inst> pool;
        IR::Block block{pool};
        TranslateScalarNative(input, block);
        for (u32 value : {0U, 1U, 0x80000000U, 0xffffffffU}) {
            for (u32 base : {0U, 1U, 0xffffffffU}) {
                ScalarNativeMachine machine;
                machine.regs[5] = value;
                machine.constants[u64{3} << 32] = base;
                machine.predicates.fill(true);
                machine.Execute(block);
                REQUIRE(machine.regs[destination] == static_cast<u32>((u64{value} << shift) >> 32) + base);
                if (destination != 5) REQUIRE(machine.regs[5] == value);
                for (bool pred : machine.predicates) REQUIRE(pred);
            }
        }
        input[0].words[2] &= ~(7U << 17);
        REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(input));
    }
}


TEST_CASE("Native capture treats predicated NOINC forward exits as branches", "[native-shader]") {
    using Capture = Shader::Ampere::ProgramCaptureState;
    Capture loop;
    REQUIRE(loop.Observe(Native{{0x3944, 16, 0x03c00000, 0}}, 0) == Capture::Result::Continue);
    REQUIRE(loop.Observe(Native{{0x7947, 0xffffffe0, 0x0383ffff, 0}}, 1) == Capture::Result::Continue);
    REQUIRE(loop.Observe(VertexTransform.back(), 2) == Capture::Result::Complete);
    Capture forward;
    REQUIRE(forward.Observe(Native{{0x3944, 48, 0x03c00000, 0}}, 0) == Capture::Result::Continue);
    REQUIRE(forward.Observe(VertexTransform.back(), 1) == Capture::Result::Continue);
    REQUIRE(forward.Observe(VertexTransform.back(), 4) == Capture::Result::Complete);
}


TEST_CASE("Native uniform LUT preserves its bank and aliases its source", "[native-shader]") {
    const std::array<Native, 2> input{{{{0x04047892, 0x3fffffff, 0x0f8ec03f, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u32 value : {0U, 1U, 0x3fffffffU, 0x80000000U, 0xffffffffU}) {
        ScalarNativeMachine machine;
        machine.regs[220] = value;
        machine.regs[239] = 0x55;
        machine.predicates.fill(true);
        machine.Execute(block);
        REQUIRE(machine.regs[220] == (value & 0x3fffffffU));
        REQUIRE(machine.regs[239] == 0x55);
        for (bool p : machine.predicates) REQUIRE(p);
    }
}


TEST_CASE("Native unsigned conversion toward zero is exact at float precision boundaries", "[native-shader]") {
    const std::array<Native, 2> input{{{{0x00077245, 2, 0x0020d000, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    const std::array<std::pair<u32, u32>, 10> cases{{
        {0, 0}, {1, 0x3f800000}, {0x00ffffff, 0x4b7fffff}, {0x01000000, 0x4b800000},
        {0x01000001, 0x4b800000}, {0x01000003, 0x4b800001}, {0x7fffffff, 0x4effffff},
        {0x80000000, 0x4f000000}, {0x800000ff, 0x4f000000}, {0xffffffff, 0x4f7fffff},
    }};
    for (const auto& [source, expected] : cases) {
        ScalarNativeMachine machine;
        machine.regs[2] = source;
        machine.predicates.fill(true);
        machine.Execute(block);
        REQUIRE(machine.regs[7] == expected);
        REQUIRE(machine.regs[2] == source);
        for (bool p : machine.predicates) REQUIRE(p);
    }
}


TEST_CASE("Native scalar uniform add preserves wraparound and its source", "[native-shader]") {
    const std::array<Native, 2> input{{{{0x00147c10, 4, 0x0fffe0ff, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u32 a : {0U, 1U, 0xffffffffU}) for (u32 b : {0U, 1U, 0x80000000U, 0xffffffffU}) {
        ScalarNativeMachine machine;
        machine.regs[0] = a;
        machine.regs[220] = b;
        machine.Execute(block);
        REQUIRE(machine.regs[20] == a + b);
        REQUIRE(machine.regs[0] == a);
        REQUIRE(machine.regs[220] == b);
    }
}


TEST_CASE("Native scalar strong global loads retain device memory ordering", "[native-shader]") {
    const bool strong = GENERATE(false, true);
    const std::array<Native, 2> input{{{{0x14067381, 0, strong ? 0x001ea900U : 0x001e8900U, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    ScalarNativeMachine machine;
    machine.regs[20] = 0x1000;
    machine.regs[21] = 0x22;
    machine.global[0x2200001000ULL] = 0xdeadbeef;
    machine.Execute(block);
    REQUIRE(machine.regs[6] == 0xdeadbeef);
    REQUIRE(machine.device_barriers == (strong ? 2U : 0U));
}


TEST_CASE("Native three source addition keeps negative immediates and negated third inputs", "[native-shader]") {
    const bool negate_c = GENERATE(false, true);
    for (u32 immediate : {0U, 33U, 0xffffff82U}) {
        std::array<Native, 2> input{{{{0x232b7810, immediate, 0x07ffe024, 0}}, VertexTransform.back()}};
        input[0].words[2] |= u32{negate_c} << 11;
        Shader::ObjectPool<IR::Inst> pool;
        IR::Block block{pool};
        TranslateScalarNative(input, block);
        for (u32 a : {0U, 1U, 0x80000000U, 0xffffffffU}) for (u32 c : {0U, 1U, 0x80000000U, 0xffffffffU}) {
            ScalarNativeMachine machine;
            machine.regs[35] = a;
            machine.regs[36] = c;
            machine.Execute(block);
            REQUIRE(machine.regs[43] == a + immediate + (negate_c ? 0U - c : c));
            REQUIRE(machine.regs[35] == a);
            REQUIRE(machine.regs[36] == c);
        }
    }
}


TEST_CASE("Native permutation sign extends the low half without changing its source", "[native-shader]") {
    const std::array<Native, 2> input{{{{0x3a377816, 0x9910, 0xff, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u32 value : {0U, 1U, 0x7fffU, 0x8000U, 0xffffU, 0xdead8000U, 0xabcd7fffU}) {
        ScalarNativeMachine machine;
        machine.regs[58] = value;
        machine.Execute(block);
        REQUIRE(machine.regs[55] == static_cast<u32>(static_cast<s32>(static_cast<s16>(value))));
        REQUIRE(machine.regs[58] == value);
    }
}


TEST_CASE("Native byte float conversions select all four bytes", "[native-shader]") {
    for (u32 byte = 0; byte < 4; ++byte) {
        const std::array<Native, 2> input{{{{0x003b7306, 33U | (byte << 28), 0x1000, 0}}, VertexTransform.back()}};
        Shader::ObjectPool<IR::Inst> pool;
        IR::Block block{pool};
        TranslateScalarNative(input, block);
        ScalarNativeMachine machine;
        machine.regs[33] = 0xff807f00;
        machine.Execute(block);
        REQUIRE(machine.regs[59] == std::bit_cast<u32>(static_cast<float>((0xff807f00U >> (byte * 8)) & 255U)));
        REQUIRE(machine.regs[33] == 0xff807f00);
    }
}


TEST_CASE("Native predicate LUT negates both first inputs", "[native-shader]") {
    const std::array<Native, 2> input{{{{0x0000781c, 0, 0x027c0072, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u32 bits = 0; bits < 128; ++bits) {
        ScalarNativeMachine machine;
        for (u32 p = 0; p < 7; ++p) machine.predicates[p] = ((bits >> p) & 1) != 0;
        auto expected = machine.predicates;
        expected[6] = !expected[4] && !expected[0];
        machine.Execute(block);
        REQUIRE(machine.predicates == expected);
    }
}


TEST_CASE("Native uniform moves read the zero register without aliasing the bank", "[native-shader]") {
    const auto ur = [](u32 index) { return 216U - (index / 16) * 16 + index % 16; };
    for (u32 source : {0U, 5U, 15U, 16U, 17U, 18U, 30U, 32U, 46U, 48U, 62U, 63U}) {
        const std::array<Native, 2> input{{{{0x00047c82, source, 0x08000000, 0}}, VertexTransform.back()}};
        const auto lowered = Shader::Ampere::LowerToMaxwell(input);
        REQUIRE(lowered);
        Shader::ObjectPool<IR::Inst> pool;
        IR::Block block{pool};
        EmptyEnvironment env;
        Shader::Maxwell::TranslatorVisitor visitor{env, block};
        visitor.MOV_reg(lowered.instructions[0]);
        ScalarNativeMachine machine;
        machine.regs.fill(0xa5a5a5a5);
        if (source != 63) machine.regs[ur(source)] = 0x12340000U | source;
        machine.Execute(block);
        REQUIRE(machine.regs[220] == (source == 63 ? 0U : 0x12340000U | source));
        REQUIRE(machine.regs[239] == 0xa5a5a5a5U);
    }
    for (u32 source : {64U, 255U}) {
        const std::array<Native, 2> input{{{{0x00047c82, source, 0x08000000, 0}}, VertexTransform.back()}};
        REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(input));
    }
}


TEST_CASE("Native LUT30 clears selected bits and preserves aliased sources", "[native-shader]") {
    for (u32 destination : {0U, 1U, 3U}) {
        const std::array<Native, 2> input{{{{0x00007212U | (destination << 16), 1, 0x078e3002, 0}}, VertexTransform.back()}};
        Shader::ObjectPool<IR::Inst> pool;
        IR::Block block{pool};
        TranslateScalarNative(input, block);
        for (u32 a : {0U, 1U, 0xaaaaaaaaU, 0xffffffffU})
            for (u32 b : {0U, 1U, 0x55555555U, 0xffffffffU}) {
                ScalarNativeMachine machine;
                machine.regs[0] = a; machine.regs[1] = b; machine.regs[2] = 0xffffffff;
                machine.predicates.fill(true);
                machine.Execute(block);
                REQUIRE(machine.regs[destination] == (a & ~b));
                for (bool predicate : machine.predicates) REQUIRE(predicate);
            }
    }
}


TEST_CASE("Native uniform logical right shift retains unsigned high bits", "[native-shader]") {
    for (u32 shift : {0U, 1U, 2U, 16U, 31U, 32U}) {
        const std::array<Native, 2> input{{{{0x3f057899, shift, 0x08011604, 0}}, VertexTransform.back()}};
        Shader::ObjectPool<IR::Inst> pool;
        IR::Block block{pool};
        TranslateScalarNative(input, block);
        for (u32 value : {0U, 1U, 0x7fffffffU, 0x80000000U, 0xffffffffU}) {
            ScalarNativeMachine machine;
            machine.regs[220] = value;
            machine.Execute(block);
            REQUIRE(machine.regs[221] == (shift < 32 ? value >> shift : 0U));
            REQUIRE(machine.regs[220] == value);
        }
    }
}


TEST_CASE("Native uniform permutation sign extends its low half and supports aliasing", "[native-shader]") {
    for (u32 destination : {6U, 8U}) {
        const std::array<Native, 2> input{{{{0x08007896U | (destination << 16), 0x9910, 0x0800003f, 0}}, VertexTransform.back()}};
        Shader::ObjectPool<IR::Inst> pool;
        IR::Block block{pool};
        TranslateScalarNative(input, block);
        for (u32 value : {0U, 1U, 0x7fffU, 0x8000U, 0xffffU, 0xdead8000U, 0xabcd7fffU}) {
            ScalarNativeMachine machine;
            machine.regs[224] = value;
            machine.Execute(block);
            REQUIRE(machine.regs[destination + 216] == static_cast<u32>(static_cast<s32>(static_cast<s16>(value))));
            if (destination != 8) REQUIRE(machine.regs[224] == value);
        }
    }
}


TEST_CASE("Native uniform bit tests update one uniform predicate and preserve scalar predicates", "[native-shader]") {
    const u32 bit_mask = GENERATE(0U, 1U, 0x20000U, 0x40000U, 0xffffffffU);
    for (u32 output = 0; output < 7; ++output) {
        const std::array<Native, 2> input{{{{0x0a3f7892, bit_mask, 0x0f80c03fU | (output << 17), 0}}, VertexTransform.back()}};
        Shader::ObjectPool<IR::Inst> pool;
        IR::Block block{pool};
        TranslateScalarNative(input, block);
        for (u32 value : {0U, 1U, 2U, 0x20000U, 0x40000U, 0x80000000U, 0xffffffffU})
            for (u32 bank : {0U, 0x55U, 0x7fU}) {
                ScalarNativeMachine machine;
                machine.regs[226] = value; machine.regs[239] = bank;
                machine.predicates = {true, false, true, false, true, false, true};
                const auto original = machine.predicates;
                machine.Execute(block);
                const u32 mask = 1U << (6 - output);
                REQUIRE(machine.regs[239] == ((bank & ~mask) | ((value & bit_mask) ? mask : 0U)));
                REQUIRE(machine.predicates == original);
                REQUIRE(machine.regs[226] == value);
            }
    }
}


TEST_CASE("Native uniform register effective addresses preserve scaled addition and aliasing", "[native-shader]") {
    for (u32 shift : {0U, 1U, 2U, 16U, 31U}) {
        const std::array<Native, 2> input{{{{0x05047291, 8, 0x0f8e003fU | (shift << 11), 0}}, VertexTransform.back()}};
        Shader::ObjectPool<IR::Inst> pool;
        IR::Block block{pool};
        TranslateScalarNative(input, block);
        for (u32 value : {0U, 1U, 0x80000000U, 0xffffffffU}) {
            ScalarNativeMachine machine;
            machine.regs[221] = value; machine.regs[224] = 0xfffffff0;
            machine.Execute(block);
            REQUIRE(machine.regs[220] == ((value << shift) + 0xfffffff0U));
            REQUIRE(machine.regs[221] == value);
            REQUIRE(machine.regs[224] == 0xfffffff0U);
        }
    }
}


TEST_CASE("Native uniform global reduction preserves its full pointer and wrapping addend", "[native-shader]") {
    const std::array<Native, 2> input{{{{0xff00798e, 0, 0x0c10e104, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u32 low : {0U, 0xfffffff0U, 0xfffffffcU}) {
        ScalarNativeMachine machine;
        machine.regs[220] = low; machine.regs[221] = 0x21; machine.regs[0] = 17;
        const u64 address = 0x2100000000ULL | low;
        machine.global[address] = 0xfffffff8;
        machine.Execute(block);
        REQUIRE(machine.global == std::unordered_map<u64, u32>{{address, 9}});
        REQUIRE(machine.regs[220] == low);
        REQUIRE(machine.regs[221] == 0x21);
        REQUIRE(machine.regs[0] == 17);
    }
}


TEST_CASE("Native XOR and inverted outer-input LUTs retain data and predicate truth tables", "[native-shader]") {
    for (u32 lut : {4U, 0x0cU, 0x3cU}) {
        for (u32 destination : {0U, 1U, 2U, 3U}) {
            const std::array<Native, 2> input{{{{0x00007212U | (destination << 16), 1, 0x07820002U | (lut << 8), 0}}, VertexTransform.back()}};
            Shader::ObjectPool<IR::Inst> pool;
            IR::Block block{pool};
            TranslateScalarNative(input, block);
            for (u32 a : {0U, 1U, 0xaaaaaaaaU, 0xffffffffU})
                for (u32 b : {0U, 1U, 0x55555555U, 0xffffffffU})
                    for (u32 c : {0U, 1U, 0xccccccccU, 0xffffffffU}) {
                        ScalarNativeMachine machine;
                        machine.regs[0] = a; machine.regs[1] = b; machine.regs[2] = c;
                        machine.predicates.fill(true);
                        machine.Execute(block);
                        const u32 expected = lut == 4 ? (~a & b & ~c) : lut == 0x0c ? (~a & b) : (a ^ b);
                        REQUIRE(machine.regs[destination] == expected);
                        REQUIRE(machine.predicates[1] == (expected != 0));
                        for (u32 p = 0; p < 7; ++p) if (p != 1) REQUIRE(machine.predicates[p]);
                    }
        }
    }
}


TEST_CASE("Native uniform carry chains preserve 64-bit overflow and both predicate banks", "[native-shader]") {
    for (u32 output = 0; output < 7; ++output) {
        const std::array<Native, 3> input{{
            {{0x04047290, 8, 0x0ff1e03fU | (output << 17), 0}},
            {{0x3f057290, 9, 0x087fe43fU | (output << 23), 0}}, VertexTransform.back()}};
        Shader::ObjectPool<IR::Inst> pool;
        IR::Block block{pool};
        TranslateScalarNative(input, block);
        for (u32 a : {0U, 1U, 0xfffffff0U, 0xffffffffU})
            for (u32 b : {0U, 1U, 32U, 0xffffffffU})
                for (u32 high : {0U, 0xffffffffU}) {
                    ScalarNativeMachine machine;
                    machine.regs[220] = a; machine.regs[224] = b; machine.regs[225] = high;
                    machine.regs[239] = 0x55;
                    machine.predicates = {true, false, true, false, true, false, true};
                    const auto predicates = machine.predicates;
                    machine.Execute(block);
                    const u64 sum = u64{a} + b;
                    const u32 mask = 1U << (6 - output);
                    REQUIRE(machine.regs[220] == static_cast<u32>(sum));
                    REQUIRE(machine.regs[221] == high + static_cast<u32>(sum >> 32));
                    REQUIRE(machine.regs[239] == ((0x55U & ~mask) | ((sum >> 32) ? mask : 0U)));
                    REQUIRE(machine.predicates == predicates);
                    REQUIRE(machine.regs[224] == b);
                }
    }
}


TEST_CASE("Native upper uniform registers remain separate from predicate state", "[native-shader]") {
    const std::array<Native, 4> input{{
        {{0x00107882, 0xffffffff, 0, 0}},
        {{0x00117882, 0x12345678, 0, 0}},
        {{0x10107890, 2, 0x0fffe03f, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    ScalarNativeMachine machine;
    machine.regs[239] = 0x55;
    machine.regs[232] = 0xabcdef12;
    machine.regs[233] = 0x76543210;
    machine.Execute(block);
    REQUIRE(machine.regs[200] == 1);
    REQUIRE(machine.regs[201] == 0x12345678);
    REQUIRE(machine.regs[239] == 0x55);
    REQUIRE(machine.regs[232] == 0xabcdef12);
    REQUIRE(machine.regs[233] == 0x76543210);
}


TEST_CASE("Native uniform predicate extraction selects UP0 without changing either bank", "[native-shader]") {
    const std::array<Native, 2> input{{{{0x3f067883, 1, 0x08000000, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u32 bank = 0; bank < 128; ++bank) {
        ScalarNativeMachine machine;
        machine.regs[239] = bank;
        machine.predicates.fill(true);
        machine.Execute(block);
        REQUIRE(machine.regs[222] == ((bank >> 6) & 1));
        REQUIRE(machine.regs[239] == bank);
        for (bool predicate : machine.predicates) REQUIRE(predicate);
    }
}


TEST_CASE("Native multi-scoreboard dependency waits retain surrounding dataflow", "[native-shader]") {
    const std::array<Native, 3> input{{{{0x00007802, 17, 0x00000f00, 0}},
        {{0x0000791a, 0x800e, 0, 0}}, VertexTransform.back()}};
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(lowered);
    REQUIRE(lowered.instructions.size() == 2);
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    ScalarNativeMachine machine;
    machine.Execute(block);
    REQUIRE(machine.regs[0] == 17);
}


TEST_CASE("Native scalar effective addresses add the selected uniform base", "[native-shader]") {
    const std::array<Native, 2> input{{{{0x351c7c11, 5, 0x0f8e10ff, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u32 value : {0U, 1U, 0x80000000U, 0xffffffffU}) {
        ScalarNativeMachine machine;
        machine.regs[53] = value; machine.regs[221] = 0xfffffff0;
        machine.Execute(block);
        REQUIRE(machine.regs[28] == ((value << 2) + 0xfffffff0U));
        REQUIRE(machine.regs[53] == value);
        REQUIRE(machine.regs[221] == 0xfffffff0U);
    }
}


TEST_CASE("Native mixed predicate AND retains its scalar input and uniform bank", "[native-shader]") {
    const std::array<Native, 2> input{{{{0x0000781c, 0, 0x00f4e848, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u32 bank = 0; bank < 128; ++bank)
        for (bool scalar : {false, true}) {
            ScalarNativeMachine machine;
            machine.regs[239] = bank;
            machine.predicates.fill(true); machine.predicates[1] = scalar;
            auto expected = machine.predicates;
            expected[2] = scalar && !(bank & (1U << 2));
            machine.Execute(block);
            REQUIRE(machine.predicates == expected);
            REQUIRE(machine.regs[239] == bank);
        }
}


TEST_CASE("Native uniform shifts honor their uniform execution predicate", "[native-shader]") {
    for (u32 condition = 0; condition < 7; ++condition)
        for (bool invert : {false, true}) {
            const std::array<Native, 2> input{{{{0x3f070899U | ((condition | (invert ? 8U : 0U)) << 12), 2, 0x08011606, 0}}, VertexTransform.back()}};
            const auto lowered = Shader::Ampere::LowerToMaxwell(input);
            REQUIRE(lowered);
            REQUIRE(((lowered.instructions[4] >> 16) & 15) == (6U | (invert ? 8U : 0U)));
            REQUIRE(Shader::Maxwell::Decode(lowered.instructions[4]) == Shader::Maxwell::Opcode::SHR_imm);
            REQUIRE(Shader::Maxwell::Decode(lowered.instructions[5]) == Shader::Maxwell::Opcode::R2P_imm);
        }
}


TEST_CASE("Native shared loads retain uniform offsets and their byte displacement", "[native-shader]") {
    const std::array<Native, 2> input{{{{0xff1c7984, 0x13009, 0x08000800, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u32 address : {0U, 4U, 128U}) {
        ScalarNativeMachine machine;
        machine.regs[225] = address;
        machine.shared[address + 0x130] = 0x12345678;
        machine.Execute(block);
        REQUIRE(machine.regs[28] == 0x12345678);
        REQUIRE(machine.regs[225] == address);
    }
}


TEST_CASE("Native uniform shared stores and loads retain their data and displacement", "[native-shader]") {
    const std::array<Native, 3> input{{{{0xff007988, 0x13020, 0x08000809, 0}},
        {{0xff1c7984, 0x13009, 0x08000800, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    ScalarNativeMachine machine;
    machine.regs[225] = 64; machine.regs[32] = 0x12345678;
    machine.Execute(block);
    REQUIRE(machine.shared == std::unordered_map<u64, u32>{{64 + 0x130, 0x12345678}});
    REQUIRE(machine.regs[28] == 0x12345678);
    REQUIRE(machine.regs[225] == 64);
}


TEST_CASE("Native constant signed minmax retains negative bounds and execution predicates", "[native-shader]") {
    const std::array<Native, 2> input{{{{0xff017a17, 0xa00, 0x07800200, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (s32 value : {-10, -1, 0, 1, 100}) {
        ScalarNativeMachine machine;
        machine.constants[0x28] = static_cast<u32>(value);
        machine.Execute(block);
        REQUIRE(machine.regs[1] == static_cast<u32>(std::max(0, value)));
    }
    auto predicated = input;
    predicated[0].words[0] = 0xff01aa17;
    const auto lowered = Shader::Ampere::LowerToMaxwell(predicated);
    REQUIRE(lowered);
    for (u64 instruction : std::span{lowered.instructions}.first(lowered.instructions.size() - 1))
        REQUIRE(((instruction >> 16) & 15) == 10);
}


TEST_CASE("Native wide global stores preserve all words across a low-address carry", "[native-shader]") {
    for (u32 width : {4U, 5U, 6U}) {
        const std::array<Native, 2> input{{{{0x00007386, 12, 0x0010a100U | (width << 9), 0}}, VertexTransform.back()}};
        Shader::ObjectPool<IR::Inst> pool;
        IR::Block block{pool};
        TranslateScalarNative(input, block);
        ScalarNativeMachine machine;
        machine.regs[0] = 0xfffffffc; machine.regs[1] = 0x21;
        const u32 words = 1U << (width - 4);
        for (u32 i = 0; i < words; ++i) machine.regs[12 + i] = 0x12345678 + i;
        machine.Execute(block);
        REQUIRE(machine.global.size() == words);
        for (u32 i = 0; i < words; ++i)
            REQUIRE(machine.global.at(0x21fffffffcULL + i * 4) == 0x12345678 + i);
    }
}


TEST_CASE("Native split cancellation visits only its selected cleanup path", "[native-shader]") {
    std::array<Native, 14> input{{
        {{0x7945, 192, 0x03800000, 0}},
        {{0x17945, 80, 0x03800000, 0}},
        {{0x10942, 0, 0x03800000, 0}},
        {{0x8947, 32, 0x03800000, 0}},
        {{0x7947, 64, 0x03800000, 0}},
        {{0x27802, 99, 0xf00, 0}},
        {{0x17941, 0, 0x03800000, 0}},
        {{0x27802, 2, 0xf00, 0}},
        {{0x7947, 48, 0x03800000, 0}},
        {{0x27802, 3, 0xf00, 0}},
        {{0x7947, 16, 0x03800000, 0}},
        {{0x27802, 99, 0xf00, 0}},
        {{0x7941, 0, 0x03800000, 0}},
        VertexTransform.back(),
    }};
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(lowered);
    auto broken = input;
    broken[3].words[0] ^= 0x8000;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(broken));
    broken = input;
    broken[10].words[1] = static_cast<u32>(-32);
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(broken));
    std::vector<u64> code(0x50 / 8);
    for (const auto word : lowered.instructions) {
        if ((code.size() * 8) % 32 == 0) code.push_back(0);
        code.push_back(word);
    }
    class BarrierEnvironment final : public EmptyEnvironment {
    public:
        explicit BarrierEnvironment(const std::vector<u64>& code_) : code{code_} { stage = Shader::Stage::VertexB; }
        u64 ReadInstruction(u32 address) override { return code.at(address / 8); }
        const std::vector<u64>& code;
    } env{code};
    Shader::ObjectPool<Shader::Maxwell::Flow::Block> flow_pool;
    Shader::Maxwell::Flow::CFG cfg{env, flow_pool, 0x50};
    INFO(cfg.Dot());
    const u32 offset = lowered.source_instruction_offsets[9];
    const Shader::Maxwell::Location continuation{0x50 + offset * 8 + ((offset + 1) / 3) * 8};
    for (bool predicate : {false, true}) {
        const auto* block = &*cfg.Functions().front().blocks.begin();
        bool visited_continuation = false, exited = false;
        std::string trace;
        for (u32 step = 0; step < 64 && block; ++step) {
            trace += std::to_string(block->begin.Offset()) + ":" + std::to_string(block->end.Offset()) + " ";
            visited_continuation |= !block->begin.IsVirtual() && block->Contains(continuation);
            if (block->end_class == Shader::Maxwell::Flow::EndClass::Exit) { exited = true; break; }
            const auto [pred, inverted] = block->cond.GetPred();
            REQUIRE(block->cond.GetFlowTest() == IR::FlowTest::T);
            REQUIRE((pred == IR::Pred::P0 || pred == IR::Pred::PT));
            const bool taken = (pred == IR::Pred::PT || predicate) != inverted;
            block = taken ? block->branch_true : block->branch_false;
        }
        REQUIRE(exited);
        INFO("path=" << trace << " marker=" << continuation.Offset());
        REQUIRE(visited_continuation == predicate);
    }
}


TEST_CASE("Native split cancellation proves a forward cleanup after intervening data", "[native-shader]") {
    std::array<Native, 14> input{{
        {{0x7945, 192, 0x03800000, 0}},
        {{0x17945, 80, 0x03800000, 0}},
        {{0x10942, 0, 0x03800000, 0}},
        {{0x8947, 32, 0x03800000, 0}},
        {{0x27802, 1, 0xf00, 0}},
        {{0x7947, 48, 0x03800000, 0}},
        {{0x17941, 0, 0x03800000, 0}},
        {{0x27802, 2, 0xf00, 0}},
        {{0x7947, 48, 0x03800000, 0}},
        {{0x27802, 3, 0xf00, 0}},
        {{0x7947, 16, 0x03800000, 0}},
        {{0x27802, 99, 0xf00, 0}},
        {{0x7941, 0, 0x03800000, 0}},
        VertexTransform.back(),
    }};
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(lowered);
    auto broken = input;
    broken[3].words[0] ^= 0x8000;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(broken));
    broken = input;
    broken[10].words[1] = static_cast<u32>(-32);
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(broken));
    std::vector<u64> code(0x50 / 8);
    for (const auto word : lowered.instructions) {
        if ((code.size() * 8) % 32 == 0) code.push_back(0);
        code.push_back(word);
    }
    class BarrierEnvironment final : public EmptyEnvironment {
    public:
        explicit BarrierEnvironment(const std::vector<u64>& code_) : code{code_} { stage = Shader::Stage::VertexB; }
        u64 ReadInstruction(u32 address) override { return code.at(address / 8); }
        const std::vector<u64>& code;
    } env{code};
    Shader::ObjectPool<Shader::Maxwell::Flow::Block> flow_pool;
    Shader::Maxwell::Flow::CFG cfg{env, flow_pool, 0x50};
    INFO(cfg.Dot());
    const u32 offset = lowered.source_instruction_offsets[9];
    const Shader::Maxwell::Location continuation{0x50 + offset * 8 + ((offset + 1) / 3) * 8};
    for (bool predicate : {false, true}) {
        const auto* block = &*cfg.Functions().front().blocks.begin();
        bool visited_continuation = false, exited = false;
        std::string trace;
        for (u32 step = 0; step < 64 && block; ++step) {
            trace += std::to_string(block->begin.Offset()) + ":" + std::to_string(block->end.Offset()) + " ";
            visited_continuation |= !block->begin.IsVirtual() && block->Contains(continuation);
            if (block->end_class == Shader::Maxwell::Flow::EndClass::Exit) { exited = true; break; }
            const auto [pred, inverted] = block->cond.GetPred();
            REQUIRE(block->cond.GetFlowTest() == IR::FlowTest::T);
            REQUIRE((pred == IR::Pred::P0 || pred == IR::Pred::PT));
            const bool taken = (pred == IR::Pred::PT || predicate) != inverted;
            block = taken ? block->branch_true : block->branch_false;
        }
        REQUIRE(exited);
        INFO("path=" << trace << " marker=" << continuation.Offset());
        REQUIRE(visited_continuation == predicate);
    }
}


TEST_CASE("Native byte permutation inserts the low source byte without clobbering aliases", "[native-shader]") {
    for (u32 destination : {0U, 1U, 2U}) {
        const std::array<Native, 2> input{{{{0x00007816U | (destination << 16), 0x7054, 1, 0}}, VertexTransform.back()}};
        Shader::ObjectPool<IR::Inst> pool;
        IR::Block block{pool};
        TranslateScalarNative(input, block);
        for (u32 a : {0U, 0x12345678U, 0xffffffffU})
            for (u32 b : {0U, 0x89abcdefU, 0xffffffffU}) {
                ScalarNativeMachine machine;
                machine.regs[0] = a; machine.regs[1] = b;
                machine.Execute(block);
                REQUIRE(machine.regs[destination] == ((b & 0xff00ffffU) | ((a & 0xffU) << 16)));
            }
    }
}


TEST_CASE("Native constant effective addresses allow an unused carry output", "[native-shader]") {
    const std::array<Native, 2> input{{{{0x00007a11, 0x01002e00, 0x078e40ff, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u32 value : {0U, 1U, 0x80000000U, 0xffffffffU}) {
        ScalarNativeMachine machine;
        machine.regs[0] = value;
        machine.constants[(u64{4} << 32) | 0xb8] = 0x12345678;
        machine.predicates.fill(true);
        machine.Execute(block);
        REQUIRE(machine.regs[0] == ((value << 8) + 0x12345678U));
        for (bool predicate : machine.predicates) REQUIRE(predicate);
    }
}


TEST_CASE("Native scalar cube array sampling preserves layer XYZ and split RGBA", "[native-shader]") {
    std::array<Native, 2> input{{{{0x08007b60, 0xf0800804, 0x041e0f04, 0x100f4400}}, VertexTransform.back()}};
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(lowered);
    REQUIRE(lowered.required_texture_buffer == 2);
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    u32 samples{};
    std::vector<std::pair<u32, u32>> copies;
    for (const auto& inst : block.Instructions()) {
        if (inst.GetOpcode() == IR::Opcode::BoundImageSampleImplicitLod) {
            REQUIRE(inst.Arg(0).U32() == 32);
            REQUIRE(inst.Flags<IR::TextureInstInfo>().type.Value() == Shader::TextureType::ColorArrayCube);
            REQUIRE(inst.Flags<IR::TextureInstInfo>().is_depth.Value() == 0);
            const auto* coords = inst.Arg(1).Inst();
            REQUIRE(coords->GetOpcode() == IR::Opcode::CompositeConstructF32x4);
            for (u32 i = 0; i < 3; ++i)
                REQUIRE(coords->Arg(i).Inst()->Arg(0).Inst()->Arg(0).Reg() == IR::Reg{251 + i});
            REQUIRE(coords->Arg(3).Inst()->GetOpcode() == IR::Opcode::ConvertF32U16);
            ++samples;
        }
        if (inst.GetOpcode() == IR::Opcode::SetRegister && inst.Arg(1).Inst()->GetOpcode() == IR::Opcode::GetRegister)
            copies.emplace_back(static_cast<u32>(inst.Arg(0).Reg()), static_cast<u32>(inst.Arg(1).Inst()->Arg(0).Reg()));
    }
    REQUIRE(samples == 1);
    REQUIRE(copies == std::vector<std::pair<u32, u32>>{{250, 8}, {251, 9}, {252, 4}, {253, 5}, {0, 250}, {1, 251}, {4, 252}, {5, 253}});
    input[0].words[0] |= 1U << 24;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(input));
    input[0].words[0] &= ~(1U << 24);
    input[0].words[2] |= 1U << 23;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(input));
}


TEST_CASE("Native indexed interpolation adds the uniform byte offset to the attribute base", "[native-shader]") {
    const auto ur = [](u32 index) { return 216U - (index / 16) * 16 + index % 16; };
    const u32 source = GENERATE(0U, 4U, 17U, 18U, 32U, 62U);
    std::array<Native, 2> input{{{{0x04307526, 0xff, 0x080e0021, 0x000e6200}}, VertexTransform.back()}};
    input[0].words[0] = (input[0].words[0] & 0x00ffffffU) | (source << 24);
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u32 index = 0; index < 6; ++index) {
        std::array<u32, 256> registers{};
        registers[ur(source)] = index * 16;
        std::unordered_map<const IR::Inst*, u32> values;
        const auto value = [&](const IR::Value& v) { return v.IsImmediate() ? v.U32() : values.at(v.Inst()); };
        bool found{};
        for (const auto& inst : block.Instructions()) {
            switch (inst.GetOpcode()) {
            case IR::Opcode::GetRegister: values[&inst] = registers[static_cast<u32>(inst.Arg(0).Reg())]; break;
            case IR::Opcode::SetRegister: registers[static_cast<u32>(inst.Arg(0).Reg())] = value(inst.Arg(1)); break;
            case IR::Opcode::IAdd32: values[&inst] = value(inst.Arg(0)) + value(inst.Arg(1)); break;
            case IR::Opcode::GetAttributeIndexed:
                REQUIRE(value(inst.Arg(0)) == 0x84 + index * 16);
                found = true;
                break;
            default: FAIL("unexpected instruction before indexed attribute read");
            }
            if (found) break;
        }
        REQUIRE(found);
    }
    input[0].words[0] = (input[0].words[0] & 0x00ffffffU) | (64U << 24);
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(input));
    input[0].words[0] = 0x04307526;
    input[0].words[2] |= 1U << 12;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(input));
}


TEST_CASE("Native cube array explicit LOD preserves the four coordinates and independent LOD", "[native-shader]") {
    const std::array<Native, 2> input{{{{0x04007b60, 0xe0800809, 0x059e0f02, 0x100fc200}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    std::vector<std::pair<u32, u32>> copies;
    u32 samples{};
    for (const auto& inst : block.Instructions()) {
        if (inst.GetOpcode() == IR::Opcode::BoundImageSampleExplicitLod) {
            REQUIRE(inst.Flags<IR::TextureInstInfo>().type.Value() == Shader::TextureType::ColorArrayCube);
            REQUIRE(inst.Arg(2).Inst()->Arg(0).Inst()->Arg(0).Reg() == IR::Reg{9});
            REQUIRE(inst.Arg(0).U32() == 32);
            ++samples;
        }
        if (inst.GetOpcode() == IR::Opcode::SetRegister && inst.Arg(1).Inst()->GetOpcode() == IR::Opcode::GetRegister)
            copies.emplace_back(static_cast<u32>(inst.Arg(0).Reg()), static_cast<u32>(inst.Arg(1).Inst()->Arg(0).Reg()));
    }
    REQUIRE(samples == 1);
    REQUIRE(copies == std::vector<std::pair<u32, u32>>{{250, 4}, {251, 5}, {252, 6}, {253, 7}, {0, 250}, {1, 251}, {2, 252}, {3, 253}});
}


TEST_CASE("Native uniform bit reverse preserves every bit and permits source destination aliases", "[native-shader]") {
    const auto ur = [](u32 index) { return 216U - (index / 16) * 16 + index % 16; };
    for (u32 dest : {4U, 17U, 18U, 30U, 32U, 46U, 48U, 62U}) {
        std::array<Native, 2> input{{{{0x000072be | (dest << 16), 4, 0x08000000, 0}}, VertexTransform.back()}};
        Shader::ObjectPool<IR::Inst> pool;
        IR::Block block{pool};
        TranslateScalarNative(input, block);
        std::vector<u32> inputs{0, ~0U, 0x01234567, 0x89abcdef};
        for (u32 bit = 0; bit < 32; ++bit) inputs.push_back(1U << bit);
        for (const u32 value : inputs) {
            ScalarNativeMachine machine;
            machine.regs[220] = value;
            machine.Execute(block);
            u32 expected{};
            for (u32 bit = 0; bit < 32; ++bit) expected = (expected << 1) | ((value >> bit) & 1);
            REQUIRE(machine.regs[ur(dest)] == expected);
        }
        input[0].words[1] = 63;
        Shader::ObjectPool<IR::Inst> zero_pool;
        IR::Block zero_block{zero_pool};
        TranslateScalarNative(input, zero_block);
        ScalarNativeMachine zero;
        zero.Execute(zero_block);
        REQUIRE(zero.regs[ur(dest)] == 0);
        input[0].words[1] = 64;
        REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(input));
    }
}


TEST_CASE("Native reversed ballot with UFLO shift selects the lowest active lane", "[native-shader]") {
    const std::array<Native, 3> input{{{{0x000472be, 4, 0x08000000, 0}},
        {{0x000472bd, 4, 0x080e0400, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    std::vector<u32> masks{0, ~0U, 0x12345678, 0x80000000};
    for (u32 bit = 0; bit < 32; ++bit) masks.push_back(1U << bit);
    for (u32 mask : masks) {
        ScalarNativeMachine machine;
        machine.regs[220] = mask;
        machine.Execute(block);
        REQUIRE(machine.regs[220] == (mask ? static_cast<u32>(std::countr_zero(mask)) : ~0U));
    }
}


TEST_CASE("Native register ballots retain the active predicate mask", "[native-shader]") {
    const std::array<Native, 2> input{{{{0x001b7806, 0, 0x000e0000, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u32 active : {1U, 0x80000001U, 0x55555555U, ~0U}) {
        for (u32 selected : {0U, 0xaaaaaaaaU, ~0U}) {
            std::array<std::array<u32, IR::NUM_REGS>, 32> registers{};
            std::array<std::array<bool, 7>, 32> predicates{};
            for (u32 lane = 0; lane < 32; ++lane) predicates[lane][0] = (selected >> lane) & 1;
            const auto saved = predicates;
            ExecuteNativeWarp(block, active, registers, predicates);
            for (u32 lane = 0; lane < 32; ++lane) {
                if ((active >> lane) & 1) REQUIRE(registers[lane][27] == (active & selected));
            }
            REQUIRE(predicates == saved);
        }
    }
}


TEST_CASE("Native full warp butterfly reads the XOR partner in every lane", "[native-shader]") {
    for (u32 offset : {1U, 2U, 4U, 8U, 16U}) {
        const std::array<Native, 2> input{{{{0x181b7f89, 0x0c001f00 | (offset << 21), 0x000e0000, 0}}, VertexTransform.back()}};
        Shader::ObjectPool<IR::Inst> pool;
        IR::Block block{pool};
        TranslateScalarNative(input, block);
        std::array<std::array<u32, IR::NUM_REGS>, 32> registers{};
        std::array<std::array<bool, 7>, 32> predicates{};
        for (u32 lane = 0; lane < 32; ++lane) registers[lane][24] = 0x1000 + lane * 17;
        ExecuteNativeWarp(block, ~0U, registers, predicates);
        for (u32 lane = 0; lane < 32; ++lane) REQUIRE(registers[lane][27] == 0x1000 + (lane ^ offset) * 17);
    }
}


TEST_CASE("Native indexed shuffle retains its predicate at the last valid lane", "[native-shader]") {
    const std::array<Native, 2> input{{{{0x181e7589, 0x00001e1b, 0, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u32 target : {0U, 15U, 30U, 31U}) {
        std::array<std::array<u32, IR::NUM_REGS>, 32> registers{};
        std::array<std::array<bool, 7>, 32> predicates{};
        for (u32 lane = 0; lane < 32; ++lane) { registers[lane][24] = 100 + lane; registers[lane][27] = target; }
        ExecuteNativeWarp(block, ~0U, registers, predicates);
        for (u32 lane = 0; lane < 32; ++lane) {
            REQUIRE(registers[lane][30] == 100 + (target <= 30 ? target : lane));
            REQUIRE(predicates[lane][0] == (target <= 30));
        }
    }
}


TEST_CASE("Native upward and immediate indexed shuffles preserve boundary predicates", "[native-shader]") {
    const bool upward = GENERATE(false, true);
    const u32 distance = GENERATE(0U, 1U, 16U, 31U);
    const std::array<Native, 2> input{{
        {{upward ? 0x0b027989U : 0x0b027f89U,
          upward ? 0x04000000U | (distance << 21) : (distance << 21) | 0x1f00U,
          upward ? 0xffU : 0U, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    std::array<std::array<u32, IR::NUM_REGS>, 32> registers{};
    std::array<std::array<bool, 7>, 32> predicates{};
    for (u32 lane = 0; lane < 32; ++lane) registers[lane][11] = 100 + lane;
    ExecuteNativeWarp(block, ~0U, registers, predicates);
    for (u32 lane = 0; lane < 32; ++lane) {
        const bool valid = !upward || lane >= distance;
        REQUIRE(registers[lane][2] == 100 + (upward ? valid ? lane - distance : lane : distance));
        REQUIRE(predicates[lane][0] == valid);
    }
}


TEST_CASE("Native constant bitwise logic reads the selected bank and preserves aliases", "[native-shader]") {
    const u32 destination = GENERATE(0U, 8U);
    const std::array<Native, 2> input{{
        {{0x08007a12U | (destination << 16), 0x00c00100, 0x078e3cff, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u32 value : {0U, 0xffffffffU, 0x80000000U, 0x12345678U}) {
        ScalarNativeMachine machine;
        machine.constants[(u64{3} << 32) | 4] = value;
        machine.regs[8] = 0x5aa5ff00;
        machine.Execute(block);
        REQUIRE(machine.regs[destination] == (0x5aa5ff00U ^ value));
    }
}


TEST_CASE("Native explicit-LOD 2D samples retain packed signed offsets and aliased outputs", "[native-shader]") {
    const u32 mask = GENERATE(1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 11U, 12U, 13U, 14U, 15U);
    const u32 destination = GENERATE(8U, 36U);
    const u32 count = static_cast<u32>(std::popcount(mask));
    const u32 second = count > 2 ? 40 : 255;
    const std::array<Native, 2> input{{
        {{0x08008b60U | (destination << 16), 0x30800824,
          0x019e1000U | (mask << 8) | second, 0}}, VertexTransform.back()}};
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(lowered);
    REQUIRE(lowered.required_texture_buffer == 2);
    for (size_t i = 0; i + 1 < lowered.instructions.size(); ++i) {
        const auto pred = Shader::Maxwell::Instruction{lowered.instructions[i]}.Pred();
        REQUIRE(pred.index == 0);
        REQUIRE(pred.negated);
    }
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    std::vector<std::pair<u32, u32>> copies;
    std::vector<u32> channels;
    u32 samples{};
    for (const auto& inst : block.Instructions()) {
        if (inst.GetOpcode() == IR::Opcode::BoundImageSampleExplicitLod) {
            ++samples;
            REQUIRE(inst.Flags<IR::TextureInstInfo>().type.Value() == Shader::TextureType::Color2D);
            REQUIRE(inst.Arg(0).U32() == 32);
            const auto* coords = inst.Arg(1).Inst();
            REQUIRE(coords->GetOpcode() == IR::Opcode::CompositeConstructF32x2);
            for (u32 i = 0; i < 2; ++i)
                REQUIRE(coords->Arg(i).Inst()->Arg(0).Inst()->Arg(0).Reg() == IR::Reg{250 + i});
            REQUIRE(inst.Arg(2).Inst()->Arg(0).Inst()->Arg(0).Reg() == IR::Reg{252});
            const auto* offsets = inst.Arg(3).Inst();
            REQUIRE(offsets->GetOpcode() == IR::Opcode::CompositeConstructU32x2);
            for (u32 i = 0; i < 2; ++i) {
                const auto* field = offsets->Arg(i).Inst();
                REQUIRE(field->GetOpcode() == IR::Opcode::BitFieldSExtract);
                REQUIRE(field->Arg(0).Inst()->Arg(0).Reg() == IR::Reg{253});
                REQUIRE(field->Arg(1).U32() == i * 4);
                REQUIRE(field->Arg(2).U32() == 4);
            }
        }
        if (inst.GetOpcode() == IR::Opcode::CompositeExtractF32x4) channels.push_back(inst.Arg(1).U32());
        if (inst.GetOpcode() == IR::Opcode::SetRegister && inst.Arg(1).Inst()->GetOpcode() == IR::Opcode::GetRegister)
            copies.emplace_back(static_cast<u32>(inst.Arg(0).Reg()), static_cast<u32>(inst.Arg(1).Inst()->Arg(0).Reg()));
    }
    REQUIRE(samples == 1);
    REQUIRE(copies.size() == 4 + count);
    REQUIRE(copies[0] == std::pair<u32, u32>{250, 8});
    REQUIRE(copies[1] == std::pair<u32, u32>{251, 9});
    REQUIRE(copies[2] == std::pair<u32, u32>{252, 36});
    REQUIRE(copies[3] == std::pair<u32, u32>{253, 37});
    std::vector<u32> expected;
    for (u32 i = 0; i < 4; ++i) if (mask & (1U << i)) expected.push_back(i);
    REQUIRE(channels == expected);
    for (u32 i = 0; i < count; ++i)
        REQUIRE(copies[4 + i] == std::pair<u32, u32>{i < 2 ? destination + i : second + i - 2, 250 + i});
}


TEST_CASE("Native explicit-LOD offset samples reject unimplemented layouts", "[native-shader]") {
    const Native valid{{0x00247b60, 0x30800824, 0x019e13ff, 0}};
    for (u32 mode = 0; mode < 10; ++mode) {
        auto invalid = valid;
        switch (mode) {
        case 0: invalid.words[0] |= 1U << 24; break; // Odd coordinate pair.
        case 1: invalid.words[1] |= 1; break; // Odd LOD/offset pair.
        case 2: invalid.words[0] |= 1U << 16; break; // Odd paired result.
        case 3: invalid.words[2] |= 0x4000; break; // Depth comparison.
        case 4: invalid.words[2] |= 0x8000; break; // Packed-half offset result.
        case 5: invalid.words[2] |= 0x2000; break; // Unknown offset mode.
        case 6: invalid.words[2] = 0x019e17ff; break; // Missing split destination.
        case 7: invalid.words[2] = 0x019e10ff; break; // Empty result mask.
        case 8: invalid.words[1] = (invalid.words[1] & ~0x0fc00000U) | (18U << 22); break;
        case 9: invalid.words[1] = (invalid.words[1] & ~0x003fff00U) | (0x2000U << 8); break;
        }
        const std::array<Native, 2> source{invalid, VertexTransform.back()};
        REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(source));
    }
}


TEST_CASE("Native volume samples pack exactly the selected channels without clobbering aliases", "[native-shader]") {
    const u32 mask = GENERATE(1U, 2U, 4U, 8U, 3U, 9U, 10U, 12U, 7U, 11U, 13U, 14U, 15U);
    const u32 count = static_cast<u32>(std::popcount(mask));
    const u32 dest = count == 1 ? 31 : 30;
    const u32 second = count > 2 ? 40 : 255;
    const std::array<Native, 2> input{{{{0x20008b60 | (dest << 16), 0x5000101f, 0x049e0000 | (mask << 8) | second, 0}}, VertexTransform.back()}};
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(lowered);
    REQUIRE(lowered.required_texture_buffer == 0);
    for (size_t i = 0; i + 1 < lowered.instructions.size(); ++i) {
        const auto pred = Shader::Maxwell::Instruction{lowered.instructions[i]}.Pred();
        REQUIRE(pred.index == 0);
        REQUIRE(pred.negated);
    }
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    std::vector<std::pair<u32, u32>> copies;
    std::vector<u32> channels;
    u32 samples{};
    for (const auto& inst : block.Instructions()) {
        if (inst.GetOpcode() == IR::Opcode::BoundImageSampleExplicitLod) {
            REQUIRE(inst.Flags<IR::TextureInstInfo>().type.Value() == Shader::TextureType::Color3D);
            REQUIRE(inst.Arg(0).U32() == 64);
            REQUIRE(inst.Arg(2).F32() == 0);
            ++samples;
        }
        if (inst.GetOpcode() == IR::Opcode::CompositeExtractF32x4) channels.push_back(inst.Arg(1).U32());
        if (inst.GetOpcode() == IR::Opcode::SetRegister && inst.Arg(1).Inst()->GetOpcode() == IR::Opcode::GetRegister)
            copies.emplace_back(static_cast<u32>(inst.Arg(0).Reg()), static_cast<u32>(inst.Arg(1).Inst()->Arg(0).Reg()));
    }
    REQUIRE(samples == 1);
    REQUIRE(copies.size() == 3 + count);
    REQUIRE(copies[0] == std::pair<u32, u32>{250, 32});
    REQUIRE(copies[1] == std::pair<u32, u32>{251, 33});
    REQUIRE(copies[2] == std::pair<u32, u32>{252, 31});
    std::vector<u32> expected;
    for (u32 i = 0; i < 4; ++i) if (mask & (1U << i)) expected.push_back(i);
    REQUIRE(channels == expected);
    for (u32 i = 0; i < count; ++i)
        REQUIRE(copies[3 + i] == std::pair<u32, u32>{i < 2 ? dest + i : second + i - 2, 250 + i});
}


TEST_CASE("Native half texture samples pack RG and BA into separate registers", "[native-shader]") {
    const bool half = GENERATE(false, true);
    const std::array<Native, 2> input{{{{0x06047b60, 0xb0800804, half ? 0x001e8f06U : 0x001e0f06U, 0x101f4400}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    u32 samples{}, packs{};
    std::vector<u32> destinations;
    for (const auto& inst : block.Instructions()) {
        if (inst.GetOpcode() == IR::Opcode::BoundImageSampleImplicitLod) {
            REQUIRE(inst.Flags<IR::TextureInstInfo>().type.Value() == Shader::TextureType::ColorArray2D);
            ++samples;
        }
        if (inst.GetOpcode() == IR::Opcode::PackHalf2x16) ++packs;
        if (inst.GetOpcode() == IR::Opcode::SetRegister)
            destinations.push_back(static_cast<u32>(inst.Arg(0).Reg()));
    }
    REQUIRE(samples == 1);
    REQUIRE(packs == (half ? 2U : 0U));
    REQUIRE(destinations == (half ? std::vector<u32>{4, 6} : std::vector<u32>{4, 5, 6, 7}));
}


TEST_CASE("Native unsigned 64-bit float conversion preserves both output words", "[native-shader]") {
    const u32 destination = GENERATE(4U, 6U);
    const bool overlap = GENERATE(false, true);
    const u32 source = overlap ? destination : 12U;
    const std::array<Native, 2> input{{
        {{(destination << 16) | 0x7311U, source, 0x0021d800, 0x000e3000}},
        VertexTransform.back(),
    }};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    const std::array<std::pair<u32, u64>, 16> cases{{
        {0x00000000U, 0}, {0x80000000U, 0}, {0x00000001U, 0}, {0x80000001U, 0},
        {0x3ff33333U, 1}, {0xbff33333U, 0},
        {0x4f800000U, 0x0000000100000000ULL},
        {0x4f800001U, 0x0000000100000200ULL},
        {0x53800002U, 0x0000010000040000ULL},
        {0x5f7fffffU, 0xffffff0000000000ULL},
        {0x5f800000U, ~u64{0}}, {0x7f800000U, ~u64{0}}, {0xff800000U, 0},
        {0x7fc12345U, 0x8000000000000000ULL}, {0xffc12345U, 0x8000000000000000ULL},
        {0x7f7fffffU, ~u64{0}},
    }};
    for (const auto& [bits, expected] : cases) {
        INFO("input bits=" << bits);
        ScalarNativeMachine machine;
        machine.regs.fill(0x13579bdfU);
        machine.regs[source] = bits;
        machine.predicates = {true, false, true, false, true, false, true};
        const auto predicates = machine.predicates;
        machine.Execute(block);
        REQUIRE(machine.regs[destination] == static_cast<u32>(expected));
        REQUIRE(machine.regs[destination + 1] == static_cast<u32>(expected >> 32));
        REQUIRE(machine.regs[destination + 2] == 0x13579bdfU);
        REQUIRE(machine.predicates == predicates);
        if (!overlap) REQUIRE(machine.regs[source] == bits);
    }
}


TEST_CASE("Native 64-bit float conversion retains predicates and rejects other modes", "[native-shader]") {
    const u32 predicate = GENERATE(0U, 7U, 8U);
    const std::array<Native, 2> input{{
        {{(4U << 16) | (predicate << 12) | 0x311U, 4, 0x0021d800, 0}},
        VertexTransform.back(),
    }};
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(lowered);
    REQUIRE(lowered.instructions.size() == 2);
    REQUIRE(Shader::Maxwell::Decode(lowered.instructions.front()) == Shader::Maxwell::Opcode::F2I_reg);
    const auto actual = Shader::Maxwell::Instruction{lowered.instructions.front()}.Pred();
    REQUIRE(actual.index == (predicate & 7));
    REQUIRE(actual.negated == ((predicate & 8) != 0));
    for (u32 invalid = 0; invalid < 6; ++invalid) {
        auto modified = input;
        if (invalid == 0) modified[0].words[2] ^= 1U << 8;
        if (invalid == 1) modified[0].words[2] ^= 1U << 16;
        if (invalid == 2) modified[0].words[0] ^= 1U << 16;
        if (invalid == 3) modified[0].words[0] = (254U << 16) | (predicate << 12) | 0x311U;
        if (invalid == 4) modified[0].words[1] |= 1U << 8;
        if (invalid == 5) modified[0].words[0] |= 1U << 24;
        REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(modified));
    }
}


TEST_CASE("Native three-input addition produces both cumulative carry predicates", "[native-shader]") {
    const bool extended = GENERATE(false, true);
    const bool constant_b = GENERATE(false, true);
    const u32 negatives = GENERATE(0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U);
    const u32 destination = GENERATE(0U, 1U, 2U, 4U, 255U);
    const u32 carry_mode = GENERATE(0U, 1U, 2U, 3U);
    const u32 first_carry = carry_mode == 2 ? 7U : carry_mode == 3 ? 15U : carry_mode == 1 ? 8U : 0U;
    const u32 second_carry = carry_mode == 2 ? 15U : carry_mode == 3 ? 7U : 1U;
    u32 flags = 0x07ffe002U & ~((7U << 17) | (7U << 20));
    flags |= 1U << 20;
    if (negatives & 1) flags |= 1U << 8;
    if (negatives & 4) flags |= 1U << 11;
    if (extended) {
        flags &= ~((15U << 23) | (15U << 13));
        flags |= (1U << 10) | (first_carry << 23) | (second_carry << 13);
    }
    const std::array<Native, 2> input{{
        {{(destination << 16) | (constant_b ? 0x7a10U : 0x7210U),
          (constant_b ? 0x00c04000U : 1U) | ((negatives & 2) ? 0x80000000U : 0U), flags, 0}},
        VertexTransform.back(),
    }};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    const std::array<std::array<u32, 3>, 8> cases{{
        {{0, 0, 0}}, {{0xffffffffU, 0xffffffffU, 0xffffffffU}},
        {{0xffffffffU, 1, 0}}, {{0xffffffffU, 0xffffffffU, 1}},
        {{0, 1, 0xffffffffU}}, {{1, 0xffffffffU, 0}},
        {{0x80000000U, 0x7fffffffU, 1}}, {{0x12345678U, 0x87654321U, 0xabcdef01U}},
    }};
    for (const auto& values : cases) for (u32 carry_values = 0; carry_values < 4; ++carry_values) {
        ScalarNativeMachine machine;
        std::copy(values.begin(), values.end(), machine.regs.begin());
        if (constant_b) machine.constants[(u64{3} << 32) | 0x100] = values[1];
        machine.predicates = {bool(carry_values & 1), bool(carry_values & 2), true, false, true, false, true};
        auto expected_predicates = machine.predicates;
        u64 expected{};
        for (u32 i = 0; i < 3; ++i) {
            if (negatives & (1U << i)) expected += u64{values[i] ^ 0xffffffffU} + (extended ? 0U : 1U);
            else expected += values[i];
        }
        const auto carry = [&](u32 code) {
            const bool value = (code & 7) == 7 ? true : machine.predicates[code & 7];
            return value != ((code & 8) != 0);
        };
        if (extended) expected += static_cast<u32>(carry(first_carry)) + static_cast<u32>(carry(second_carry));
        expected_predicates[0] = expected >= 0x100000000ULL;
        expected_predicates[1] = expected >= 0x200000000ULL;
        machine.Execute(block);
        if (destination != 255) REQUIRE(machine.regs[destination] == static_cast<u32>(expected));
        REQUIRE(machine.predicates == expected_predicates);
        for (u32 i = 0; i < 3; ++i) if (i != destination) REQUIRE(machine.regs[i] == values[i]);
    }
}


TEST_CASE("Captured unsigned division helper preserves the full quotient", "[.native-division-captured]") {
    const char* directory = std::getenv("EDEN_NATIVE_SHADER_FIXTURES");
    REQUIRE(directory != nullptr);
    std::ifstream file(std::filesystem::path{directory} / "division.source.bin", std::ios::binary | std::ios::ate);
    REQUIRE(file.good());
    const auto size = static_cast<size_t>(file.tellg());
    REQUIRE(size % sizeof(Native) == 0);
    std::vector<Native> input(size / sizeof(Native));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(input.data()), size);
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    const auto check = [&](u64 dividend, u64 divisor) {
        ScalarNativeMachine machine;
        machine.regs[2] = static_cast<u32>(dividend); machine.regs[3] = static_cast<u32>(dividend >> 32);
        machine.regs[4] = static_cast<u32>(divisor); machine.regs[1] = static_cast<u32>(divisor >> 32);
        machine.Execute(block);
        const u64 expected = divisor ? dividend / divisor : ~u64{0};
        INFO("dividend=" << dividend << " divisor=" << divisor);
        if (((u64{machine.regs[3]} << 32) | machine.regs[2]) != expected) {
            if (const char* path = std::getenv("EDEN_NATIVE_ARITHMETIC_REPORT")) {
                std::ofstream report(path);
                ScalarNativeMachine trace;
                trace.regs[2] = static_cast<u32>(dividend); trace.regs[3] = static_cast<u32>(dividend >> 32);
                trace.regs[4] = static_cast<u32>(divisor); trace.regs[1] = static_cast<u32>(divisor >> 32);
                for (size_t index = 0; index + 1 < input.size(); ++index) {
                    const std::array<Native, 2> step{{input[index], VertexTransform.back()}};
                    Shader::ObjectPool<IR::Inst> step_pool;
                    IR::Block step_block{step_pool};
                    TranslateScalarNative(step, step_block);
                    trace.Execute(step_block);
                    report << index;
                    for (u32 reg = 0; reg < 16; ++reg) report << ',' << trace.regs[reg];
                    for (bool pred : trace.predicates) report << ',' << pred;
                    report << '\n';
                }
            }
        }
        REQUIRE(((u64{machine.regs[3]} << 32) | machine.regs[2]) == expected);
    };
    const std::array<u64, 12> values{{0, 1, 2, 3, 0xffffU, 0xffffffffULL, 0x100000000ULL,
        0x100000001ULL, 0x7fffffffffffffffULL, 0x8000000000000000ULL, 0xfffffffffffffffeULL, ~u64{0}}};
    for (u64 dividend : values) for (u64 divisor : values) check(dividend, divisor);
    u64 random = 0x817421ac679adb3fULL;
    const auto next = [&]() { random ^= random << 13; random ^= random >> 7; random ^= random << 17; return random; };
    for (u32 i = 0; i < 1024; ++i) {
        const u64 dividend = next(), divisor = next();
        check(dividend, divisor);
        check(dividend, divisor >> 32);
    }
}


TEST_CASE("Native predicated carry chains snapshot their guard until the final output", "[native-shader]") {
    const u32 guard = GENERATE(0U, 8U, 1U, 9U, 4U, 12U, 7U, 15U);
    const bool constant_b = GENERATE(false, true);
    const bool extended = GENERATE(false, true);
    const u32 flags = extended ? ((0x07ffe002U & ~((7U << 17) | (7U << 20) | (15U << 23) | (15U << 13))) |
        (1U << 20) | (1U << 10) | (1U << 13)) : ((0x07ffe002U & ~((7U << 17) | (7U << 20))) | (1U << 20));
    const std::array<Native, 2> input{{
        {{(4U << 16) | (guard << 12) | (constant_b ? 0xa10U : 0x210U), constant_b ? 0x00c04000U : 1U, flags, 0}},
        VertexTransform.back(),
    }};
    for (bool enabled : {false, true})
        for (const auto& operands : std::array<std::array<u32, 3>, 3>{{{{0, 0, 0}}, {{0xffffffffU, 1, 0}}, {{0xffffffffU, 0xffffffffU, 2}}}}) {
            ScalarNativeMachine machine;
            std::copy(operands.begin(), operands.end(), machine.regs.begin());
            machine.regs[4] = 0xdeadbeef;
            machine.constants[(u64{3} << 32) | 0x100] = operands[1];
            machine.predicates = {false, true, true, false, true, false, true};
            if ((guard & 7) != 7) machine.predicates[guard & 7] = enabled != bool(guard & 8);
            auto expected_predicates = machine.predicates;
            const bool active = ((guard & 7) == 7 ? true : machine.predicates[guard & 7]) != bool(guard & 8);
            u64 sum = u64{operands[0]} + operands[1] + operands[2];
            if (extended) sum += static_cast<u32>(machine.predicates[0]) + static_cast<u32>(machine.predicates[1]);
            if (active) {
                expected_predicates[0] = sum >= 0x100000000ULL;
                expected_predicates[1] = sum >= 0x200000000ULL;
            }
            ExecutePredicatedScalarNative(input, machine);
            REQUIRE(machine.regs[4] == (active ? static_cast<u32>(sum) : 0xdeadbeefU));
            REQUIRE(machine.predicates == expected_predicates);
        }
}


TEST_CASE("Native predicated loop exit is a forward transfer with no pending return", "[native-shader]") {
    const u32 predicate = GENERATE(3U, 11U);
    const std::array<Native, 6> input{{
        {{0x00007802, 0, 0xf00, 0}},
        {{0x00007810, 1, 0x07ffe0ff, 0}},
        {{0x0000780c, 4, 0x03f66270, 0}},
        {{(predicate << 12) | 0x944U, 16, 0x03c00000, 0}},
        {{0x7947, 0xffffffc0, 0x0383ffff, 0}},
        VertexTransform.back(),
    }};
    Shader::Ampere::ProgramCaptureState capture;
    for (u32 index = 0; index < input.size(); ++index)
        REQUIRE(capture.Observe(input[index], index) == (index + 1 == input.size()
            ? Shader::Ampere::ProgramCaptureState::Result::Complete : Shader::Ampere::ProgramCaptureState::Result::Continue));
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    INFO(lowered.error << " offset=" << lowered.error_offset);
    REQUIRE(lowered);
    u32 calls{};
    for (u64 word : lowered.instructions) calls += Shader::Maxwell::Decode(word) == Shader::Maxwell::Opcode::CAL;
    REQUIRE(calls == 0);
    auto invalid = input;
    invalid[4].words[1] = 0;
    invalid[4].words[2] = 0x03800000;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(invalid));
    invalid = input;
    invalid[4].words[1] = 0xffffffa0;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(invalid));
}


TEST_CASE("Native byte permutation retains selectors repeated bytes and input aliases", "[native-shader]") {
    const u32 selector = GENERATE(0x4431U, 0x7531U, 0x0000U, 0x7777U, 0x3210U, 0x7654U, 0x1357U, 0x6420U);
    const u32 destination = GENERATE(0U, 1U, 2U);
    const bool zero_second = GENERATE(false, true);
    const std::array<Native, 2> input{{
        {{(destination << 16) | 0x7816U, selector, zero_second ? 255U : 1U, 0}}, VertexTransform.back(),
    }};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u32 a : {0U, 0xffffffffU, 0x01234567U, 0x89abcdefU, 0x80000000U})
        for (u32 b : {0U, 0xffffffffU, 0x01234567U, 0x89abcdefU, 0x80000000U}) {
            ScalarNativeMachine machine;
            machine.regs[0] = a; machine.regs[1] = b;
            machine.predicates = {true, false, true, false, true, false, true};
            const auto predicates = machine.predicates;
            const u64 bytes = (u64{zero_second ? 0U : b} << 32) | a;
            u32 expected{};
            for (u32 byte = 0; byte < 4; ++byte)
                expected |= static_cast<u32>((bytes >> (((selector >> (byte * 4)) & 7) * 8)) & 0xff) << (byte * 8);
            machine.Execute(block);
            REQUIRE(machine.regs[destination] == expected);
            REQUIRE(machine.predicates == predicates);
        }
    auto unsupported = input;
    unsupported[0].words[1] |= 0x8000;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(unsupported));
}


TEST_CASE("Native extended constant-buffer addition retains its register source and carry", "[native-shader]") {
    const u32 destination = GENERATE(0U, 4U);
    const u32 carry = GENERATE(0U, 7U, 8U, 15U);
    const std::array<Native, 2> input{{
        {{(destination << 16) | 0x7a10U, 0x00c04000, 0x007fe4ffU | (carry << 23), 0}}, VertexTransform.back(),
    }};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u32 a : {0U, 1U, 0x80000000U, 0xffffffffU})
        for (u32 b : {0U, 1U, 0x76543210U, 0xffffffffU}) for (bool active : {false, true}) {
            ScalarNativeMachine machine;
            machine.regs[0] = a;
            machine.constants[(u64{3} << 32) | 0x100] = b;
            machine.predicates.fill(active);
            const auto predicates = machine.predicates;
            const bool enabled = ((carry & 7) == 7 ? true : active) != bool(carry & 8);
            machine.Execute(block);
            REQUIRE(machine.regs[destination] == a + b + static_cast<u32>(enabled));
            REQUIRE(machine.predicates == predicates);
        }
}


TEST_CASE("Native shared matrix loads distribute each row across the correct warp lanes", "[native-shader]") {
    const bool transpose = GENERATE(false, true);
    const u32 destination = GENERATE(8U, 12U);
    const u32 matrices = GENERATE(2U, 4U);
    const u32 displacement = GENERATE(0U, 0x4c80U);
    const bool uniform = GENERATE(false, true);
    const std::array<Native, 2> input{{
        {{(12U << 24) | (destination << 16) | 0x783bU, (displacement << 8) | (uniform ? 5U : 0U),
            (uniform ? 0x08000000U : 0U) | (transpose ? 0x4000U : 0U) | (matrices == 2 ? 0x100U : 0x200U), 0}}, VertexTransform.back(),
    }};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    std::array<std::array<u32, IR::NUM_REGS>, 32> registers{};
    std::array<std::array<bool, 7>, 32> predicates{};
    std::unordered_map<u32, u32> shared;
    const auto packed = [](u32 matrix, u32 row, u32 column) {
        const u32 value = matrix * 1000 + row * 100 + column;
        return value | ((value + 1) << 16);
    };
    for (u32 lane = 0; lane < 32; ++lane) {
        const u32 matrix = lane / 8, row = lane % 8;
        registers[lane][12] = 0x1000 + matrix * 0x200 + row * 32;
        registers[lane][221] = 0x80;
        predicates[lane] = {true, false, true, false, true, false, true};
        for (u32 pair = 0; pair < 4; ++pair)
            shared[registers[lane][12] + (uniform ? 0x80 : 0) + displacement + pair * 4] = packed(matrix, row, pair * 2);
    }
    const auto original = registers;
    const auto original_predicates = predicates;
    ExecuteNativeWarp(block, 0xffffffffU, registers, predicates, &shared);
    for (u32 lane = 0; lane < 32; ++lane) {
        for (u32 matrix = 0; matrix < matrices; ++matrix)
            REQUIRE(registers[lane][destination + matrix] == (transpose ? ((matrix * 1000 + (lane % 4) * 200 + lane / 4) | ((matrix * 1000 + ((lane % 4) * 2 + 1) * 100 + lane / 4) << 16)) : packed(matrix, lane / 4, (lane % 4) * 2)));
        for (u32 reg = 0; reg < 240; ++reg)
            if (reg < destination || reg >= destination + matrices) REQUIRE(registers[lane][reg] == original[lane][reg]);
    }
    REQUIRE(predicates == original_predicates);
    auto unsupported = input;
    unsupported[0].words[2] ^= 0x100;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(unsupported));
}


TEST_CASE("Native half matrix multiply matches dense and identity matrix products", "[native-shader]") {
    const u32 original_destination = GENERATE(8U, 10U, 16U, 18U, 24U, 32U);
    const bool shadow = GENERATE(false, true);
    const u32 destination = shadow ? 250U : original_destination;
    const u32 source_a = shadow ? 240U : 8U, source_b = shadow ? 248U : 16U, source_c = shadow ? 244U : 24U;
    const auto guest = [&](u32 reg) { return shadow && reg >= 240 ? reg + 256 : reg; };
    const u32 depth = GENERATE(8U, 16U);
    const bool zero_accumulator = GENERATE(false, true);
    const bool identity = GENERATE(false, true);
    const std::array<Native, 2> input{{
        {{(source_a << 24) | (destination << 16) | 0x723cU, source_b, (depth == 16 ? 0x800U : 0U) | (zero_accumulator ? 255U : source_c), 0}},
        VertexTransform.back(),
    }};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block, shadow);
    std::array<std::array<double, 16>, 16> a{};
    std::array<std::array<double, 8>, 16> b{}, c{}, expected{};
    for (u32 row = 0; row < 16; ++row) {
        for (u32 k = 0; k < depth; ++k)
            a[row][k] = identity ? static_cast<double>(row == k) : (static_cast<int>((row * 17 + k * 5) % 9) - 4) * 0.25;
        for (u32 col = 0; col < 8; ++col) {
            b[row][col] = (static_cast<int>((row * 7 + col * 3) % 11) - 5) * 0.25;
            c[row][col] = (static_cast<int>((row * 3 + col) % 7) - 3) * 0.5;
        }
    }
    for (u32 row = 0; row < 16; ++row) for (u32 col = 0; col < 8; ++col) {
        double result = zero_accumulator ? 0.0 : c[row][col];
        for (u32 k = 0; k < depth; ++k) result += a[row][k] * b[k][col];
        expected[row][col] = result;
    }
    const auto pack = [](double low, double high) { return u32{EncodeTestHalf(low)} | (u32{EncodeTestHalf(high)} << 16); };
    std::array<std::array<u32, IR::NUM_REGS>, 32> registers{};
    std::array<std::array<bool, 7>, 32> predicates{};
    for (u32 lane = 0; lane < 32; ++lane) {
        const u32 group = lane / 4, thread = lane % 4;
        for (u32 fragment = 0; fragment < depth / 4; ++fragment) {
            const u32 row = group + ((fragment & 1) ? 8 : 0), col = thread * 2 + (fragment >= 2 ? 8 : 0);
            registers[lane][guest(source_a + fragment)] = pack(a[row][col], a[row][col + 1]);
        }
        for (u32 fragment = 0; fragment < 2; ++fragment) {
            const u32 row = thread * 2 + fragment * 8;
            if (fragment < depth / 8) registers[lane][guest(source_b + fragment)] = pack(b[row][group], b[row + 1][group]);
            registers[lane][guest(source_c + fragment)] = pack(c[group + fragment * 8][thread * 2], c[group + fragment * 8][thread * 2 + 1]);
        }
        predicates[lane] = {true, false, true, false, true, false, true};
    }
    const auto original = registers;
    const auto original_predicates = predicates;
    ExecuteNativeWarp(block, 0xffffffffU, registers, predicates);
    for (u32 lane = 0; lane < 32; ++lane) {
        const u32 group = lane / 4, thread = lane % 4;
        for (u32 fragment = 0; fragment < 2; ++fragment) {
            const u32 row = group + fragment * 8;
            REQUIRE(registers[lane][guest(destination + fragment)] == pack(expected[row][thread * 2], expected[row][thread * 2 + 1]));
        }
        for (u32 reg = 0; reg < (shadow ? 255U : 240U); ++reg)
            if (reg < destination || reg >= destination + 2) REQUIRE(registers[lane][guest(reg)] == original[lane][guest(reg)]);
    }
    REQUIRE(predicates == original_predicates);
}


TEST_CASE("Native packed dot product selects unsigned halves bytes and accumulator", "[native-shader]") {
    const u32 destination = GENERATE(0U, 1U, 2U, 4U);
    const bool zero_accumulator = GENERATE(false, true);
    const std::array<Native, 2> input{{
        {{(destination << 16) | 0x7226U, 1, 0x1000U | (zero_accumulator ? 255U : 2U), 0}}, VertexTransform.back(),
    }};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u32 a : {0U, 0x0001ffffU, 0xffff0001U, 0x80008000U, 0xffffffffU})
        for (u32 b : {0U, 0x01020304U, 0xabcd0102U, 0xffffffffU})
            for (u32 c : {0U, 1U, 0xffffffffU}) {
                ScalarNativeMachine machine;
                machine.regs[0] = a; machine.regs[1] = b; machine.regs[2] = c;
                machine.predicates = {true, false, true, false, true, false, true};
                const auto predicates = machine.predicates;
                const u32 expected = (a & 0xffff) * (b & 0xff) + (a >> 16) * ((b >> 8) & 0xff) +
                    (zero_accumulator ? 0U : c);
                machine.Execute(block);
                REQUIRE(machine.regs[destination] == expected);
                REQUIRE(machine.predicates == predicates);
            }
    auto unsupported = input;
    unsupported[0].words[2] ^= 0x200;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(unsupported));
}


TEST_CASE("Native descriptor-addressed global loads retain the explicit 64-bit pointer", "[native-shader]") {
    const u32 width = GENERATE(4U, 5U);
    const std::array<Native, 2> input{{
        {{0x02047981, 0x2008, 0x0c1e1100U | (width << 9), 0}}, VertexTransform.back(),
    }};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    ScalarNativeMachine machine;
    machine.regs[2] = 0xfffffff0; machine.regs[3] = 0x12345678;
    machine.regs[224] = 0xabcdef01; machine.regs[225] = 0x87654321;
    machine.global[0x1234567900000010ULL] = 0x13579bdf;
    machine.global[0x1234567900000014ULL] = 0x2468ace0;
    machine.Execute(block);
    REQUIRE(machine.regs[4] == 0x13579bdfU);
    if (width == 5) REQUIRE(machine.regs[5] == 0x2468ace0U);
    REQUIRE(machine.regs[224] == 0xabcdef01U);
    REQUIRE(machine.regs[225] == 0x87654321U);
}


TEST_CASE("Native global-to-shared copies transfer four words or zero-fill without reading", "[native-shader]") {
    const bool zero_fill = GENERATE(false, true);
    const s32 global_offset = GENERATE(0, 32, 512, -16);
    const s32 shared_offset = GENERATE(0, 16, 4096, -16);
    const std::array<Native, 4> input{{
        {{0x02047fae, ((static_cast<u32>(shared_offset / 16) & 0xffffU) << 16) | (static_cast<u32>(global_offset) & 0xfffU),
            0x0f901c08U | (zero_fill ? 0x40U : 0U), 0}},
        {{0x79af, 0, 0, 0}},
        {{0x791a, 0x8000, 0, 0}},
        VertexTransform.back(),
    }};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    ScalarNativeMachine machine;
    machine.regs[2] = 0xfffffff0; machine.regs[3] = 1; machine.regs[4] = 64;
    machine.regs[224] = 0xabcdef12; machine.regs[225] = 0x76543210;
    machine.predicates = {true, false, true, false, true, false, true};
    const auto predicates = machine.predicates;
    const std::array<u32, 4> data{0x12345678, 0x9abcdef0, 0x76543210, 0xfedcba98};
    if (!zero_fill) for (u32 component = 0; component < 4; ++component)
        machine.global[0x1fffffff0ULL + global_offset + component * 4] = data[component];
    machine.shared[64 + shared_offset - 4] = 0xabababab;
    machine.shared[64 + shared_offset + 16] = 0xcdcdcdcd;
    machine.Execute(block);
    for (u32 component = 0; component < 4; ++component)
        REQUIRE(machine.shared.at(64 + shared_offset + component * 4) == (zero_fill ? 0U : data[component]));
    REQUIRE(machine.shared.size() == 6);
    REQUIRE(machine.shared.at(64 + shared_offset - 4) == 0xabababab);
    REQUIRE(machine.shared.at(64 + shared_offset + 16) == 0xcdcdcdcd);
    REQUIRE(machine.predicates == predicates);
    REQUIRE(machine.shared_memory_barriers == 3);
    REQUIRE(machine.regs[2] == 0xfffffff0U);
    REQUIRE(machine.regs[3] == 1);
    REQUIRE(machine.regs[4] == 64);
    REQUIRE(machine.regs[224] == 0xabcdef12U);
    REQUIRE(machine.regs[225] == 0x76543210U);
}


TEST_CASE("Native global-to-shared copy retains its source predicate and rejects other modes", "[native-shader]") {
    const u32 condition = GENERATE(0U, 4U, 7U, 8U, 12U);
    std::array<Native, 2> input{{
        {{0x02047fae, 0, 0x0c101c08U | ((condition & 7) << 23) | ((condition >> 3) << 6), 0}},
        VertexTransform.back(),
    }};
    const auto result = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(result);
    u32 loads{};
    for (u64 word : result.instructions) {
        if (Shader::Maxwell::Decode(word) != Shader::Maxwell::Opcode::LDG) continue;
        const auto pred = Shader::Maxwell::Instruction{word}.Pred();
        REQUIRE(pred.index == (condition & 7));
        REQUIRE(pred.negated == bool(condition & 8));
        ++loads;
    }
    REQUIRE(loads == 4);
    auto invalid = input;
    invalid[0].words[0] ^= 0x1000;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(invalid));
    invalid = input;
    invalid[0].words[0] |= 1U << 24;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(invalid));
    invalid = input;
    invalid[0].words[2] ^= 0x200;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(invalid));
}


TEST_CASE("Native high multiplication reads a full constant-buffer multiplier", "[native-shader]") {
    const u32 destination = GENERATE(0U, 2U);
    const std::array<Native, 2> input{{
        {{(destination << 16) | 0x7a27U, 0x00c0b300, 0x078e00ff, 0}}, VertexTransform.back(),
    }};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u32 a : {0U, 1U, 0x12345678U, 0x80000000U, 0xffffffffU})
        for (u32 b : {0U, 1U, 0x87654321U, 0x80000000U, 0xffffffffU}) {
            ScalarNativeMachine machine;
            machine.regs[0] = a;
            machine.constants[(u64{3} << 32) | 0x2cc] = b;
            machine.Execute(block);
            REQUIRE(machine.regs[destination] == static_cast<u32>((u64{a} * b) >> 32));
        }
}


TEST_CASE("Native multiple main exits retain forward paths and shared library calls", "[native-shader]") {
    const std::array<Native, 10> input{{
        {{0x7947, 48, 0x03800000, 0}},
        {{0x00087802, 48, 0xf00, 0}},
        {{0x7944, 64, 0x03c00000, 0}},
        VertexTransform.back(),
        {{0x00087802, 96, 0xf00, 0}},
        {{0x7944, 16, 0x03c00000, 0}},
        VertexTransform.back(),
        {{0x00037421, 0x40400000, 0x00010000, 0}},
        {{0x00097802, 0, 0xf00, 0}},
        {{0x08007950, 0xffffff60, 0x03c3ffff, 0}},
    }};
    const auto result = Shader::Ampere::LowerToMaxwell(input);
    INFO(result.error << " offset=" << result.error_offset);
    REQUIRE(result);
    REQUIRE(result.source_size == input.size() * sizeof(Native));
    auto invalid = input;
    invalid[0].words[1] = 112;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(invalid));
}


TEST_CASE("Native global stores preserve scalar widths paired data and signed displacement", "[native-shader]") {
    const u32 width = GENERATE(0U, 2U, 4U, 5U);
    const u32 displacement = GENERATE(0U, 8U, 0xfffff8U);
    const std::array<Native, 2> input{{
        {{0x02007986, (displacement << 8) | 6U, 0x0c101104U | (width << 9), 0}}, VertexTransform.back(),
    }};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    ScalarNativeMachine machine;
    machine.regs[2] = 0xfffffffc; machine.regs[3] = 0x10;
    machine.regs[220] = 0x1000; machine.regs[221] = 2;
    machine.regs[6] = 0x89abcdef; machine.regs[7] = 0x76543210;
    const u64 address = 0x10fffffffcULL +
        static_cast<u64>(static_cast<s64>(static_cast<s32>(displacement << 8) >> 8));
    machine.Execute(block);
    REQUIRE(machine.global.size() == (width == 5 ? 2U : 1U));
    REQUIRE(machine.global.at(address) == (width == 0 ? 0xefU : width == 2 ? 0xcdefU : 0x89abcdefU));
    if (width == 5) {
        REQUIRE(machine.global.at(address + 4) == 0x76543210U);
        auto invalid = input;
        invalid[0].words[1] += 1;
        REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(invalid));
    }
}


TEST_CASE("Native wide multiply reads both words of a constant-buffer addend", "[native-shader]") {
    const u32 destination = GENERATE(0U, 2U, 4U);
    const bool signed_product = GENERATE(false, true);
    const u32 output = GENERATE(0U, 7U);
    const std::array<Native, 2> input{{
        {{(destination << 16) | 0x7625U, 0x8000, 0x07800002U | (output << 17) | (signed_product ? 0x200U : 0U), 0}}, VertexTransform.back(),
    }};
    if (signed_product && output != 7) {
        REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(input));
        return;
    }
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u32 a : {0U, 1U, 0x80000000U, 0xffffffffU})
        for (u32 b : {0U, 1U, 0x87654321U, 0xffffffffU})
            for (u64 c : {0ULL, 0xffffffffULL, 0x1234567887654321ULL, 0xffffffffffffffffULL}) {
                ScalarNativeMachine machine;
                machine.regs[0] = a; machine.regs[2] = b;
                machine.constants[0x200] = static_cast<u32>(c);
                machine.constants[0x204] = static_cast<u32>(c >> 32);
                const u64 product = signed_product ? static_cast<u64>(s64{static_cast<s32>(a)} * static_cast<s32>(b)) : u64{a} * b;
                const u64 expected = product + c;
                auto predicates = machine.predicates;
                if (output != 7) predicates[output] = expected < product;
                machine.Execute(block);
                REQUIRE(((u64{machine.regs[destination + 1]} << 32) | machine.regs[destination]) == expected);
                REQUIRE(machine.predicates == predicates);
            }
    auto invalid = input;
    invalid[0].words[1] += 0x100;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(invalid));
}


TEST_CASE("Native downward shuffle retains warp bounds and aliased source values", "[native-shader]") {
    const u32 destination = GENERATE(3U, 8U);
    const u32 distance = GENERATE(1U, 2U, 16U, 31U);
    const std::array<Native, 2> input{{
        {{(8U << 24) | (destination << 16) | 0x7f89U, 0x08001f00U | (distance << 21), 0x000e0000, 0}},
        VertexTransform.back(),
    }};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u32 active : {0xffffffffU, 0x55555555U, 0x80000001U}) {
        std::array<std::array<u32, IR::NUM_REGS>, 32> registers{};
        std::array<std::array<bool, 7>, 32> predicates{};
        for (u32 lane = 0; lane < 32; ++lane) registers[lane][8] = lane * 17 + 7;
        const auto original = registers;
        ExecuteNativeWarp(block, active, registers, predicates);
        for (u32 lane = 0; lane < 32; ++lane) {
            if (!(active & (1U << lane))) { REQUIRE(registers[lane] == original[lane]); continue; }
            const u32 source = lane + distance <= 31 ? lane + distance : lane;
            if (active & (1U << source)) REQUIRE(registers[lane][destination] == original[source][8]);
        }
        REQUIRE(predicates == std::array<std::array<bool, 7>, 32>{});
    }
}


TEST_CASE("Native NaN-propagating floating min/max preserves finite values and aliases", "[native-shader]") {
    const bool maximum = GENERATE(false, true);
    const u32 destination = GENERATE(0U, 1U, 2U);
    const std::array<Native, 2> input{{
        {{(destination << 16) | 0x7209U, 1, 0x03820000U | (maximum ? 0x04000000U : 0U), 0}}, VertexTransform.back(),
    }};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    const std::array<float, 8> values{{0.0f, 1.0f, -1.0f, 123.5f, -23.5f,
        std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::quiet_NaN()}};
    for (float a : values) for (float b : values) {
        ScalarNativeMachine machine;
        machine.regs[0] = std::bit_cast<u32>(a); machine.regs[1] = std::bit_cast<u32>(b);
        machine.predicates = {true, false, true, false, true, false, true};
        const auto predicates = machine.predicates;
        machine.Execute(block);
        if (std::isnan(a) || std::isnan(b)) REQUIRE(std::isnan(std::bit_cast<float>(machine.regs[destination])));
        else REQUIRE(machine.regs[destination] == std::bit_cast<u32>(maximum ? std::fmax(a, b) : std::fmin(a, b)));
        REQUIRE(machine.predicates == predicates);
    }
}


TEST_CASE("Native predicate LUTs preserve inverted inputs and aliased outputs", "[native-shader]") {
    const u32 lut = GENERATE(2U, 0xefU);
    const u32 target = GENERATE(0U, 1U, 2U, 6U);
    const u32 constants = GENERATE(0U, 1U, 2U, 3U);
    for (u32 invert = 0; invert < 8; ++invert) {
        const u32 a = (constants == 1 ? 7U : 0U) | ((invert & 1) ? 8U : 0U);
        const u32 b = (constants == 2 ? 7U : 1U) | ((invert & 2) ? 8U : 0U);
        const u32 c = (constants == 3 ? 7U : 2U) | ((invert & 4) ? 8U : 0U);
        const u32 flags = (lut & 7) | ((lut >> 3) << 8) | (c << 4) | (b << 13) |
            (target << 17) | (7U << 20) | (a << 23);
        const std::array<Native, 2> input{{{{0x781c, 0, flags, 0}}, VertexTransform.back()}};
        Shader::ObjectPool<IR::Inst> pool;
        IR::Block block{pool};
        TranslateScalarNative(input, block);
        for (u32 values = 0; values < 8; ++values) {
            ScalarNativeMachine machine;
            machine.predicates = {bool(values & 1), bool(values & 2), bool(values & 4), true, false, true, false};
            auto expected = machine.predicates;
            const auto get = [&](u32 code) { return ((code & 7) == 7 ? true : machine.predicates[code & 7]) != bool(code & 8); };
            const u32 index = (static_cast<u32>(get(a)) << 2) | (static_cast<u32>(get(b)) << 1) | static_cast<u32>(get(c));
            expected[target] = ((lut >> index) & 1) != 0;
            machine.Execute(block);
            REQUIRE(machine.predicates == expected);
        }
    }
}


TEST_CASE("Native signed NTZ truncation preserves denormal control and aliases", "[native-shader]") {
    const bool ftz = GENERATE(false, true);
    const u32 destination = GENERATE(0U, 2U);
    const std::array<Native, 2> input{{
        {{(destination << 16) | 0x7305U, 0, 0x0020f100U | (ftz ? 0x10000U : 0U), 0}}, VertexTransform.back(),
    }};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    u32 rounds{};
    for (const auto& inst : block.Instructions()) if (inst.GetOpcode() == IR::Opcode::FPTrunc32) {
        REQUIRE(inst.Flags<IR::FpControl>().fmz_mode == (ftz ? IR::FmzMode::FTZ : IR::FmzMode::None));
        ++rounds;
    }
    REQUIRE(rounds == 1);
    for (float value : {0.0f, -0.0f, 0.9f, -0.9f, 1.9f, -1.9f, 12345.75f, -12345.75f,
                       std::numeric_limits<float>::denorm_min(), -std::numeric_limits<float>::denorm_min(),
                       std::numeric_limits<float>::quiet_NaN()}) {
        ScalarNativeMachine machine;
        machine.regs[0] = std::bit_cast<u32>(value);
        machine.Execute(block);
        REQUIRE(machine.regs[destination] == (std::isnan(value) ? 0U : static_cast<u32>(static_cast<s32>(value))));
    }
}


TEST_CASE("Native three-input uniform addition retains all operands and aliases", "[native-shader]") {
    const u32 destination = GENERATE(0U, 2U, 4U);
    const bool zero_third = GENERATE(false, true);
    const bool carry_output = GENERATE(false, true);
    const u32 flags = (0x0fffe000U & ~0xe0000U) | (carry_output ? 0U : 0xe0000U) | (zero_third ? 255U : 2U);
    const std::array<Native, 2> input{{
        {{(destination << 16) | 0x7c10U, 3, flags, 0}}, VertexTransform.back(),
    }};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u32 a : {0U, 1U, 0x80000000U, 0xffffffffU})
        for (u32 b : {0U, 1U, 0x12345678U, 0xffffffffU})
            for (u32 c : {0U, 1U, 0x87654321U, 0xffffffffU}) {
                ScalarNativeMachine machine;
                machine.regs[0] = a; machine.regs[219] = b; machine.regs[2] = c;
                machine.predicates = {true, false, true, false, true, false, true};
                auto predicates = machine.predicates;
                const u64 sum = u64{a} + b + (zero_third ? 0U : c);
                if (carry_output) predicates[0] = sum >= 0x100000000ULL;
                machine.Execute(block);
                REQUIRE(machine.regs[destination] == static_cast<u32>(sum));
                REQUIRE(machine.regs[219] == b);
                REQUIRE(machine.predicates == predicates);
            }
}


TEST_CASE("Native deferred-blocking barrier remains a full workgroup synchronization", "[native-shader]") {
    const u32 mode = GENERATE(0U, 0x10000U);
    const std::array<Native, 2> input{{{{0x7b1d, 0, mode, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    ScalarNativeMachine machine;
    machine.Execute(block);
    REQUIRE(machine.workgroup_barriers == 1);
    auto invalid = input;
    invalid[0].words[1] = 1;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(invalid));
    invalid = input;
    invalid[0].words[2] |= 1;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(invalid));
}


TEST_CASE("Native signed high multiplication preserves signs and input aliases", "[native-shader]") {
    const u32 destination = GENERATE(0U, 3U);
    const std::array<u32, 9> values{{0, 1, 9, 10, 0x66666667U, 0x7fffffffU, 0x80000000U, 0xffffffc0U, 0xffffffffU}};
    for (u32 b : values) {
        const std::array<Native, 2> input{{
            {{(destination << 16) | 0x7827U, b, 0x078e02ff, 0}}, VertexTransform.back(),
        }};
        Shader::ObjectPool<IR::Inst> pool;
        IR::Block block{pool};
        TranslateScalarNative(input, block);
        for (u32 a : values) {
            ScalarNativeMachine machine;
            machine.regs[0] = a;
            const s64 product = s64{static_cast<s32>(a)} * static_cast<s32>(b);
            machine.Execute(block);
            REQUIRE(machine.regs[destination] == static_cast<u32>(static_cast<u64>(product) >> 32));
        }
    }
}


TEST_CASE("Native min/max immediate preserves full signed and unsigned values", "[native-shader]") {
    const bool constant = GENERATE(false, true);
    const bool signed_compare = GENERATE(false, true);
    const bool maximum = GENERATE(false, true);
    const u32 destination = GENERATE(0U, 2U);
    const std::array<u32, 8> values{{0, 1, 0x7ffffU, 0x80000U, 0x7fffffffU, 0x80000000U, 0xffffffc0U, 0xffffffffU}};
    for (u32 b : values) {
        const std::array<Native, 2> input{{
            {{(destination << 16) | (constant ? 0x7a17U : 0x7817U), constant ? 0x00c04000U : b, 0x03800000U | (signed_compare ? 0x200U : 0U) |
                (maximum ? 0x04000000U : 0U), 0}},
            VertexTransform.back(),
        }};
        Shader::ObjectPool<IR::Inst> pool;
        IR::Block block{pool};
        TranslateScalarNative(input, block);
        for (u32 a : values) {
            ScalarNativeMachine machine;
            machine.regs[0] = a;
            machine.constants[(u64{3} << 32) | 0x100] = b;
            machine.predicates = {true, false, true, false, true, false, true};
            const auto predicates = machine.predicates;
            const bool less = signed_compare ? static_cast<s32>(a) < static_cast<s32>(b) : a < b;
            const u32 expected = (less != maximum) ? a : b;
            machine.Execute(block);
            REQUIRE(machine.regs[destination] == expected);
            REQUIRE(machine.predicates == predicates);
        }
    }
}


TEST_CASE("Native signed address carry and byte stores preserve pointer and width", "[native-shader]") {
    const u32 destination = GENERATE(0U, 2U);
    const bool constant_high = GENERATE(false, true);
    const std::array<Native, 4> input{{
        {{(6U << 24) | (destination << 16) | 0x7a10U, 0x6000, 0x07f1e0ff, 0}},
        {{(6U << 24) | ((destination + 1) << 16) | (constant_high ? 0x7a11U : 0x7211U), constant_high ? 0x6100U : 7U, 0x000f0eff, 0}},
        {{(destination << 24) | 0x7986U, 5, 0x0c101104, 0}},
        VertexTransform.back(),
    }};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u32 offset : {0U, 1U, 0x7fffffffU, 0x80000000U, 0xffffffffU})
        for (u64 base : {0ULL, 0xffffffffULL, 0x12345678ffffffffULL}) {
            ScalarNativeMachine machine;
            machine.regs[6] = offset;
            machine.regs[7] = static_cast<u32>(base >> 32);
            machine.regs[5] = 0x876543ab;
            machine.regs[220] = 0x1000; machine.regs[221] = 2;
            machine.constants[0x180] = static_cast<u32>(base);
            machine.constants[0x184] = static_cast<u32>(base >> 32);
            machine.Execute(block);
            const u64 expected = base + static_cast<u64>(static_cast<s64>(static_cast<s32>(offset)));
            REQUIRE(((u64{machine.regs[destination + 1]} << 32) | machine.regs[destination]) == expected);
            REQUIRE(machine.global.size() == 1);
            REQUIRE(machine.global.at(expected) == 0xab);
        }
}


TEST_CASE("Native constant-buffer logic and byte loads retain their values", "[native-shader]") {
    const std::array<Native, 3> input{{
        {{0x03027a12, 0x5a00, 0x078efcff, 0}},
        {{0xff057b82, 0x5e00, 0, 0}},
        VertexTransform.back(),
    }};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u32 a : {0U, 0xaaaaaaaaU, 0xffffffffU}) for (u32 b : {0U, 0x12345678U, 0xffffffffU}) {
        ScalarNativeMachine machine;
        machine.regs[3] = a;
        machine.constants[0x168] = b;
        machine.constants[0x178] = 0xffffffab;
        machine.Execute(block);
        REQUIRE(machine.regs[2] == (a | b));
        REQUIRE(machine.regs[5] == 0xab);
    }
}


TEST_CASE("Uniform native trap guards reject changed constants and unknown inputs", "[native-shader]") {
    std::array<Native, 5> input{{
        {{0xff037624, 0, 0x078e00ff, 0}},
        {{0x0300720c, 255, 0x03f06270, 0}},
        {{0x0947, 16, 0x03800000, 0}},
        {{0x795c, 4, 0x00300000, 0}},
        VertexTransform.back(),
    }};
    u32 constant{};
    const Shader::Ampere::TrapGuardReader read = [&](u32 bank, u32 offset) -> std::optional<u32> {
        REQUIRE(bank == 0); REQUIRE(offset == 0);
        return constant;
    };
    const auto prefix = std::span{input}.first(4);
    for (u32 value : {0U, 1U, 0x7fffffffU, 0x80000000U, 0xffffffffU}) {
        constant = value;
        REQUIRE(Shader::Ampere::VerifyTrapGuards(prefix, read) == (value < 0x80000000U));
    }
    REQUIRE_FALSE(Shader::Ampere::VerifyTrapGuards(prefix, {}));
    REQUIRE_FALSE(Shader::Ampere::VerifyTrapGuards(prefix, [](u32, u32) { return std::optional<u32>{}; }));
    constant = 0;
    auto unknown = input;
    unknown[0] = Native{{0xff037224, 255, 0x078e0004, 0}};
    REQUIRE_FALSE(Shader::Ampere::VerifyTrapGuards(std::span{unknown}.first(4), read));
    unknown = input;
    unknown[0] = Native{{0x00037919, 0, 0x2100, 0}};
    REQUIRE_FALSE(Shader::Ampere::VerifyTrapGuards(std::span{unknown}.first(4), read));
    unknown = input;
    unknown[2].words[1] = 0xfffffff0;
    REQUIRE_FALSE(Shader::Ampere::VerifyTrapGuards(std::span{unknown}.first(4), read));
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(input));
    const std::array<u32, 1> traps{48};
    REQUIRE(Shader::Ampere::LowerToMaxwell(input, traps));
}


TEST_CASE("Native arithmetic callees distinguish predicate destinations from return links", "[native-shader]") {
    const u32 link = GENERATE(0U, 3U, 8U);
    std::array<Native, 9> input{{
        {{0x7802U | (link << 16), 32, 0xf00, 0}},
        {{0x7944, 32, 0x03c00000, 0}},
        VertexTransform.back(),
        {{0x7918, 0, 0, 0}},
        {{0x720cU | (2U << 24), 4, 0x03f06070U, 0}},
        {{0x02067210, 4, 0x07ffe0ff, 0}},
        {{0x7802U | ((link + 1) << 16), 0, 0xf00, 0}},
        {{0x02067210, 4, 0x07ffe0ff, 0}},
        {{0x7950U | (link << 24), 0xffffff70, 0x03c3ffff, 0}},
    }};
    const auto result = Shader::Ampere::LowerToMaxwell(input);
    INFO(result.error << " offset=" << result.error_offset);
    REQUIRE(result);
    auto corrupt = input;
    corrupt[7].words[0] = (corrupt[7].words[0] & ~0xff0000U) | ((link + 1) << 16);
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(corrupt));
    corrupt = input;
    corrupt[5] = Native{{0x7210U | (link << 16) | (2U << 24), 4, 0x07ffe0ff, 0}};
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(corrupt));
    if (link > 0) {
        corrupt = input;
        corrupt[5] = Native{{0x7225U | ((link - 1) << 16) | (2U << 24), 4, 0x078e00ff, 0}};
        REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(corrupt));
    }
}


TEST_CASE("Native unsigned integer-to-float conversion honors nearest and upward rounding", "[native-shader]") {
    const bool upward = GENERATE(false, true);
    const u32 destination = GENERATE(2U, 3U, 6U);
    const std::array<Native, 2> input{{
        {{(destination << 16) | 0x7312U, 2, upward ? 0x00309000U : 0x00301000U, 0}},
        VertexTransform.back(),
    }};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    std::vector<u64> inputs{0, 1, 2, 3, 0xffffffffULL, 0x100000000ULL, ~u64{0}};
    for (u32 bit = 1; bit < 64; ++bit) {
        const u64 power = u64{1} << bit;
        inputs.insert(inputs.end(), {power - 1, power, power + 1});
        if (bit >= 24) {
            const u64 half = u64{1} << (bit - 24);
            inputs.insert(inputs.end(), {power + half - 1, power + half, power + half + 1,
                power + 3 * half - 1, power + 3 * half, power + 3 * half + 1});
        }
    }
    u64 random = 0x98479a67b545ff21ULL;
    for (u32 i = 0; i < 1024; ++i) {
        random ^= random << 13; random ^= random >> 7; random ^= random << 17;
        inputs.push_back(random);
    }
    for (u64 source : inputs) {
        float expected = static_cast<float>(source);
        if (upward && static_cast<double>(expected) < 0x1p64 && static_cast<u64>(expected) < source)
            expected = std::nextafter(expected, std::numeric_limits<float>::infinity());
        ScalarNativeMachine machine;
        machine.regs[2] = static_cast<u32>(source);
        machine.regs[3] = static_cast<u32>(source >> 32);
        machine.predicates = {true, false, true, false, true, false, true};
        const auto predicates = machine.predicates;
        machine.Execute(block);
        REQUIRE(machine.regs[destination] == std::bit_cast<u32>(expected));
        REQUIRE(machine.predicates == predicates);
    }
}


TEST_CASE("Native wide and high multiply-add retain carries and aliased inputs", "[native-shader]") {
    const bool wide = GENERATE(false, true);
    const bool signed_product = GENERATE(false, true);
    const u32 destination = GENERATE(0U, 2U, 4U, 8U);
    const u32 output = GENERATE(0U, 6U, 7U);
    const std::array<Native, 2> input{{
        {{(destination << 16) | (wide ? 0x7225U : 0x7227U), 2, 0x07800004U | (output << 17) | (signed_product ? 0x200U : 0U), 0}},
        VertexTransform.back(),
    }};
    if (signed_product && output != 7) {
        REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(input));
        return;
    }
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    const std::array<u32, 8> values{{0, 1, 0xffffU, 0x10000U, 0x7fffffffU, 0x80000000U, 0xfffffffeU, 0xffffffffU}};
    for (u32 a : values) for (u32 b : values) for (u32 c : values) {
        ScalarNativeMachine machine;
        machine.regs[0] = a;
        machine.regs[2] = b;
        machine.regs[4] = c;
        machine.regs[5] = b;
        machine.predicates = {true, false, true, false, true, false, true};
        auto predicates = machine.predicates;
        const u64 product = signed_product ? static_cast<u64>(s64{static_cast<s32>(a)} * static_cast<s32>(b)) : u64{a} * b;
        const u64 addend = (u64{b} << 32) | c;
        const u64 sum = product + addend;
        const u64 expected = wide ? sum : sum >> 32;
        const bool carry = sum < product;
        if (output != 7) predicates[output] = carry;
        machine.Execute(block);
        REQUIRE(machine.regs[destination] == static_cast<u32>(expected));
        if (wide) REQUIRE(machine.regs[destination + 1] == static_cast<u32>(expected >> 32));
        REQUIRE(machine.predicates == predicates);
    }
}


TEST_CASE("Native subtraction carry chains retain the full unsigned 64-bit difference", "[native-shader]") {
    const u32 destination = GENERATE(0U, 2U, 4U);
    const u32 extended = (0x07ffe0ffU & ~((15U << 23) | (15U << 13))) | (1U << 10) | (15U << 13);
    const std::array<Native, 3> input{{
        {{(destination << 16) | 0x7210U, 0x80000002, 0x07f1e0ff, 0}},
        {{(1U << 24) | ((destination + 1) << 16) | 0x7210U, 0x80000003, extended, 0}},
        VertexTransform.back(),
    }};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    const std::array<u64, 8> values{{0, 1, 0xffffffffULL, 0x100000000ULL,
        0x7fffffffffffffffULL, 0x8000000000000000ULL, 0xffffffffffffffffULL, 0x1234567887654321ULL}};
    for (u64 a : values) for (u64 b : values) {
        ScalarNativeMachine machine;
        machine.regs[0] = static_cast<u32>(a); machine.regs[1] = static_cast<u32>(a >> 32);
        machine.regs[2] = static_cast<u32>(b); machine.regs[3] = static_cast<u32>(b >> 32);
        machine.predicates = {true, false, true, false, true, false, true};
        auto predicates = machine.predicates;
        predicates[0] = static_cast<u32>(a) >= static_cast<u32>(b);
        machine.Execute(block);
        REQUIRE((u64{machine.regs[destination + 1]} << 32 | machine.regs[destination]) == a - b);
        REQUIRE(machine.predicates == predicates);
    }
}


TEST_CASE("Native low carry and zero-product high add preserve 64-bit sums", "[native-shader]") {
    const u32 destination = GENERATE(0U, 2U, 4U, 255U);
    const bool immediate = GENERATE(false, true);
    const bool high_overlap = GENERATE(false, true);
    const u32 high_destination = high_overlap ? 1U : 6U;
    const std::array<u32, 5> values{{0, 1, 0x7fffffffU, 0x80000000U, 0xffffffffU}};
    for (u32 b : values) {
        const std::array<Native, 3> input{{
            {{(destination << 16) | (immediate ? 0x7810U : 0x7210U), immediate ? b : 2U, 0x07f3e0ff, 0}},
            {{0xff007224U | (high_destination << 16), 255, 0x008e0601, 0}},
            VertexTransform.back(),
        }};
        Shader::ObjectPool<IR::Inst> pool;
        IR::Block block{pool};
        TranslateScalarNative(input, block);
        for (u32 a : values) for (u32 high : values) {
            const u64 sum = u64{a} + b;
            ScalarNativeMachine machine;
            machine.regs[0] = a;
            machine.regs[1] = high;
            machine.regs[2] = b;
            machine.predicates = {true, false, true, false, true, false, true};
            auto expected = machine.predicates;
            expected[1] = (sum >> 32) != 0;
            machine.Execute(block);
            if (destination != 255) REQUIRE(machine.regs[destination] == static_cast<u32>(sum));
            REQUIRE(machine.regs[high_destination] == static_cast<u32>(u64{high} + (sum >> 32)));
            REQUIRE(machine.predicates == expected);
        }
    }
}


TEST_CASE("Native zero-product carry additions retain predicate polarity", "[native-shader]") {
    const u32 carry_predicate = GENERATE(0U, 1U, 6U, 7U);
    const bool negate = GENERATE(false, true);
    const bool complement = GENERATE(false, true);
    const std::array<Native, 2> input{{
        {{0xff067224, 255, 0x000e0601U | (complement ? 0x800U : 0U) | ((carry_predicate | (negate ? 8U : 0U)) << 23), 0}},
        VertexTransform.back(),
    }};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u32 value : {0U, 1U, 0xffffffffU}) for (bool carry : {false, true}) {
        ScalarNativeMachine machine;
        machine.regs[1] = value;
        machine.predicates.fill(carry);
        const auto predicates = machine.predicates;
        machine.Execute(block);
        const bool active = (carry_predicate == 7 ? true : carry) != negate;
        REQUIRE(machine.regs[6] == (complement ? ~value : value) + static_cast<u32>(active));
        REQUIRE(machine.regs[1] == value);
        REQUIRE(machine.predicates == predicates);
    }
}


TEST_CASE("Native unit-multiplier extended addition preserves aliases and carry polarity", "[native-shader]") {
    const bool complement = GENERATE(false, true);
    const u32 destination = GENERATE(0U, 2U, 6U);
    const u32 carry_predicate = GENERATE(0U, 6U, 7U, 15U);
    const std::array<Native, 2> input{{
        {{(destination << 16) | 0x7824U, 1, 0x000e0602U | (complement ? 0x800U : 0U) | (carry_predicate << 23), 0}},
        VertexTransform.back(),
    }};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u32 a : {0U, 1U, 0x80000000U, 0xffffffffU})
        for (u32 b : {0U, 1U, 0x80000000U, 0xffffffffU}) for (bool active : {false, true}) {
            ScalarNativeMachine machine;
            machine.regs[0] = a; machine.regs[2] = b;
            machine.predicates.fill(active);
            const auto predicates = machine.predicates;
            const bool carry = ((carry_predicate & 7) == 7 ? true : active) != bool(carry_predicate & 8);
            machine.Execute(block);
            REQUIRE(machine.regs[destination] == a + (complement ? ~b : b) + static_cast<u32>(carry));
            REQUIRE(machine.predicates == predicates);
        }
}


TEST_CASE("Native ordered extended comparisons preserve low-word decisions and predicates", "[native-shader]") {
    const u32 comparison = GENERATE(1U, 3U, 4U, 6U);
    const u32 output = GENERATE(0U, 6U);
    const bool alias = GENERATE(false, true);
    const bool negate_low = GENERATE(false, true);
    const u32 low_input = alias ? output : 3U;
    const std::array<Native, 2> input{{
        {{0x0100720c, 2, 0x03f00100U | (comparison << 12) | (output << 17) |
            ((low_input | (negate_low ? 8U : 0U)) << 4), 0}},
        VertexTransform.back(),
    }};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    const auto compare = [&](u64 a, u64 b) {
        if (comparison == 1) return a < b;
        if (comparison == 3) return a <= b;
        if (comparison == 4) return a > b;
        return a >= b;
    };
    const std::array<u64, 8> values{{0, 1, 0xffffffffULL, 0x100000000ULL,
        0x100000001ULL, 0x7fffffff00000000ULL, 0x8000000000000000ULL, ~u64{0}}};
    for (u64 a : values) for (u64 b : values) {
        ScalarNativeMachine machine;
        machine.regs[1] = static_cast<u32>(a >> 32);
        machine.regs[2] = static_cast<u32>(b >> 32);
        machine.predicates = {true, false, true, false, true, false, true};
        machine.predicates[low_input] = compare(static_cast<u32>(a), static_cast<u32>(b)) != negate_low;
        auto expected = machine.predicates;
        expected[output] = compare(a, b);
        machine.Execute(block);
        REQUIRE(machine.predicates == expected);
        REQUIRE(machine.regs[1] == static_cast<u32>(a >> 32));
        REQUIRE(machine.regs[2] == static_cast<u32>(b >> 32));
    }
}


TEST_CASE("Native ordered extended comparisons retain execution guards", "[native-shader]") {
    const u32 execution = GENERATE(0U, 7U, 8U);
    const std::array<Native, 2> input{{
        {{(1U << 24) | (execution << 12) | 0x20cU, 2, 0x03f01100U, 0}},
        VertexTransform.back(),
    }};
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(lowered);
    REQUIRE(lowered.instructions.size() > 2);
    for (size_t i = 0; i + 1 < lowered.instructions.size(); ++i) {
        const auto predicate = Shader::Maxwell::Instruction{lowered.instructions[i]}.Pred();
        REQUIRE(predicate.index == (execution & 7));
        REQUIRE(predicate.negated == ((execution & 8) != 0));
    }
    for (u32 field : {0x200U, 0x400U, 0x800000U}) {
        auto invalid = input;
        invalid[0].words[2] ^= field;
        REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(invalid));
    }
}


TEST_CASE("Branch-edge facts preserve loop predicate definitions", "[native-shader]") {
    const bool initial = GENERATE(false, true);
    const bool reset = GENERATE(false, true);
    const bool describe_branch = GENERATE(false, true);
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block entry{pool}, header{pool}, body{pool}, tail{pool};
    entry.AddBranch(&header);
    header.AddBranch(&body);
    header.AddBranch(&tail);
    body.AddBranch(&tail);
    tail.AddBranch(&header);
    const auto flag = header.PrependNewInst(header.begin(), IR::Opcode::Phi);
    const auto next = tail.PrependNewInst(tail.begin(), IR::Opcode::Phi);
    flag->SetFlags(IR::Type::U1);
    next->SetFlags(IR::Type::U1);
    flag->AddPhiOperand(&entry, IR::Value{initial});
    flag->AddPhiOperand(&tail, IR::Value{&*next});
    next->AddPhiOperand(&header, IR::Value{&*flag});
    next->AddPhiOperand(&body, IR::Value{reset});
    const auto numeric = header.PrependNewInst(header.end(), IR::Opcode::Phi);
    numeric->SetFlags(IR::Type::U32);
    numeric->AddPhiOperand(&entry, IR::Value{17U});
    numeric->AddPhiOperand(&tail, IR::Value{23U});
    IR::IREmitter ir{header};
    const auto condition = ir.ConditionRef(ir.LogicalOr(IR::U1{IR::Value{&*flag}}, ir.GetPred(IR::Pred::P0)));
    IR::Program program;
    program.blocks = {&entry, &header, &body, &tail};
    if (describe_branch) {
        IR::AbstractSyntaxNode node;
        node.type = IR::AbstractSyntaxNode::Type::If;
        node.data.if_node = {condition, &body, &tail};
        program.syntax_list.push_back(node);
    }
    const auto folded = Shader::Optimization::ConditionalPhi::Fold(program);
    bool after_phi{};
    for (const auto& inst : header.Instructions()) {
        if (inst.GetOpcode() != IR::Opcode::Phi) after_phi = true;
        else REQUIRE_FALSE(after_phi);
    }
    REQUIRE(numeric->GetOpcode() == IR::Opcode::Phi);
    const auto first = IR::Value{&*flag}.Resolve();
    const auto second = IR::Value{&*next}.Resolve();
    if (!describe_branch && initial == reset) {
        REQUIRE(first.IsImmediate());
        REQUIRE(second.IsImmediate());
        REQUIRE(first.U1() == initial);
        REQUIRE(second.U1() == initial);
        REQUIRE(folded == 2);
    } else if (describe_branch && !reset) {
        REQUIRE(second.IsImmediate());
        REQUIRE_FALSE(second.U1());
        if (!initial) {
            REQUIRE(first.IsImmediate());
            REQUIRE_FALSE(first.U1());
            REQUIRE(folded == 2);
        } else {
            REQUIRE_FALSE(first.IsImmediate());
            REQUIRE(folded == 1);
        }
    } else {
        REQUIRE_FALSE(first.IsImmediate());
        REQUIRE_FALSE(second.IsImmediate());
        REQUIRE(folded == 0);
    }
}


TEST_CASE("Boolean edge implications do not assume ambiguous operands", "[native-shader]") {
    const bool conjunction = GENERATE(false, true);
    const bool taken = GENERATE(false, true);
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool}, yes{pool}, no{pool};
    block.AddBranch(&yes);
    block.AddBranch(&no);
    IR::IREmitter ir{block};
    const auto p = ir.GetPred(IR::Pred::P0), q = ir.GetPred(IR::Pred::P1);
    const auto condition = ir.ConditionRef(conjunction ? ir.LogicalAnd(p, ir.LogicalNot(q)) : ir.LogicalOr(p, ir.LogicalNot(q)));
    const Shader::Optimization::ConditionalPhi::Branch branch{condition, &yes, &no};
    const auto facts = Shader::Optimization::ConditionalPhi::EdgeFacts(branch, taken ? &yes : &no);
    if (conjunction == taken) {
        REQUIRE(facts.at(p.InstRecursive()) == taken);
        REQUIRE(facts.at(q.InstRecursive()) == !taken);
    } else {
        REQUIRE_FALSE(facts.contains(p.InstRecursive()));
        REQUIRE_FALSE(facts.contains(q.InstRecursive()));
    }
}


TEST_CASE("Unanchored boolean Phi cycles do not become constants", "[native-shader]") {
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    block.AddBranch(&block);
    const auto phi = block.PrependNewInst(block.begin(), IR::Opcode::Phi);
    phi->SetFlags(IR::Type::U1);
    phi->AddPhiOperand(&block, IR::Value{&*phi});
    IR::Program program;
    program.blocks = {&block};
    REQUIRE(Shader::Optimization::ConditionalPhi::Fold(program) == 0);
    REQUIRE_FALSE(IR::Value{&*phi}.Resolve().IsImmediate());
}


TEST_CASE("Texture Phi specialization requires an infeasible incoming edge", "[native-shader]") {
    const u32 mode = GENERATE(0U, 1U, 2U, 3U, 4U, 5U, 6U);
    class RootEnvironment final : public EmptyEnvironment {
    public:
        u32 ReadCbufValue(u32 bank, u32 offset) override {
            REQUIRE(bank == 0);
            if (offset == 0x518) return 40960;
            REQUIRE(offset == 0x51c);
            return 0;
        }
    } env;
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block entry{pool}, root{pool}, clobber{pool}, merge{pool}, sample{pool}, exit{pool};
    entry.AddBranch(&root);
    if (mode == 4) entry.AddBranch(&sample);
    root.AddBranch(&clobber);
    root.AddBranch(&merge);
    clobber.AddBranch(&merge);
    merge.AddBranch(&sample);
    merge.AddBranch(&exit);
    sample.AddBranch(&exit);
    if (mode == 3) exit.AddBranch(&clobber);
    IR::IREmitter ir{root};
    const auto index = ir.GetReg(IR::Reg::R1);
    const auto pointer = ir.GetCbuf(ir.Imm32(0U), ir.Imm32(0x510U));
    const auto relative = ir.IAdd(ir.IMul(index, ir.Imm32(160U)), ir.Imm32(152U));
    const auto offset = ir.ISub(ir.IAdd(pointer, relative), ir.BitwiseAnd(pointer, ir.Imm32(0xfffffff0U)));
    const auto load = root.PrependNewInst(root.end(), IR::Opcode::LoadStorage64, {ir.Imm32(0U), offset});
    const IR::U32 handle{ir.CompositeExtract(IR::Value{&*load}, 0)};
    const auto selector = ir.GetReg(IR::Reg::R2);
    const auto first_condition = ir.ConditionRef(ir.IEqual(selector, ir.Imm32(1U)));
    IR::IREmitter bad_ir{clobber};
    const IR::U32 invalid = mode == 5 ? bad_ir.Imm32(0U) : bad_ir.BitCast<IR::U32>(IR::F32{bad_ir.FPAdd(bad_ir.Imm32(0.1f), bad_ir.Imm32(0.2f))});
    const auto phi = merge.PrependNewInst(merge.begin(), IR::Opcode::Phi);
    phi->SetFlags(IR::Type::U32);
    phi->AddPhiOperand(&root, handle);
    phi->AddPhiOperand(&clobber, invalid);
    IR::IREmitter merge_ir{merge};
    IR::U32 second_selector = mode == 2 ? merge_ir.GetReg(IR::Reg::R3) : selector;
    if (mode == 6) {
        const auto selected = merge.PrependNewInst(merge.begin(), IR::Opcode::Phi);
        selected->SetFlags(IR::Type::U32);
        selected->AddPhiOperand(&root, selector);
        selected->AddPhiOperand(&clobber, selector);
        second_selector = IR::U32{IR::Value{&*selected}};
    }
    const auto second_condition = merge_ir.ConditionRef(merge_ir.IEqual(second_selector, merge_ir.Imm32(mode == 1 ? 1U : 2U)));
    IR::IREmitter sample_ir{sample};
    IR::TextureInstInfo flags{};
    flags.type.Assign(Shader::TextureType::Color2D);
    (void)sample_ir.ImageSampleExplicitLod(IR::U32{IR::Value{&*phi}},
        sample_ir.CompositeConstruct(sample_ir.Imm32(0.0f), sample_ir.Imm32(1.0f)), sample_ir.Imm32(0.0f), {}, flags);
    IR::Program program;
    program.stage = Shader::Stage::Fragment;
    program.blocks = {&entry, &root, &clobber, &merge, &sample, &exit};
    program.post_order_blocks = {&exit, &sample, &merge, &clobber, &root, &entry};
    program.info.storage_buffers_descriptors.push_back({0, 0x510, 1, false});
    IR::AbstractSyntaxNode first, second;
    first.type = second.type = IR::AbstractSyntaxNode::Type::If;
    first.data.if_node = {first_condition, &clobber, &merge};
    second.data.if_node = {second_condition, &sample, &exit};
    program.syntax_list = {first, second};
    Shader::HostTranslateInfo host;
    host.ApplyDescriptorLimitPolicy();
    host.support_buffer_texture_handles = true;
    if (mode == 0 || mode == 5 || mode == 6) {
        REQUIRE_NOTHROW(Shader::Optimization::TexturePass(env, program, host));
        REQUIRE(program.info.texture_descriptors.size() == 1);
        for (const auto& inst : sample.Instructions()) if (inst.GetOpcode() == IR::Opcode::ImageSampleExplicitLod)
            REQUIRE(inst.Arg(0).Resolve() == index.Resolve());
    } else {
        REQUIRE_THROWS_AS(Shader::Optimization::TexturePass(env, program, host), Shader::NotImplementedException);
    }
}


TEST_CASE("A repeated buffer handle keeps its original index through Phi", "[native-shader]") {
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block root{pool}, left{pool}, right{pool}, merge{pool}, sample{pool}, exit{pool};
    root.AddBranch(&left);
    root.AddBranch(&right);
    left.AddBranch(&merge);
    right.AddBranch(&merge);
    merge.AddBranch(&sample);
    merge.AddBranch(&exit);
    sample.AddBranch(&exit);
    IR::IREmitter ir{root};
    const auto index = ir.GetReg(IR::Reg::R1);
    const auto pointer = ir.GetCbuf(ir.Imm32(0U), ir.Imm32(0x510U));
    const auto relative = ir.IAdd(ir.IMul(index, ir.Imm32(160U)), ir.Imm32(152U));
    const auto offset = ir.ISub(ir.IAdd(pointer, relative), ir.BitwiseAnd(pointer, ir.Imm32(0xfffffff0U)));
    const auto load = root.PrependNewInst(root.end(), IR::Opcode::LoadStorage64, {ir.Imm32(0U), offset});
    const IR::U32 low{ir.CompositeExtract(IR::Value{&*load}, 0)};
    const auto first_condition = ir.ConditionRef(ir.GetPred(IR::Pred::P0));
    const auto phi = merge.PrependNewInst(merge.begin(), IR::Opcode::Phi);
    phi->SetFlags(IR::Type::U32);
    phi->AddPhiOperand(&left, low);
    phi->AddPhiOperand(&right, low);
    IR::IREmitter merge_ir{merge};
    const auto second_condition = merge_ir.ConditionRef(merge_ir.INotEqual(IR::U32{IR::Value{&*phi}}, merge_ir.Imm32(0U)));
    IR::Program program;
    program.blocks = {&root, &left, &right, &merge, &sample, &exit};
    program.info.storage_buffers_descriptors.push_back({0, 0x510, 1, false});
    IR::AbstractSyntaxNode first, second;
    first.type = second.type = IR::AbstractSyntaxNode::Type::If;
    first.data.if_node = {first_condition, &left, &right};
    second.data.if_node = {second_condition, &sample, &exit};
    program.syntax_list = {first, second};
    Shader::Optimization::BufferTexture::Tracker tracker{program};
    const auto source = tracker.FindForUse(IR::Value{&*phi}, &sample);
    REQUIRE(source);
    REQUIRE(source->index.Resolve() == index.Resolve());
}


TEST_CASE("Buffer texture descriptors retain field and query identity", "[native-shader]") {
    const bool different_field = GENERATE(false, true);
    class RootEnvironment final : public EmptyEnvironment {
    public:
        u32 ReadCbufValue(u32 bank, u32 offset) override {
            REQUIRE(bank == 0);
            if (offset == 0x518) return 40960;
            REQUIRE(offset == 0x51c);
            return 0;
        }
    } env;
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block root{pool}, sample{pool}, exit{pool};
    root.AddBranch(&sample);
    root.AddBranch(&exit);
    sample.AddBranch(&exit);
    IR::IREmitter ir{root};
    const auto index = ir.GetReg(IR::Reg::R1);
    const auto pointer = ir.GetCbuf(ir.Imm32(0U), ir.Imm32(0x510U));
    const auto make_handle = [&](u32 field) {
        const auto relative = ir.IAdd(ir.IMul(index, ir.Imm32(160U)), ir.Imm32(field));
        const auto offset = ir.ISub(ir.IAdd(pointer, relative), ir.BitwiseAnd(pointer, ir.Imm32(0xfffffff0U)));
        const auto load = root.PrependNewInst(root.end(), IR::Opcode::LoadStorage64, {ir.Imm32(0U), offset});
        return IR::U32{ir.CompositeExtract(IR::Value{&*load}, 0)};
    };
    const auto first = make_handle(152);
    const auto second = different_field ? make_handle(144) : first;
    IR::TextureInstInfo flags{};
    flags.type.Assign(Shader::TextureType::Color2D);
    (void)ir.ImageSampleExplicitLod(first,
        ir.CompositeConstruct(ir.Imm32(0.0f), ir.Imm32(1.0f)), ir.Imm32(0.0f), {}, flags);
    const auto dimensions = ir.ImageQueryDimension(first, ir.Imm32(0U), ir.Imm1(true));
    ir.SetReg(IR::Reg::R5, IR::U32{ir.CompositeExtract(dimensions, 0)});
    const auto condition = ir.ConditionRef(ir.INotEqual(second, ir.Imm32(0U)));
    IR::IREmitter sample_ir{sample};
    (void)sample_ir.ImageSampleExplicitLod(second,
        sample_ir.CompositeConstruct(sample_ir.Imm32(0.0f), sample_ir.Imm32(1.0f)), sample_ir.Imm32(0.0f), {}, flags);
    IR::Program program;
    program.stage = Shader::Stage::Fragment;
    program.blocks = {&root, &sample, &exit};
    program.post_order_blocks = {&exit, &sample, &root};
    program.info.storage_buffers_descriptors.push_back({0, 0x510, 1, false});
    IR::AbstractSyntaxNode branch;
    branch.type = IR::AbstractSyntaxNode::Type::If;
    branch.data.if_node = {condition, &sample, &exit};
    program.syntax_list = {branch};
    Shader::HostTranslateInfo host;
    host.ApplyDescriptorLimitPolicy();
    host.support_buffer_texture_handles = true;
    REQUIRE_NOTHROW(Shader::Optimization::TexturePass(env, program, host));
    REQUIRE(program.info.texture_descriptors.size() == (different_field ? 3U : 2U));
    u32 primary{}, secondary{}, queries{};
    for (const auto& descriptor : program.info.texture_descriptors) {
        const auto& source = descriptor.buffer_source;
        if (!different_field && !source.width_query)
            REQUIRE(std::ranges::none_of(source.exclusions, [](const auto& guard) { return guard.mask != 0; }));
        if (source.width_query) { ++queries; REQUIRE(source.offset == 152); }
        else if (source.offset == 144) ++secondary;
        else { ++primary; REQUIRE(source.offset == 152); }
    }
    REQUIRE(primary == 1);
    REQUIRE(secondary == (different_field ? 1U : 0U));
    REQUIRE(queries == 1);
}


TEST_CASE("Native immediate high-word shifts form wrapped bit masks", "[native-shader]") {
    const u32 value = GENERATE(1U, 0x87654321U, 0xffffffffU);
    const std::array<Native, 2> input{{
        {{0xff057419, value, 0x00010e03, 0}}, VertexTransform.back(),
    }};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u32 shift : {0U, 1U, 10U, 31U, 32U, 33U, 63U, 0xffffffffU}) {
        ScalarNativeMachine machine;
        machine.regs[3] = shift;
        machine.Execute(block);
        REQUIRE(machine.regs[5] == (value << (shift & 31)));
        REQUIRE(machine.regs[3] == shift);
    }
}


TEST_CASE("Native uniform right shifts preserve signedness bounds and aliases", "[native-shader]") {
    const bool signed_shift = GENERATE(false, true);
    const u32 distance = GENERATE(0U, 2U, 31U, 32U);
    const u32 destination = GENERATE(4U, 5U);
    const std::array<Native, 2> input{{
        {{0x3f007899U | (destination << 16), distance, signed_shift ? 0x08011404U : 0x08011604U, 0}},
        VertexTransform.back(),
    }};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u32 value : {0U, 1U, 0x7fffffffU, 0x80000000U, 0xffffffffU}) {
        ScalarNativeMachine machine;
        machine.regs[220] = value;
        machine.Execute(block);
        const u32 expected = signed_shift ? static_cast<u32>(static_cast<s32>(value) >> std::min(distance, 31U)) :
            distance == 32 ? 0U : value >> distance;
        REQUIRE(machine.regs[216 + destination] == expected);
    }
}


TEST_CASE("Native extended uniform registers preserve independent carry state", "[native-shader]") {
    const auto ur = [](u32 index) { return 216U - (index / 16) * 16 + index % 16; };
    const std::array<Native, 5> input{{
        {{(4U << 24) | (16U << 16) | 0x7290U, 6, 0x0ff1e03f, 0}},
        {{(5U << 24) | (18U << 16) | 0x7290U, 7, 0x087fe43f, 0}},
        {{0x00007c02, 16, 0x08000f00, 0}},
        {{0x00017c02, 18, 0x08000f00, 0}},
        VertexTransform.back(),
    }};
    const std::array<u64, 6> values{{0, 1, 0xffffffffULL, 0x100000001ULL, 0x7fffffffffffffffULL, ~u64{0}}};
    for (u64 a : values) for (u64 b : values) for (u32 pattern : {0U, 0x15U, 0x7fU}) {
        ScalarNativeMachine machine;
        machine.regs[ur(4)] = static_cast<u32>(a);
        machine.regs[ur(5)] = static_cast<u32>(a >> 32);
        machine.regs[ur(6)] = static_cast<u32>(b);
        machine.regs[ur(7)] = static_cast<u32>(b >> 32);
        machine.regs[239] = 0x12345600U | pattern;
        machine.predicates = {true, false, true, false, false, true, false};
        const auto predicates = machine.predicates;
        ExecutePredicatedScalarNative(input, machine);
        REQUIRE(machine.regs[0] == static_cast<u32>(a + b));
        REQUIRE(machine.regs[1] == static_cast<u32>((a + b) >> 32));
        REQUIRE(machine.regs[ur(16)] == machine.regs[0]);
        REQUIRE(machine.regs[ur(18)] == machine.regs[1]);
        const bool carry = u64{static_cast<u32>(a)} + static_cast<u32>(b) >= 0x100000000ULL;
        REQUIRE(machine.regs[239] == ((0x12345600U | pattern) & ~0x40U) + (carry ? 0x40U : 0U));
        REQUIRE(machine.predicates == predicates);
    }
}


TEST_CASE("Native uniform register mapping covers each reserved bank", "[native-shader]") {
    const u32 target = GENERATE(0U, 14U, 16U, 18U, 30U, 32U, 46U, 48U, 60U, 62U);
    const u32 mapped = 216U - (target / 16) * 16 + target % 16;
    const std::array<Native, 3> input{{
        {{(4U << 24) | (target << 16) | 0x78a4U, 5, 0x0f8e023f, 0}},
        {{0x000a7c02, target, 0x08000f00, 0}},
        VertexTransform.back(),
    }};
    ScalarNativeMachine machine;
    machine.regs[220] = 0x12345678U;
    machine.predicates = {true, false, true, false, true, false, true};
    const auto predicates = machine.predicates;
    ExecutePredicatedScalarNative(input, machine);
    REQUIRE(machine.regs[mapped] == 0x12345678U * 5U);
    REQUIRE(machine.regs[10] == machine.regs[mapped]);
    REQUIRE(machine.predicates == predicates);
}


TEST_CASE("Native uniform constant pairs remain contiguous at bank boundaries", "[native-shader]") {
    const u32 target = GENERATE(14U, 16U, 30U, 32U, 46U, 48U, 60U);
    const auto ur = [](u32 index) { return 216U - (index / 16) * 16 + index % 16; };
    const std::array<Native, 2> input{{
        {{(target << 16) | 0x7ab9U, 0x00c04000, 0xa00, 0}},
        VertexTransform.back(),
    }};
    ScalarNativeMachine machine;
    for (u32 index = 0; index < 63; ++index) machine.regs[ur(index)] = 0xc0000000U + index;
    machine.regs[239] = 0x15;
    machine.predicates = {true, false, true, true, false, false, true};
    const auto predicates = machine.predicates;
    machine.constants[(u64{3} << 32) | 0x100] = 0x12345678;
    machine.constants[(u64{3} << 32) | 0x104] = 0x87654321;
    ExecutePredicatedScalarNative(input, machine);
    for (u32 index = 0; index < 63; ++index)
        REQUIRE(machine.regs[ur(index)] == (index == target ? 0x12345678U : index == target + 1 ? 0x87654321U : 0xc0000000U + index));
    REQUIRE(machine.regs[239] == 0x15);
    REQUIRE(machine.predicates == predicates);
}


TEST_CASE("Native extended uniform bank rejects overlapping general registers", "[native-shader]") {
    const u32 uniform = GENERATE(4U, 16U);
    const std::array<Native, 3> input{{
        {{0x00c87802, 1, 0xf00, 0}},
        {{(4U << 24) | (uniform << 16) | 0x78a4U, 5, 0x0f8e023f, 0}},
        VertexTransform.back(),
    }};
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(static_cast<bool>(lowered) == (uniform == 4));
}


TEST_CASE("Native shared vector zero stores preserve their address and predicates", "[native-shader]") {
    const u32 width = GENERATE(5U, 6U);
    const u32 stride = GENERATE(0U, 1U, 3U);
    const bool active = GENERATE(false, true);
    const std::array<Native, 2> input{{
        {{(4U << 24) | 0x0388U, 0x000020ff, (width << 9) | (stride << 14), 0}},
        VertexTransform.back(),
    }};
    ScalarNativeMachine machine;
    machine.regs[4] = 16;
    machine.predicates = {active, true, false, true, false, false, true};
    const auto predicates = machine.predicates;
    const u32 address = (stride ? 16U << (stride + 1) : 16U) + 32;
    const u32 count = width == 5 ? 2 : 4;
    for (u32 offset = 0; offset < count + 2; ++offset) machine.shared[address - 4 + offset * 4] = 0xdeadbeef;
    ExecutePredicatedScalarNative(input, machine);
    for (u32 offset = 0; offset < count + 2; ++offset)
        REQUIRE(machine.shared.at(address - 4 + offset * 4) == (active && offset > 0 && offset <= count ? 0U : 0xdeadbeefU));
    REQUIRE(machine.regs[4] == 16);
    REQUIRE(machine.predicates == predicates);
}


TEST_CASE("Native conditional half ReLU keeps unclamped results when disabled", "[native-shader]") {
    const u32 condition = GENERATE(0U, 1U, 6U, 7U, 8U, 9U, 14U, 15U);
    const u32 destination = GENERATE(4U, 8U, 11U);
    const std::array<Native, 2> input{{
        {{(8U << 24) | (destination << 16) | 0x7831U, 0x3c003c00, 0x800bU | (condition << 23), 0}},
        VertexTransform.back(),
    }};
    for (bool enabled : {false, true}) for (u32 value : {0xbc003c00U, 0x3c00bc00U, 0xc000c000U, 0x7c00fc00U}) {
        ScalarNativeMachine machine;
        machine.regs[8] = value;
        machine.regs[11] = 0x3c003c00;
        machine.predicates = {true, false, true, false, false, true, false};
        if ((condition & 7) != 7) machine.predicates[condition & 7] = enabled;
        const auto predicates = machine.predicates;
        const bool clamp = ((condition & 7) == 7 ? true : enabled) != bool(condition & 8);
        u32 expected = 0;
        for (u32 half = 0; half < 2; ++half) {
            double result = DecodeTestHalf(static_cast<u16>(value >> (half * 16))) + 1.0;
            if (clamp) result = std::max(result, 0.0);
            expected |= u32{EncodeTestHalf(result)} << (half * 16);
        }
        ExecutePredicatedScalarNative(input, machine);
        REQUIRE(machine.regs[destination] == expected);
        REQUIRE(machine.predicates == predicates);
    }
}


TEST_CASE("Native scaled signed address chains retain low carry and shifted high bits", "[native-shader]") {
    const u32 shift = GENERATE(1U, 4U, 16U, 31U);
    const u32 destination = GENERATE(2U, 4U);
    const bool constant_high = GENERATE(false, true);
    const std::array<Native, 3> input{{
        {{(destination << 16) | 0x7211U, 2, 0x078800ffU | (shift << 11), 0}},
        {{((destination + 1) << 16) | (constant_high ? 0x7a11U : 0x7211U), constant_high ? 0x00c04000U : 3U, 0x020f06ffU | (shift << 11), 0}},
        VertexTransform.back(),
    }};
    for (u32 offset : {0U, 1U, 0x7fffffffU, 0x80000000U, 0xffffffffU})
        for (u64 base : std::array<u64, 4>{0, 0xffffffff, 0x12345678ffffffff, ~u64{0}}) {
            ScalarNativeMachine machine;
            machine.regs[0] = offset;
            machine.regs[2] = static_cast<u32>(base);
            machine.regs[3] = static_cast<u32>(base >> 32);
            machine.constants[(u64{3} << 32) | 0x100] = static_cast<u32>(base >> 32);
            machine.predicates = {true, false, true, true, false, true, false};
            auto predicates = machine.predicates;
            predicates[4] = u64{static_cast<u32>(offset << shift)} + static_cast<u32>(base) >= 0x100000000ULL;
            ExecutePredicatedScalarNative(input, machine);
            const u64 expected = base + (static_cast<u64>(static_cast<s64>(static_cast<s32>(offset))) << shift);
            REQUIRE(machine.regs[destination] == static_cast<u32>(expected));
            REQUIRE(machine.regs[destination + 1] == static_cast<u32>(expected >> 32));
            REQUIRE(machine.predicates == predicates);
        }
}


TEST_CASE("Native immediate wide multiply preserves signed products addends and aliases", "[native-shader]") {
    const u32 multiplier = GENERATE(0U, 1U, 16U, 0x80000000U, 0xffffffffU);
    const u32 destination = GENERATE(0U, 4U, 8U);
    const bool signed_product = GENERATE(false, true);
    const u32 guard = GENERATE(3U, 11U, 7U);
    const std::array<Native, 2> input{{
        {{(destination << 16) | (guard << 12) | 0x825U, multiplier, 0x078e0004U | (signed_product ? 0x200U : 0U), 0}},
        VertexTransform.back(),
    }};
    for (bool enabled : {false, true}) for (u32 a : {0U, 1U, 0x7fffffffU, 0x80000000U, 0xffffffffU})
        for (u64 addend : {0ULL, 0xffffffffULL, 0x12345678ffffffffULL, ~0ULL}) {
            ScalarNativeMachine machine;
            machine.regs[0] = a;
            machine.regs[4] = static_cast<u32>(addend);
            machine.regs[5] = static_cast<u32>(addend >> 32);
            machine.predicates = {true, false, true, enabled, false, true, false};
            const auto predicates = machine.predicates;
            const u64 before = machine.regs[destination] | (u64{machine.regs[destination + 1]} << 32);
            const bool active = ((guard & 7) == 7 ? true : enabled) != bool(guard & 8);
            const u64 product = signed_product ? static_cast<u64>(s64{static_cast<s32>(a)} * static_cast<s32>(multiplier)) : u64{a} * multiplier;
            ExecutePredicatedScalarNative(input, machine);
            REQUIRE((machine.regs[destination] | (u64{machine.regs[destination + 1]} << 32)) == (active ? product + addend : before));
            REQUIRE(machine.predicates == predicates);
        }
}


TEST_CASE("Native general registers below the used uniform slots remain available", "[native-shader]") {
    const u32 destination = GENERATE(216U, 217U, 218U, 219U, 220U);
    const std::array<Native, 3> input{{
        {{(destination << 16) | 0x7802U, 0x12345678, 0xf00, 0}},
        {{(4U << 24) | (4U << 16) | 0x78a4U, 5, 0x0f8e023f, 0}},
        VertexTransform.back(),
    }};
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(static_cast<bool>(lowered) == (destination < 220));
    if (!lowered) return;
    ScalarNativeMachine machine;
    machine.regs[220] = 7;
    ExecutePredicatedScalarNative(input, machine);
    REQUIRE(machine.regs[destination] == 0x12345678);
    REQUIRE(machine.regs[220] == 35);
}


TEST_CASE("Native shadowed high registers preserve borrowed values across guarded ReLU", "[native-shader]") {
    const u32 guard = GENERATE(0U, 6U, 8U, 14U, 7U);
    const std::array<Native, 5> input{{
        {{0x00047882, 3, 0, 0}},
        {{0x00f67802, 0xbc00c000, 0xf00, 0}},
        {{0x00f77802, 0x3c003c00, 0xf00, 0}},
        {{(246U << 24) | (246U << 16) | (guard << 12) | 0x831U, 0x3c003c00, 0x038080f7, 0}},
        VertexTransform.back(),
    }};
    for (bool enabled : {false, true}) {
        ScalarNativeMachine machine;
        for (u32 reg = 0; reg < 220; ++reg) machine.regs[reg] = reg * 0x10101;
        machine.predicates = {enabled, true, false, true, false, true, enabled};
        const auto predicates = machine.predicates;
        const auto original = machine.regs;
        ExecutePredicatedScalarNative(input, machine, true);
        const bool active = ((guard & 7) == 7 ? true : enabled) != bool(guard & 8);
        REQUIRE(machine.regs[502] == (active ? 0U : 0xbc00c000U));
        REQUIRE(machine.regs[503] == 0x3c003c00);
        REQUIRE(machine.regs[220] == 3);
        for (u32 reg = 0; reg < 220; ++reg) REQUIRE(machine.regs[reg] == original[reg]);
        REQUIRE(machine.predicates == predicates);
    }
}


TEST_CASE("Native bitwise LUTs preserve all input bits and aliases", "[native-shader]") {
    const u32 lut = GENERATE(0x3cU, 0x78U, 0x30U, 0xf4U);
    const u32 destination = GENERATE(0U, 1U, 2U, 3U);
    const std::array<Native, 2> input{{
        {{(destination << 16) | 0x7212U, 1, 0x078e0002U | (lut << 8), 0}}, VertexTransform.back(),
    }};
    for (u32 a : {0U, 0xffffffffU, 0xaaaa5555U}) for (u32 b : {0U, 0xffffffffU, 0x13579bdfU})
        for (u32 c : {0U, 0xffffffffU, 0x2468ace0U}) {
            ScalarNativeMachine machine;
            machine.regs[0] = a; machine.regs[1] = b; machine.regs[2] = c;
            ExecutePredicatedScalarNative(input, machine);
            u32 expected = 0;
            for (u32 bit = 0; bit < 32; ++bit) {
                const u32 index = (((a >> bit) & 1) << 2) | (((b >> bit) & 1) << 1) | ((c >> bit) & 1);
                expected |= ((lut >> index) & 1) << bit;
            }
            REQUIRE(machine.regs[destination] == expected);
        }
}


TEST_CASE("Native high address shifts retain both words and the carry predicate", "[native-shader]") {
    const u32 shift = GENERATE(1U, 4U, 16U, 31U);
    const u32 carry = GENERATE(1U, 7U, 9U, 15U);
    const u32 destination = GENERATE(0U, 2U, 3U, 4U);
    const bool constant = GENERATE(false, true);
    const std::array<Native, 2> input{{
        {{(destination << 16) | (constant ? 0x7a11U : 0x7211U), constant ? 0x00c04000U : 3U, 0x000f0402U | (shift << 11) | (carry << 23), 0}}, VertexTransform.back(),
    }};
    for (u64 value : {0ULL, 0x12345678abcdef01ULL, 0x80000000ffffffffULL, ~0ULL})
        for (u32 base : {0U, 1U, 0xffffffffU}) for (bool enabled : {false, true}) {
            ScalarNativeMachine machine;
            machine.regs[0] = static_cast<u32>(value);
            machine.regs[2] = static_cast<u32>(value >> 32);
            machine.regs[3] = base;
            machine.constants[(u64{3} << 32) | 0x100] = base;
            machine.predicates = {true, enabled, false, true, false, true, false};
            const auto predicates = machine.predicates;
            const bool increment = ((carry & 7) == 7 ? true : enabled) != bool(carry & 8);
            const u32 expected = static_cast<u32>((value << shift) >> 32) + base + static_cast<u32>(increment);
            ExecutePredicatedScalarNative(input, machine);
            REQUIRE(machine.regs[destination] == expected);
            REQUIRE(machine.predicates == predicates);
        }
}


TEST_CASE("Native unsigned halfword global loads preserve addresses and ignore descriptors", "[native-shader]") {
    const bool shadow = GENERATE(false, true);
    const u32 guard = GENERATE(0U, 8U, 7U);
    const u32 destination = shadow ? 246U : 4U, pointer = shadow ? 250U : 2U;
    const std::array<Native, 2> input{{
        {{(pointer << 24) | (destination << 16) | (guard << 12) | 0x981U, 6, 0x0c1e1500, 0}}, VertexTransform.back(),
    }};
    for (bool enabled : {false, true}) for (u64 address : {0x12345002ULL, 0x12345678abcdef02ULL}) {
        ScalarNativeMachine machine;
        const auto guest = [&](u32 reg) { return shadow ? reg + 256 : reg; };
        machine.regs[guest(pointer)] = static_cast<u32>(address);
        machine.regs[guest(pointer + 1)] = static_cast<u32>(address >> 32);
        machine.regs[guest(destination)] = 0xdeadbeef;
        machine.regs[222] = 0x11111111; machine.regs[223] = 0x22222222;
        machine.global[address] = 0xabcd8765;
        machine.predicates = {enabled, true, false, true, false, true, false};
        const auto predicates = machine.predicates;
        ExecutePredicatedScalarNative(input, machine, shadow);
        const bool active = ((guard & 7) == 7 ? true : enabled) != bool(guard & 8);
        REQUIRE(machine.regs[guest(destination)] == (active ? 0x8765U : 0xdeadbeefU));
        REQUIRE(machine.regs[guest(pointer)] == static_cast<u32>(address));
        REQUIRE(machine.regs[guest(pointer + 1)] == static_cast<u32>(address >> 32));
        REQUIRE(machine.regs[222] == 0x11111111);
        REQUIRE(machine.regs[223] == 0x22222222);
        REQUIRE(machine.predicates == predicates);
    }
}


TEST_CASE("Native immediate high address shifts preserve signed and unsigned extensions", "[native-shader]") {
    const u32 shift = GENERATE(1U, 4U, 31U);
    const bool sign = GENERATE(false, true);
    const u32 destination = GENERATE(0U, 4U);
    const u32 base = GENERATE(0U, 0x20U, 0xffffffffU);
    const std::array<Native, 2> input{{
        {{(destination << 16) | 0x7811U, base, 0x078f00ffU | (shift << 11) | (sign ? 0x200U : 0U), 0}}, VertexTransform.back(),
    }};
    for (u32 value : {0U, 1U, 0x7fffffffU, 0x80000000U, 0xffffffffU}) {
        ScalarNativeMachine machine;
        machine.regs[0] = value;
        const u32 high = sign ? static_cast<u32>(static_cast<s32>(value) >> (32 - shift)) : value >> (32 - shift);
        ExecutePredicatedScalarNative(input, machine);
        REQUIRE(machine.regs[destination] == high + base);
    }
}


TEST_CASE("Native local spills retain dynamic byte addresses offsets and predication", "[native-shader]") {
    const u32 displacement = GENERATE(0U, 4U, 12U);
    const u32 guard = GENERATE(0U, 8U, 7U);
    const bool last_use = GENERATE(false, true);
    const std::array<Native, 3> input{{
        {{(1U << 24) | (guard << 12) | 0x387U, (displacement << 8) | 4U, 0x00100800, 0}},
        {{(1U << 24) | (5U << 16) | 0x7983U, displacement << 8, last_use ? 0x00300800U : 0x00100800U, 0}},
        VertexTransform.back(),
    }};
    for (bool active : {false, true}) for (u32 base : {0U, 16U, 32U}) {
        ScalarNativeMachine machine;
        machine.regs[1] = base;
        machine.regs[4] = 0x1234abcd;
        machine.predicates = {active, true, false, true, false, true, false};
        const auto predicates = machine.predicates;
        for (u32 word = 0; word < 16; ++word) machine.local[word] = 0xdeadbeef;
        const bool enabled = ((guard & 7) == 7 ? true : active) != bool(guard & 8);
        ExecutePredicatedScalarNative(input, machine, false, 64);
        REQUIRE(machine.regs[5] == (enabled ? 0x1234abcdU : 0xdeadbeefU));
        REQUIRE(machine.regs[1] == base);
        for (u32 word = 0; word < 16; ++word)
            REQUIRE(machine.local.at(word) == (enabled && word == (base + displacement) / 4 ? 0x1234abcdU : 0xdeadbeefU));
        REQUIRE(machine.predicates == predicates);
    }
}


TEST_CASE("Native uniform subtrahends preserve modular results and execution guards", "[native-shader]") {
    const u32 destination = GENERATE(0U, 4U);
    const u32 guard = GENERATE(0U, 8U, 7U);
    const std::array<Native, 2> input{{
        {{(destination << 16) | (guard << 12) | 0xc10U, 0x80000006, 0x0fffe0ff, 0}}, VertexTransform.back(),
    }};
    for (u32 a : {0U, 1U, 0x80000000U, 0xffffffffU}) for (u32 b : {0U, 1U, 0x80000000U, 0xffffffffU})
        for (bool enabled : {false, true}) {
            ScalarNativeMachine machine;
            machine.regs[0] = a; machine.regs[222] = b;
            machine.predicates = {enabled, true, false, true, false, true, false};
            const auto predicates = machine.predicates;
            const u32 before = machine.regs[destination];
            const bool active = ((guard & 7) == 7 ? true : enabled) != bool(guard & 8);
            ExecutePredicatedScalarNative(input, machine);
            REQUIRE(machine.regs[destination] == (active ? a - b : before));
            REQUIRE(machine.regs[222] == b);
            REQUIRE(machine.predicates == predicates);
        }
}


TEST_CASE("Native hyperbolic tangent retains its function modifiers and execution guard", "[native-shader]") {
    const u32 destination = GENERATE(0U, 4U);
    const u32 guard = GENERATE(0U, 8U, 7U);
    const bool negate = GENERATE(false, true), absolute = GENERATE(false, true);
    const std::array<Native, 2> input{{
        {{(destination << 16) | (guard << 12) | 0x308U, (negate ? 0x80000000U : 0U) | (absolute ? 0x40000000U : 0U), 0x2400, 0}}, VertexTransform.back(),
    }};
    for (bool enabled : {false, true}) for (u32 bits : {0U, 0x80000000U, 1U, 0x80000001U, 0x3dcccccdU, 0xbf000000U, 0x40000000U, 0x41200000U, 0x7f800000U, 0xff800000U, 0x7fc12345U}) {
        ScalarNativeMachine machine;
        machine.regs[0] = bits;
        machine.predicates = {enabled, true, false, true, false, true, false};
        const auto predicates = machine.predicates;
        const u32 before = machine.regs[destination];
        const bool active = ((guard & 7) == 7 ? true : enabled) != bool(guard & 8);
        float value = std::bit_cast<float>(bits);
        if (absolute) value = std::abs(value);
        if (negate) value = -value;
        ExecutePredicatedScalarNative(input, machine);
        if (!active) REQUIRE(machine.regs[destination] == before);
        else if (std::isnan(value)) REQUIRE(std::isnan(std::bit_cast<float>(machine.regs[destination])));
        else REQUIRE(machine.regs[destination] == std::bit_cast<u32>(std::tanh(value)));
        REQUIRE(machine.predicates == predicates);
    }
}


TEST_CASE("Native packed half comparisons detect infinity and NaN independently", "[native-shader]") {
    const u32 destination = GENERATE(0U, 4U);
    const bool immediate = GENERATE(false, true);
    const std::array<Native, 2> input{{
        {{(destination << 16) | (immediate ? 0x7433U : 0x7233U), immediate ? 0x7c007c00U : 1U, 0x0380a200, 0}}, VertexTransform.back(),
    }};
    const std::array<u32, 9> halves{{0, 1, 0x3c00, 0xbc00, 0x7bff, 0x7c00, 0xfc00, 0x7c01, 0xfe00}};
    for (u32 low : halves) for (u32 high : halves) {
        ScalarNativeMachine machine;
        machine.regs[0] = low | (high << 16);
        machine.regs[1] = 0x7c007c00;
        machine.predicates = {true, false, true, false, true, false, true};
        const auto predicates = machine.predicates;
        const u32 expected = ((low & 0x7fff) >= 0x7c00 ? 0xffffU : 0U) | ((high & 0x7fff) >= 0x7c00 ? 0xffff0000U : 0U);
        ExecutePredicatedScalarNative(input, machine);
        REQUIRE(machine.regs[destination] == expected);
        REQUIRE(machine.predicates == predicates);
    }
}


TEST_CASE("Native uniform reciprocal uses the uniform source and preserves both register banks", "[native-shader]") {
    const u32 guard = GENERATE(0U, 8U, 7U);
    const bool negate = GENERATE(false, true), absolute = GENERATE(false, true);
    const std::array<Native, 2> input{{
        {{(4U << 16) | (guard << 12) | 0xd08U, 4U | (negate ? 0x80000000U : 0U) | (absolute ? 0x40000000U : 0U), 0x08001000, 0}}, VertexTransform.back(),
    }};
    for (bool enabled : {false, true}) for (u32 bits : {0U, 0x80000000U, 0x3f800000U, 0xbf800000U, 0x40000000U, 0x7f800000U, 0xff800000U}) {
        ScalarNativeMachine machine;
        machine.regs[220] = bits;
        machine.regs[4] = 0x12345678;
        machine.predicates = {enabled, true, false, true, false, true, false};
        const auto predicates = machine.predicates;
        float value = std::bit_cast<float>(bits);
        if (absolute) value = std::abs(value);
        if (negate) value = -value;
        const bool active = ((guard & 7) == 7 ? true : enabled) != bool(guard & 8);
        ExecutePredicatedScalarNative(input, machine);
        REQUIRE(machine.regs[4] == (active ? std::bit_cast<u32>(1.0f / value) : 0x12345678U));
        REQUIRE(machine.regs[220] == bits);
        REQUIRE(machine.predicates == predicates);
    }
}


TEST_CASE("Native low-word logical right shifts preserve bounds aliases and guards", "[native-shader]") {
    const u32 shift = GENERATE(0U, 1U, 8U, 16U, 31U, 32U);
    const u32 destination = GENERATE(0U, 4U);
    const u32 guard = GENERATE(0U, 8U, 7U);
    const std::array<Native, 2> input{{
        {{(4U << 24) | (destination << 16) | (guard << 12) | 0x819U, shift, 0x16ff, 0}}, VertexTransform.back(),
    }};
    for (u32 value : {0U, 1U, 0x80000000U, 0xffffffffU, 0x12345678U}) for (bool enabled : {false, true}) {
        ScalarNativeMachine machine;
        machine.regs[4] = value;
        machine.predicates = {enabled, true, false, true, false, true, false};
        const auto predicates = machine.predicates;
        const u32 before = machine.regs[destination];
        const bool active = ((guard & 7) == 7 ? true : enabled) != bool(guard & 8);
        ExecutePredicatedScalarNative(input, machine);
        REQUIRE(machine.regs[destination] == (active ? shift == 32 ? 0U : value >> shift : before));
        REQUIRE(machine.predicates == predicates);
    }
}


TEST_CASE("Overlapping storage descriptors share their virtual bytes across physical gaps", "[native-gpu][storage-ranges]") {
    const bool reverse = GENERATE(false, true);
    std::array<VideoCommon::StorageAddressRange, 4> ranges{{
        {0x1000, 0x1800}, {0x2400, 0x1000}, {0x3300, 0x100}, {0x3400, 0x1000},
    }};
    if (reverse) std::reverse(ranges.begin(), ranges.end());
    const auto groups = VideoCommon::GroupStorageRanges(ranges);
    REQUIRE(groups.size() == 2);
    REQUIRE(groups[0].address == 0x1000);
    REQUIRE(groups[0].end == 0x3400);
    REQUIRE(groups[0].members.size() == 3);
    REQUIRE(groups[1].address == 0x3400);
    REQUIRE(groups[1].end == 0x4400);
    const auto physical = [](u64 address) { return address < 0x2200 ? 0x8000 + address - 0x1000 : 0x20000 + address - 0x2200; };
    std::vector<u8> actual(0x30000, 0xa5);
    for (u64 address = 0x1000; address < 0x4400; ++address) actual[physical(address)] = static_cast<u8>(address % 251);
    auto expected = actual;
    std::vector<std::vector<u8>> gathered;
    for (const auto& group : groups) {
        auto& data = gathered.emplace_back(static_cast<size_t>(group.end - group.address));
        for (u64 address = group.address; address < group.end; ++address) data[address - group.address] = actual[physical(address)];
    }
    const auto view = [&](u64 address) -> std::span<u8> {
        const auto index = static_cast<size_t>(std::find_if(ranges.begin(), ranges.end(), [&](const auto& range) { return range.address == address; }) - ranges.begin());
        for (size_t group = 0; group < groups.size(); ++group)
            if (std::find(groups[group].members.begin(), groups[group].members.end(), index) != groups[group].members.end())
                return std::span{gathered[group]}.subspan(address - groups[group].address, ranges[index].size);
        throw std::logic_error("missing storage view");
    };
    view(0x1000)[0x1500] = 0x42;
    expected[physical(0x2500)] = 0x42;
    REQUIRE(view(0x2400)[0x100] == 0x42);
    view(0x2400)[0x101] = 0x37;
    expected[physical(0x2501)] = 0x37;
    REQUIRE(view(0x1000)[0x1501] == 0x37);
    view(0x3400)[0x10] = 0xe3;
    expected[physical(0x3410)] = 0xe3;
    for (size_t i = 0; i < groups.size(); ++i)
        for (u64 address = groups[i].address; address < groups[i].end; ++address)
            actual[physical(address)] = gathered[i][address - groups[i].address];
    REQUIRE(actual == expected);
}


TEST_CASE("Storage range grouping handles nested views null bindings and address overflow", "[native-gpu][storage-ranges]") {
    const std::array<VideoCommon::StorageAddressRange, 5> ranges{{
        {0x1000, 0x4000}, {0x2000, 0x100}, {0x4f00, 0x200}, {0, 0x1000}, {0x9000, 0},
    }};
    const auto groups = VideoCommon::GroupStorageRanges(ranges);
    REQUIRE(groups.size() == 1);
    REQUIRE(groups[0].address == 0x1000);
    REQUIRE(groups[0].end == 0x5100);
    REQUIRE(groups[0].members.size() == 3);
    const std::array<VideoCommon::StorageAddressRange, 1> invalid{{{~u64{0} - 1, 4}}};
    REQUIRE_THROWS_AS(VideoCommon::GroupStorageRanges(invalid), std::overflow_error);
}


TEST_CASE("Native 2D gradients preserve selected RG channels and derivative order", "[native-shader]") {
    const std::array<Native, 2> input{{{{0x1a1a1b6c, 0x21c00810, 0x001e03ff, 0x000f4600}}, VertexTransform.back()}};
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(lowered);
    REQUIRE(lowered.required_texture_buffer == 7);
    REQUIRE(Shader::Maxwell::Instruction{lowered.instructions.front()}.Pred().index == 1);
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    u32 samples{};
    std::vector<u32> writes;
    for (const auto& inst : block.Instructions()) {
        if (inst.GetOpcode() == IR::Opcode::BoundImageGradient) {
            REQUIRE(inst.Arg(0).U32() == 32);
            REQUIRE(inst.Flags<IR::TextureInstInfo>().type.Value() == Shader::TextureType::Color2D);
            REQUIRE(inst.Flags<IR::TextureInstInfo>().num_derivatives.Value() == 2);
            for (u32 component = 0; component < 2; ++component)
                REQUIRE(inst.Arg(1).Inst()->Arg(component).Inst()->Arg(0).Inst()->Arg(0).Reg() == IR::Reg{26 + component});
            for (u32 component = 0; component < 4; ++component)
                REQUIRE(inst.Arg(2).Inst()->Arg(component).Inst()->Arg(0).Inst()->Arg(0).Reg() == IR::Reg{16 + component});
            ++samples;
        }
        if (inst.GetOpcode() == IR::Opcode::SetRegister) writes.push_back(static_cast<u32>(inst.Arg(0).Reg()));
    }
    REQUIRE(samples == 1);
    REQUIRE(writes == std::vector<u32>{26, 27});
}


TEST_CASE("Native gradient samples retain nonconsecutive channel pairs across descriptor banks", "[native-shader]") {
    const bool mixed = GENERATE(false, true);
    std::vector<Native> input;
    if (mixed) input.push_back(Native{{0x03027b6f, 0x00800800, 0x000003ff, 0}});
    input.push_back(Native{{0x02020b6c, 0x21c00010, 0x041e0f14, 0x000f4200}});
    input.push_back(VertexTransform.back());
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    std::vector<u32> writes;
    u32 samples{}, handles{};
    for (const auto& inst : block.Instructions()) {
        if (inst.GetOpcode() == IR::Opcode::GetCbufU32) {
            REQUIRE(inst.Arg(0).U32() == 7);
            REQUIRE(inst.Arg(1).U32() == 0);
            ++handles;
        }
        if (inst.GetOpcode() == (mixed ? IR::Opcode::BindlessImageGradient : IR::Opcode::BoundImageGradient)) {
            REQUIRE(inst.Flags<IR::TextureInstInfo>().type.Value() == Shader::TextureType::Color2D);
            REQUIRE(inst.Flags<IR::TextureInstInfo>().num_derivatives.Value() == 2);
            ++samples;
        }
        if (inst.GetOpcode() == IR::Opcode::SetRegister) writes.push_back(static_cast<u32>(inst.Arg(0).Reg()));
    }
    REQUIRE(samples == 1);
    REQUIRE(handles == (mixed ? 1U : 0U));
    REQUIRE(writes.size() >= 4);
    REQUIRE(std::vector<u32>(writes.end() - 4, writes.end()) == std::vector<u32>{2, 3, 20, 21});
}


TEST_CASE("Native predicated mixed-bank sampling defines its scratch descriptor on both paths", "[native-shader]") {
    const bool cube = GENERATE(false, true);
    const std::array<Native, 3> input{{
        {{0x03027b6f, 0x01c00800, 0x000003ff, 0}},
        {{0x04100b60, cube ? 0x70c00808U : 0x30c00808U, 0x049e03ff, 0}},
        VertexTransform.back(),
    }};
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(lowered);
    u32 descriptors{}, samples{};
    for (const auto instruction : lowered.instructions) {
        if ((instruction >> 48) == 0x4c98) {
            REQUIRE(((instruction >> 16) & 15) == 7);
            ++descriptors;
        }
        if ((instruction >> 56) == 0xde) {
            REQUIRE(((instruction >> 16) & 15) == 0);
            ++samples;
        }
    }
    REQUIRE(descriptors == 1);
    REQUIRE(samples == 1);
}


TEST_CASE("Native biased samples preserve XY bias and selected output channels", "[native-shader]") {
    const bool mixed = GENERATE(false, true);
    const u32 mask = GENERATE(1U, 5U, 15U);
    std::vector<Native> input;
    if (mixed) input.push_back({{0x03027b6f, 0x00c00800, 0x000003ff, 0}});
    input.push_back({{0x00307b60, 0x31c00606, 0x011e0000U | (mask << 8) | (mask == 15 ? 52U : 255U), 0}});
    input.push_back(VertexTransform.back());
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    u32 samples{};
    for (const auto& inst : block.Instructions()) {
        if (inst.GetOpcode() != (mixed ? IR::Opcode::BindlessImageSampleImplicitLod : IR::Opcode::BoundImageSampleImplicitLod)) continue;
        REQUIRE(inst.Flags<IR::TextureInstInfo>().has_bias.Value() == 1);
        REQUIRE(inst.Flags<IR::TextureInstInfo>().type.Value() == Shader::TextureType::Color2D);
        const auto* coords = inst.Arg(1).Inst();
        REQUIRE(coords->Arg(0).Inst()->Arg(0).Inst()->Arg(0).Reg() == IR::Reg{0});
        REQUIRE(coords->Arg(1).Inst()->Arg(0).Inst()->Arg(0).Reg() == IR::Reg{1});
        REQUIRE(inst.Arg(2).Inst()->Arg(0).Inst()->Arg(0).Reg() == IR::Reg{mixed ? 249U : 6U});
        ++samples;
    }
    REQUIRE(samples == 1);
}


TEST_CASE("Native scalar texture samples read their descriptor from the uniform bank", "[native-shader]") {
    const u32 mask = GENERATE(1U, 3U, 5U, 6U, 7U, 15U);
    const u32 count = std::popcount(mask);
    const std::array<Native, 2> input{{{{mask == 1 ? 0x1e097f60U : 0x1e087f60U,
        0x30000409, 0x081e0000U | (mask << 8) | (count > 2 ? 12U : 255U), 0x000f4200}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    u32 samples{};
    std::vector<u32> components;
    std::vector<std::pair<u32, u32>> copies;
    for (const auto& inst : block.Instructions()) {
        if (inst.GetOpcode() == IR::Opcode::CompositeExtractF32x4) components.push_back(inst.Arg(1).U32());
        if (inst.GetOpcode() == IR::Opcode::SetRegister && inst.Arg(1).Inst()->GetOpcode() == IR::Opcode::GetRegister) {
            const u32 from = static_cast<u32>(inst.Arg(1).Inst()->Arg(0).Reg());
            if (from >= 250 && from <= 253 && static_cast<u32>(inst.Arg(0).Reg()) < 216)
                copies.emplace_back(static_cast<u32>(inst.Arg(0).Reg()), from);
        }
        if (inst.GetOpcode() != IR::Opcode::BindlessImageSampleImplicitLod) continue;
        REQUIRE(inst.Arg(0).Inst()->Arg(0).Reg() == IR::Reg{220});
        REQUIRE(inst.Flags<IR::TextureInstInfo>().type.Value() == Shader::TextureType::Color2D);
        REQUIRE(inst.Arg(1).Inst()->Arg(0).Inst()->Arg(0).Inst()->Arg(0).Reg() == IR::Reg{250});
        REQUIRE(inst.Arg(1).Inst()->Arg(1).Inst()->Arg(0).Inst()->Arg(0).Reg() == IR::Reg{251});
        ++samples;
    }
    REQUIRE(samples == 1);
    std::vector<u32> expected_components;
    std::vector<std::pair<u32, u32>> expected_copies;
    for (u32 channel = 0; channel < 4; ++channel) if (mask & (1U << channel)) expected_components.push_back(channel);
    for (u32 i = 0; i < count; ++i) expected_copies.emplace_back(i < 2 ? (mask == 1 ? 9U : 8U) + i : 12U + i - 2, 250 + i);
    REQUIRE(components == expected_components);
    REQUIRE(copies == expected_copies);
}


TEST_CASE("Native fused multiply-add reads a uniform multiplier or addend", "[native-shader]") {
    const bool uniform_addend = GENERATE(false, true);
    const std::array<Native, 5> input{{
        VertexTransform.front(),
        {{0x00047882, 0x40400000, 0, 0}},
        {{uniform_addend ? 0x00007e23U : 0x00007c23U, 4, 0x08010001, 0}},
        {{0xff007322, 0x00007000, 0x000000ff, 0}},
        VertexTransform.back(),
    }};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    REQUIRE(EvaluateVertex(block, {8, 4}, {}) == std::array<float, 2>{uniform_addend ? 35.0f : 28.0f, 0});
    REQUIRE(EvaluateVertex(block, {-2, 5}, {}) == std::array<float, 2>{uniform_addend ? -7.0f : -1.0f, 0});
}


TEST_CASE("Native implicit cube samples preserve split RGB output and XYZ coordinates", "[native-shader]") {
    const bool mixed = GENERATE(false, true);
    std::vector<Native> input;
    if (mixed) input.push_back({{0x03027b6f, 0x01c00800, 0x000003ff, 0}});
    input.push_back({{0x06027b60, 0x70800c02, 0x041e0706, 0x000f4400}});
    input.push_back(VertexTransform.back());
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    u32 samples{};
    for (const auto& inst : block.Instructions()) {
        if (inst.GetOpcode() != (mixed ? IR::Opcode::BindlessImageSampleImplicitLod : IR::Opcode::BoundImageSampleImplicitLod)) continue;
        REQUIRE(inst.Flags<IR::TextureInstInfo>().type.Value() == Shader::TextureType::ColorCube);
        for (u32 i = 0; i < 3; ++i)
            REQUIRE(inst.Arg(1).Inst()->Arg(i).Inst()->Arg(0).Inst()->Arg(0).Reg() == IR::Reg{(mixed ? 252U : 250U) + i});
        ++samples;
    }
    REQUIRE(samples == 1);
}


TEST_CASE("Native two-channel samples retain disjoint component masks and separate coordinates", "[native-shader]") {
    const bool mixed = GENERATE(false, true);
    const u32 mask = GENERATE(5U, 6U);
    const bool zero_lod = GENERATE(false, true);
    std::vector<Native> input;
    if (mixed) input.push_back({{0x03027b6f, 0x01c00800, 0x000003ff, 0}});
    input.push_back({{0x03047b60, 0x3140120f, 0x041e00ffU | (mask << 8) | (zero_lod ? 0x00800000U : 0), 0}});
    input.push_back(VertexTransform.back());
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    u32 samples{};
    std::vector<u32> components;
    for (const auto& inst : block.Instructions()) {
        if (inst.GetOpcode() == (zero_lod ? (mixed ? IR::Opcode::BindlessImageSampleExplicitLod : IR::Opcode::BoundImageSampleExplicitLod) :
                                                  (mixed ? IR::Opcode::BindlessImageSampleImplicitLod : IR::Opcode::BoundImageSampleImplicitLod))) {
            REQUIRE(inst.Flags<IR::TextureInstInfo>().type.Value() == Shader::TextureType::Color2D);
            ++samples;
        }
        if (inst.GetOpcode() == IR::Opcode::CompositeExtractF32x4) components.push_back(inst.Arg(1).U32());
    }
    REQUIRE(samples == 1);
    REQUIRE(components == std::vector<u32>{mask == 5 ? 0U : 1U, 2U});
}


TEST_CASE("Native unsigned 32-bit conversion preserves nearest and toward-zero rounding", "[native-shader]") {
    const bool truncate = GENERATE(false, true);
    const std::array<Native, 2> input{{{{0x00157245, 5, truncate ? 0x0020d000U : 0x00201000U, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    std::vector<u32> values{0, 1, 0xffffffffU};
    for (u32 bit = 1; bit < 32; ++bit) {
        const u32 power = 1U << bit;
        values.insert(values.end(), {power - 1, power, power + 1});
        if (bit >= 24) {
            const u32 half = 1U << (bit - 24);
            values.insert(values.end(), {power + half - 1, power + half, power + half + 1});
        }
    }
    u32 random = 0x19eab374;
    for (u32 i = 0; i < 1024; ++i) {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        values.push_back(random);
    }
    for (const u32 value : values) {
        float expected = static_cast<float>(value);
        if (truncate && static_cast<double>(expected) > static_cast<double>(value))
            expected = std::nextafter(expected, -std::numeric_limits<float>::infinity());
        ScalarNativeMachine machine;
        machine.regs[5] = value;
        machine.Execute(block);
        REQUIRE(machine.regs[21] == std::bit_cast<u32>(expected));
    }
}


TEST_CASE("Native texture samples retain packed signed offsets and consecutive coordinates", "[native-shader]") {
    const bool mixed = GENERATE(false, true);
    const bool zero_lod = GENERATE(false, true);
    std::vector<Native> input;
    if (mixed) input.push_back({{0x03027b6f, 0x01c00800, 0x000003ff, 0}});
    input.push_back({{0x0c0e7b60, 0x33812005, 0x001e11ffU | (zero_lod ? 0x00800000U : 0), 0}});
    input.push_back(VertexTransform.back());
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    u32 samples{};
    for (const auto& inst : block.Instructions()) {
        if (inst.GetOpcode() != (zero_lod ? (mixed ? IR::Opcode::BindlessImageSampleExplicitLod : IR::Opcode::BoundImageSampleExplicitLod) :
                                                   (mixed ? IR::Opcode::BindlessImageSampleImplicitLod : IR::Opcode::BoundImageSampleImplicitLod))) continue;
        for (u32 i = 0; i < 2; ++i) {
            REQUIRE(inst.Arg(1).Inst()->Arg(i).Inst()->Arg(0).Inst()->Arg(0).Reg() == IR::Reg{12 + i});
            const auto* offset = inst.Arg(3).Inst()->Arg(i).Inst();
            REQUIRE(offset->GetOpcode() == IR::Opcode::BitFieldSExtract);
            REQUIRE(offset->Arg(1).U32() == 4 * i);
            REQUIRE(offset->Arg(2).U32() == 4);
            REQUIRE(offset->Arg(0).Inst()->Arg(0).Reg() == IR::Reg{mixed ? 249U : 5U});
        }
        ++samples;
    }
    REQUIRE(samples == 1);
}


TEST_CASE("Native uniform bitwise logic preserves separate predicate banks and discarded destinations", "[native-shader]") {
    const u32 output = GENERATE(0U, 1U, 2U, 3U, 4U, 5U, 6U);
    const u32 destination = GENERATE(5U, 63U);
    const std::array<Native, 2> input{{
        {{0x04007892U | (destination << 16), 1, 0x0f80c03fU | (output << 17), 0}}, VertexTransform.back(),
    }};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u32 bank : {0U, 0x2aU, 0x55U, 0x7fU}) for (u32 value : {0U, 1U, 2U, 3U, 0xffffffffU}) {
        ScalarNativeMachine machine;
        machine.regs[220] = value;
        machine.regs[239] = bank;
        for (u32 p = 0; p < 7; ++p) machine.predicates[p] = (p & 1) != 0;
        const auto ordinary = machine.predicates;
        machine.Execute(block);
        const u32 bit = 1U << (6 - output);
        REQUIRE(machine.regs[239] == ((bank & ~bit) | ((value & 1) ? bit : 0U)));
        REQUIRE(machine.predicates == ordinary);
        REQUIRE(machine.regs[220] == value);
        if (destination != 63) REQUIRE(machine.regs[221] == (value & 1));
    }
}


TEST_CASE("Native scaled multiplication preserves captured power-of-two scales (main integration coverage)", "[native-shader]") {
    std::array<Native, 4> input{{
        VertexTransform.front(),
        {{0x00007220, 0x00000001, 0x00310000, 0}},
        {{0xff007322, 0x00007000, 0x000000ff, 0}},
        VertexTransform.back(),
    }};
    const u32 scale = GENERATE(2U, 3U, 4U, 5U, 6U);
    input[1].words[2] = (scale << 20) | 0x10000;
    const float factor = scale == 2 ? 0.25f : scale == 3 ? 0.5f : scale == 5 ? 2.0f : scale == 6 ? 4.0f : 1.0f;
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(lowered);
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    EmptyEnvironment env;
    Shader::Maxwell::TranslatorVisitor visitor{env, block};
    visitor.ALD(lowered.instructions[0]);
    visitor.FMUL_reg(lowered.instructions[1]);
    visitor.AST(lowered.instructions[2]);
    REQUIRE(EvaluateVertex(block, {8, 4}, {}) == std::array<float, 2>{32 * factor, 0});
    REQUIRE(EvaluateVertex(block, {-2, 3}, {}) == std::array<float, 2>{-6 * factor, 0});
}


TEST_CASE("Native register-to-predicate writes only masked predicate bits (main integration coverage)", "[native-shader]") {
    const u32 byte = GENERATE(0U, 1U, 2U, 3U);
    const std::array<Native, 2> source{{{{0x05007804, 7, byte << 12, 0x000fe200}}, VertexTransform.back()}};
    const auto lowered = Shader::Ampere::LowerToMaxwell(source);
    REQUIRE(lowered);
    REQUIRE(Shader::Maxwell::Decode(lowered.instructions[0]) == Shader::Maxwell::Opcode::R2P_imm);
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    EmptyEnvironment env;
    Shader::Maxwell::TranslatorVisitor visitor{env, block};
    visitor.R2P_imm(lowered.instructions[0]);
    for (u32 value = 0; value < 128; ++value) {
        std::array<bool, 7> predicates{true, false, true, false, true, false, true};
        std::unordered_map<const IR::Inst*, u32> values;
        const auto get = [&](const IR::Value& v) -> u32 {
            if (!v.IsImmediate()) return values.at(v.Inst());
            return v.Type() == IR::Type::U1 ? static_cast<u32>(v.U1()) : v.U32();
        };
        for (const auto& inst : block.Instructions()) {
            const auto arg = [&](size_t i) { return get(inst.Arg(i)); };
            switch (inst.GetOpcode()) {
            case IR::Opcode::GetRegister:
                REQUIRE(static_cast<u32>(inst.Arg(0).Reg()) == 5);
                values[&inst] = value << (byte * 8); break;
            case IR::Opcode::GetPred: values[&inst] = predicates[static_cast<u32>(inst.Arg(0).Pred())]; break;
            case IR::Opcode::SetPred: predicates[static_cast<u32>(inst.Arg(0).Pred())] = arg(1) != 0; break;
            case IR::Opcode::BitFieldUExtract: values[&inst] = (arg(0) >> arg(1)) & ((1U << arg(2)) - 1); break;
            case IR::Opcode::GetZeroFromOp: values[&inst] = arg(0) == 0; break;
            case IR::Opcode::IEqual: values[&inst] = arg(0) == arg(1); break;
            case IR::Opcode::LogicalNot: values[&inst] = !arg(0); break;
            case IR::Opcode::SelectU1: values[&inst] = arg(0) ? arg(1) : arg(2); break;
            default: FAIL("Unexpected R2P IR " << IR::NameOf(inst.GetOpcode()));
            }
        }
        for (u32 bit = 0; bit < 7; ++bit)
            REQUIRE(predicates[bit] == (bit < 3 ? bool(value & (1U << bit)) : bool(bit % 2 == 0)));
    }
    auto invalid = source;
    invalid[0].words[2] = 1;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(invalid));
}


TEST_CASE("Captured native compute compiles with shared memory and atomics (main integration coverage)", "[.native-compute-captured]") {
    const char* directory = std::getenv("EDEN_NATIVE_SHADER_FIXTURES");
    REQUIRE(directory != nullptr);
    std::ifstream file(std::filesystem::path{directory} / "compute.source.bin", std::ios::binary | std::ios::ate);
    REQUIRE(file.good());
    const auto size = static_cast<size_t>(file.tellg());
    REQUIRE(size % 16 == 0);
    std::vector<Native> input(size / 16);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(input.data()), size);
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    INFO("offset=" << lowered.error_offset << " error=" << lowered.error);
    REQUIRE(lowered);
    Shader::Ampere::ProgramCaptureState capture;
    u32 captured_size{};
    for (u32 i = 0; i < input.size(); ++i) {
        const auto status = capture.Observe(input[i], i);
        REQUIRE(status != Shader::Ampere::ProgramCaptureState::Result::InvalidTarget);
        if (status == Shader::Ampere::ProgramCaptureState::Result::Complete) {
            captured_size = (i + 1) * sizeof(Native);
            break;
        }
    }
    REQUIRE(captured_size == lowered.source_size);
    std::vector<u64> code(0x50 / 8);
    for (u64 word : lowered.instructions) {
        if ((code.size() * 8) % 32 == 0) code.push_back(0);
        code.push_back(word);
    }
    code.push_back(0);
    Shader::Ampere::AppendNativeAddressMap(code, lowered.source_instruction_offsets);
    std::array<u32, 64> qmd{};
    std::ifstream descriptor(std::filesystem::path{directory} / "compute.qmd.bin", std::ios::binary);
    REQUIRE(descriptor.good());
    descriptor.read(reinterpret_cast<char*>(qmd.data()), sizeof(qmd));
    const std::array<u32, 3> group{qmd[18] >> 16, qmd[19] & 0xffff, qmd[19] >> 16};
    const u32 shared_size = qmd[17] & 0x3ffff;
    class ComputeFixtureEnvironment final : public EmptyEnvironment {
    public:
        ComputeFixtureEnvironment(const std::vector<u64>& code_, u32 shared_, std::array<u32, 3> group_, Shader::TextureType texture_)
            : code{code_}, shared{shared_}, group{group_}, texture{texture_} {
            stage = Shader::Stage::Compute;
            start_address = 0x50;
            is_proprietary_driver = true;
        }
        u64 ReadInstruction(u32 address) override { return code.at(address / 8); }
        u32 TranslateProgramAddress(u32 address) const override {
            const auto translated = Shader::Ampere::NativeProgramAddress(code, address);
            if (!translated) throw std::logic_error("invalid compute branch target");
            return *translated;
        }
        u32 ReadCbufValue(u32 slot, u32 offset) override {
            std::ifstream file(std::filesystem::path{std::getenv("EDEN_NATIVE_SHADER_FIXTURES")} /
                               ("compute.cbuf" + std::to_string(slot) + ".bin"), std::ios::binary);
            if (!file) return 0;
            file.seekg(offset);
            u32 value{};
            file.read(reinterpret_cast<char*>(&value), sizeof(value));
            if (!file) throw std::logic_error("captured compute constant outside buffer");
            return value;
        }
        u32 TextureBoundBuffer() const override { return 0; }
        u32 SharedMemorySize() const override { return shared; }
        std::array<u32, 3> WorkgroupSize() const override { return group; }
        Shader::TextureType ReadTextureType(u32) override { return texture; }
        const std::vector<u64>& code;
        u32 shared;
        std::array<u32, 3> group;
        Shader::TextureType texture;
    } env{code, shared_size, group,
        std::any_of(input.begin(), input.end(), [](const auto& instruction) {
            return instruction.Bits(0, 12) == 0xb66 && instruction.Bits(61, 3) == 2;
        }) ? Shader::TextureType::Color3D : std::any_of(input.begin(), input.end(), [](const auto& instruction) {
            return (instruction.Bits(0, 12) == 0xb66 && instruction.Bits(61, 3) == 1) ||
                   instruction.Bits(0, 12) == 0xb97 || instruction.Bits(0, 12) == 0xb9b;
        }) ? Shader::TextureType::Color2D : Shader::TextureType::Color1D};
    Shader::ObjectPool<Shader::Maxwell::Flow::Block> flow_pool;
    Shader::ObjectPool<IR::Inst> inst_pool;
    Shader::ObjectPool<IR::Block> block_pool;
    Shader::Maxwell::Flow::CFG cfg{env, flow_pool, env.StartAddress()};
    Shader::HostTranslateInfo host;
    host.support_int64 = true;
    host.support_float64 = true;
    host.support_float16 = true;
    host.support_conditional_barrier = true;
    const auto program = Shader::Maxwell::TranslateProgram(inst_pool, block_pool, env, cfg, host);
    std::ofstream(std::filesystem::path{directory} / "compute.ir.txt") << IR::DumpProgram(program);
    REQUIRE(program.stage == Shader::Stage::Compute);
    REQUIRE_FALSE(program.blocks.empty());
    u32 barriers{}, shared_atomics{}, global_atomics{}, stores{}, image_stores{}, image_loads{};
    for (const auto* block : program.blocks) for (const auto& inst : block->Instructions()) {
        image_loads += inst.GetOpcode() == IR::Opcode::ImageRead || inst.GetOpcode() == IR::Opcode::BoundImageRead;
        barriers += inst.GetOpcode() == IR::Opcode::Barrier;
        shared_atomics += inst.GetOpcode() == IR::Opcode::SharedAtomicIAdd32;
        global_atomics += inst.GetOpcode() == IR::Opcode::GlobalAtomicIAdd32 || inst.GetOpcode() == IR::Opcode::StorageAtomicIAdd32;
        stores += inst.GetOpcode() == IR::Opcode::WriteGlobal32 || inst.GetOpcode() == IR::Opcode::WriteStorage32;
        image_stores += inst.GetOpcode() == IR::Opcode::ImageWrite || inst.GetOpcode() == IR::Opcode::BoundImageWrite;
    }
    const bool surface = std::any_of(input.begin(), input.end(), [](const auto& instruction) { return instruction.Bits(0, 12) == 0xb9b; });
    if (surface) {
        const auto expected_barriers = std::count_if(input.begin(), input.end(), [](const auto& instruction) {
            return instruction.Bits(0, 12) == 0xb1d;
        });
        REQUIRE(barriers >= expected_barriers);
        REQUIRE(image_stores >= 1);
        const auto expected_loads = std::count_if(input.begin(), input.end(), [](const auto& instruction) {
            return instruction.Bits(0, 12) == 0xb97;
        });
        REQUIRE(image_loads >= expected_loads);
    } else if (std::any_of(input.begin(), input.end(), [](const auto& instruction) { return instruction.Bits(0, 12) == 0xf8c; })) {
        REQUIRE(barriers >= 2);
        REQUIRE(shared_atomics >= 4);
        REQUIRE(global_atomics >= 2);
    } else if (shared_size && std::any_of(input.begin(), input.end(), [](const auto& instruction) {
        return instruction.Bits(0, 12) == 0x38c;
    })) {
        REQUIRE(barriers >= 5);
        REQUIRE(shared_atomics >= 2);
        REQUIRE(global_atomics >= 1);
        REQUIRE(stores >= 8);
    } else {
        const auto expected_barriers = std::count_if(input.begin(), input.end(), [](const auto& instruction) {
            return instruction.Bits(0, 12) == 0xb1d;
        });
        if (expected_barriers) REQUIRE(barriers >= expected_barriers);
        else REQUIRE(barriers == 0);
        const auto expected_stores = std::count_if(input.begin(), input.end(), [](const auto& instruction) {
            return instruction.Bits(0, 12) == 0x986;
        });
        REQUIRE(expected_stores >= 1);
        REQUIRE(stores >= expected_stores);
    }
}

TEST_CASE("Native unsigned global maximum preserves full pointers and all registers", "[native-shader]") {
    const std::array<Native, 2> input{{{{0x0600798e, 4, 0x0110e100, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (u32 current : {0U, 1U, 0x7fffffffU, 0x80000000U, 0xffffffffU})
        for (u32 operand : {0U, 1U, 0x7fffffffU, 0x80000000U, 0xffffffffU}) {
            ScalarNativeMachine machine;
            for (u32 i = 0; i < 255; ++i) machine.regs[i] = i * 37 + 1;
            machine.regs[6] = 0xfffffff0;
            machine.regs[7] = 0x29;
            machine.regs[4] = operand;
            machine.predicates.fill(true);
            const auto registers = machine.regs;
            const auto predicates = machine.predicates;
            machine.global[0x29fffffff0ULL] = current;
            machine.global[0x29fffffff4ULL] = 0xcafebeef;
            machine.Execute(block);
            REQUIRE(machine.global[0x29fffffff0ULL] == std::max(current, operand));
            REQUIRE(machine.global[0x29fffffff4ULL] == 0xcafebeef);
            REQUIRE(machine.regs == registers);
            REQUIRE(machine.predicates == predicates);
        }
    for (const Native invalid : {Native{{0x0700798e, 4, 0x0110e100, 0}},
                                 Native{{0x0600798e, 4, 0x0090e100, 0}},
                                 Native{{0x0600798e, 4, 0x0110e101, 0}}}) {
        const std::array<Native, 2> rejected{{invalid, VertexTransform.back()}};
        REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(rejected));
    }
}


TEST_CASE("Merged uniform forms reserve high banks before preserving guest aliases", "[native-shader]") {
    const Native operation = GENERATE(
        Native{{0x001172be, 4, 0x08000000, 0}},
        Native{{0x000472be, 17, 0x08000000, 0}},
        Native{{0x04117896, 0x9910, 0x0800003f, 0}},
        Native{{0x11047896, 0x9910, 0x0800003f, 0}},
        Native{{0x3f117883, 1, 0x08000000, 0}},
        Native{{0x05117291, 8, 0x0f8e003f, 0}},
        Native{{0x11047291, 8, 0x0f8e003f, 0}},
        Native{{0x05047291, 17, 0x0f8e003f, 0}},
        Native{{0xff1c7984, 0x13011, 0x08000800, 0}},
        Native{{0xff007988, 0x13020, 0x08000811, 0}},
        Native{{0xff00798e, 0, 0x0c10e110, 0}},
        Native{{0x11307526, 0xff, 0x080e0021, 0x000e6200}});
    const std::array<Native, 3> input{{
        {{0x00c97802, 0x13579bdf, 0xf00, 0}}, operation, VertexTransform.back(),
    }};
    const auto unsafe = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE_FALSE(unsafe);
    REQUIRE(unsafe.error == "native program uses lowering scratch registers");
    const auto shadowed = Shader::Ampere::LowerToMaxwell(input, {}, true);
    INFO(shadowed.error << " offset=" << shadowed.error_offset);
    REQUIRE(shadowed);
}

TEST_CASE("Merged high uniform bit reversal preserves the actual guest register alias", "[native-shader]") {
    const bool high_source = GENERATE(false, true);
    const u32 source = high_source ? 17U : 4U, destination = high_source ? 4U : 17U;
    const auto ur = [](u32 index) { return 216U - (index / 16) * 16 + index % 16; };
    const std::array<Native, 3> input{{
        {{0x00c97802, 0x13579bdf, 0xf00, 0}},
        {{0x72beU | (destination << 16), source, 0x08000000, 0}}, VertexTransform.back(),
    }};
    for (u32 value : {0U, 1U, 0x01234567U, 0x80000000U, 0xffffffffU}) {
        ScalarNativeMachine machine;
        machine.regs[ur(source)] = value;
        for (u32 index = 160; index < 168; ++index) machine.regs[index] = 0xabc00000U + index;
        const auto before = machine.regs;
        machine.predicates = {true, false, true, false, true, false, true};
        const auto predicates = machine.predicates;
        ExecutePredicatedScalarNative(input, machine, true);
        u32 expected{};
        for (u32 bit = 0; bit < 32; ++bit) expected |= ((value >> bit) & 1U) << (31 - bit);
        REQUIRE(machine.regs[ur(destination)] == expected);
        REQUIRE(machine.regs[256 + 201] == 0x13579bdfU);
        for (u32 index = 160; index < 168; ++index) REQUIRE(machine.regs[index] == before[index]);
        REQUIRE(machine.predicates == predicates);
    }
}

TEST_CASE("Native partial gathers pack selected texels without overwriting aliased coordinates", "[native-shader]") {
    const u32 mask = GENERATE(1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 11U, 12U, 13U, 14U);
    const bool alias = GENERATE(false, true);
    const u32 count = static_cast<u32>(std::popcount(mask));
    const u32 destination = alias ? (count == 1 ? 5U : 4U) : 16U;
    const u32 second = count > 2 ? 9U : 255U;
    const std::array<Native, 2> input{{
        {{0x04007b63U | (destination << 16), 0x30800805, 0x001e0000U | (mask << 8) | second, 0}},
        VertexTransform.back()}};
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(lowered);
    REQUIRE(lowered.required_texture_buffer == 2);
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    std::vector<std::pair<u32, u32>> copies;
    u32 gathers{};
    for (const auto& inst : block.Instructions()) {
        if (inst.GetOpcode() == IR::Opcode::BoundImageGather) {
            ++gathers;
            REQUIRE(inst.Arg(0).U32() == 32);
            const auto* coordinates = inst.Arg(1).Inst();
            for (u32 i = 0; i < 2; ++i)
                REQUIRE(coordinates->Arg(i).Inst()->Arg(0).Inst()->Arg(0).Reg() == IR::Reg{4 + i});
        }
        if (inst.GetOpcode() == IR::Opcode::SetRegister && static_cast<u32>(inst.Arg(0).Reg()) < 250) {
            REQUIRE(inst.Arg(1).Inst()->GetOpcode() == IR::Opcode::GetRegister);
            copies.emplace_back(static_cast<u32>(inst.Arg(0).Reg()),
                static_cast<u32>(inst.Arg(1).Inst()->Arg(0).Reg()));
        }
    }
    REQUIRE(gathers == 1);
    std::vector<std::pair<u32, u32>> expected;
    u32 packed{};
    for (u32 channel = 0; channel < 4; ++channel) if (mask & (1U << channel)) {
        expected.emplace_back(packed < 2 ? destination + packed : second + packed - 2, 250 + channel);
        ++packed;
    }
    REQUIRE(copies == expected);
    auto invalid = input;
    invalid[0].words[2] &= ~0xf00U;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(invalid));
    invalid = input;
    invalid[0].words[1] = (invalid[0].words[1] & ~0xffU) | 250U;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(invalid));
}

TEST_CASE("Native split cancellation discards only the inner frame at a parent join", "[native-shader]") {
    std::array<Native, 16> input{{
        {{0x7945, 224, 0x03800000, 0}},
        {{0x17945, 160, 0x03800000, 0}},
        {{0x27945, 80, 0x03800000, 0}},
        {{0x20942, 0, 0x03800000, 0}},
        {{0x8947, 32, 0x03800000, 0}},
        {{0x7947, 80, 0x03800000, 0}},
        {{0x27802, 99, 0xf00, 0}},
        {{0x27941, 0, 0x03800000, 0}},
        {{0x27802, 2, 0xf00, 0}},
        {{0x7947, 16, 0x03800000, 0}},
        {{0x27802, 99, 0xf00, 0}},
        {{0x17941, 0, 0x03800000, 0}},
        {{0x37802, 3, 0xf00, 0}},
        {{0x7918, 0, 0, 0}},
        {{0x7941, 0, 0x03800000, 0}},
        VertexTransform.back(),
    }};
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(lowered);
    auto broken = input;
    broken[4].words[0] ^= 0x8000;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(broken));
    broken = input;
    broken[4].words[1] = 48; // The complementary path must reach the inner sync.
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(broken));
    broken = input;
    broken[5].words[1] = 128; // The canceled path cannot bypass its parent join.
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(broken));
    std::vector<u64> code(0x50 / 8);
    for (const auto word : lowered.instructions) {
        if ((code.size() * 8) % 32 == 0) code.push_back(0);
        code.push_back(word);
    }
    class BarrierEnvironment final : public EmptyEnvironment {
    public:
        explicit BarrierEnvironment(const std::vector<u64>& code_) : code{code_} { stage = Shader::Stage::VertexB; }
        u64 ReadInstruction(u32 address) override { return code.at(address / 8); }
        const std::vector<u64>& code;
    } env{code};
    Shader::ObjectPool<Shader::Maxwell::Flow::Block> flow_pool;
    Shader::Maxwell::Flow::CFG cfg{env, flow_pool, 0x50};
    INFO(cfg.Dot());
    const auto location = [&](u32 native) {
        const u32 offset = lowered.source_instruction_offsets[native];
        return Shader::Maxwell::Location{0x50 + offset * 8 + ((offset + 1) / 3) * 8};
    };
    for (const bool predicate : {false, true}) {
        const auto* block = &*cfg.Functions().front().blocks.begin();
        bool inner_continuation{}, parent_continuation{}, exited{};
        for (u32 step = 0; step < 64 && block; ++step) {
            if (!block->begin.IsVirtual()) {
                inner_continuation |= block->Contains(location(8));
                parent_continuation |= block->Contains(location(12));
            }
            if (block->end_class == Shader::Maxwell::Flow::EndClass::Exit) { exited = true; break; }
            const auto [pred, inverted] = block->cond.GetPred();
            REQUIRE(block->cond.GetFlowTest() == IR::FlowTest::T);
            REQUIRE((pred == IR::Pred::P0 || pred == IR::Pred::PT));
            block = ((pred == IR::Pred::PT || predicate) != inverted) ? block->branch_true : block->branch_false;
        }
        REQUIRE(exited);
        REQUIRE(inner_continuation == !predicate);
        REQUIRE(parent_continuation);
    }
}

TEST_CASE("Native uniform loads retain the full five-bit constant buffer bank", "[native-shader]") {
    const u32 bank = GENERATE(0U, 17U, 24U, 31U);
    const std::array<Native, 2> input{{
        {{0x00067ab9, (bank << 22) | 0x1600U, 0x00000a00, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    ScalarNativeMachine machine;
    machine.constants[(u64{bank} << 32) | 0x58] = 0xfffffff0;
    machine.constants[(u64{bank} << 32) | 0x5c] = 0x29;
    machine.regs[224] = 0xcafebeef;
    machine.Execute(block);
    REQUIRE(machine.regs[222] == 0xfffffff0);
    REQUIRE(machine.regs[223] == 0x29);
    REQUIRE(machine.regs[224] == 0xcafebeef);
}

TEST_CASE("Native partial gathers respect the extended uniform shadow boundary", "[native-shader]") {
    const Native uniform{{0x003e7ab9, 0, 0x800, 0}};
    const Native gather{{0x04047b63, 0x30800805, 0x001e03ff, 0}};
    const std::array<Native, 3> valid{uniform, gather, VertexTransform.back()};
    REQUIRE(Shader::Ampere::LowerToMaxwell(valid));
    for (u32 mode = 0; mode < 5; ++mode) {
        auto invalid = valid;
        switch (mode) {
        case 0: invalid[1].words[0] = (invalid[1].words[0] & 0x00ffffffU) | (168U << 24); break;
        case 1: invalid[1].words[1] = (invalid[1].words[1] & ~0xffU) | 168U; break;
        case 2: invalid[1].words[0] = (invalid[1].words[0] & ~0x00ff0000U) | (168U << 16); break;
        case 3: invalid[1].words[2] = 0x001e07a8; break;
        case 4: invalid[1].words[2] |= 0x4000; break; // Depth mode remains unsupported.
        }
        REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(invalid));
    }
}

TEST_CASE("Native register matrix transpose preserves every b16 tag and source alias", "[native-shader]") {
    const u32 destination = GENERATE(4U, 5U, 40U);
    const std::array<Native, 2> input{{{{0x0400723aU | (destination << 16), 0, 0, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    std::array<std::array<u32, IR::NUM_REGS>, 32> registers{};
    std::array<std::array<bool, 7>, 32> predicates{};
    for (u32 lane = 0; lane < 32; ++lane) {
        registers[lane].fill(0xabcdef01);
        const u32 row = lane / 4, col = (lane % 4) * 2;
        registers[lane][4] = (row * 100 + col) | ((row * 100 + col + 1) << 16);
        predicates[lane] = {true, false, true, false, true, false, true};
    }
    const auto original = registers;
    const auto original_predicates = predicates;
    ExecuteNativeWarp(block, ~0U, registers, predicates);
    for (u32 lane = 0; lane < 32; ++lane) {
        const u32 row = lane / 4, col = (lane % 4) * 2;
        REQUIRE(registers[lane][destination] == ((col * 100 + row) | (((col + 1) * 100 + row) << 16)));
        for (u32 reg = 0; reg < 240; ++reg)
            if (reg != destination) REQUIRE(registers[lane][reg] == original[lane][reg]);
    }
    REQUIRE(predicates == original_predicates);
    auto invalid = input;
    invalid[0].words[2] = 1;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(invalid));
    invalid = input;
    invalid[0].words[0] &= ~0xf000U;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(invalid));
}

TEST_CASE("Native packed half predicates retain unordered thresholds and independent outputs", "[native-shader]") {
    const bool broadcast = GENERATE(false, true);
    const std::array<Native, 2> input{{{{0x13007434, 0x292d292d, broadcast ? 0x03c6e800U : 0x03c6e000U, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    const std::array<u16, 9> halves{0, 0x8000, 0x292c, 0x292d, 0x292e, 0x7c00, 0xfc00, 0x7e01, 0xfe01};
    for (u16 low : halves) for (u16 high : halves) {
        ScalarNativeMachine machine;
        machine.regs.fill(0x12345678);
        machine.regs[19] = low | (u32{high} << 16);
        machine.predicates = {true, false, true, false, true, false, true};
        const auto original = machine.regs;
        auto expected = machine.predicates;
        const auto compare = [](u16 value) {
            const float number = DecodeTestHalf(value);
            return std::isnan(number) || number >= DecodeTestHalf(0x292d);
        };
        expected[3] = compare(low);
        expected[4] = compare(broadcast ? low : high);
        machine.Execute(block);
        REQUIRE(machine.predicates == expected);
        for (u32 reg = 0; reg < 240; ++reg) REQUIRE(machine.regs[reg] == original[reg]);
    }
    auto invalid = input;
    invalid[0].words[2] |= 0x10000;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(invalid));
}

TEST_CASE("Native integer multiply preserves negated uniform operands and wrapping addends", "[native-shader]") {
    const bool uniform_addend = GENERATE(false, true);
    const u32 destination = GENERATE(0U, 1U, 5U);
    const std::array<Native, 2> input{{
        {{(destination << 16) | (0U << 24) | (uniform_addend ? 0x7e24U : 0x7c24U),
          0x80000004, 0x0f8e0201, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    const std::array<u32, 7> values{0,1,2,0x7fffffffU,0x80000000U,0xfffffffeU,0xffffffffU};
    for (u32 uniform : values) for (u32 left : values) for (u32 right : values) {
        ScalarNativeMachine machine;
        machine.regs.fill(0x12345678);
        machine.regs[0] = left; machine.regs[1] = right; machine.regs[220] = uniform;
        machine.predicates = {true,false,true,false,true,false,true};
        auto expected = machine.regs;
        const auto original_predicates = machine.predicates;
        expected[destination] = uniform_addend ? left * right - uniform : left * (0U - uniform) + right;
        machine.Execute(block);
        for (u32 reg = 0; reg < 240; ++reg) REQUIRE(machine.regs[reg] == expected[reg]);
        REQUIRE(machine.predicates == original_predicates);
    }
}

TEST_CASE("Native VPRS index exports retain their identity separately from viewport masks", "[native-shader]") {
    const std::array<Native, 2> input{{{{0xff007322, 0x00005c04, 0xff, 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    u32 exports{};
    for (const auto& inst : block.Instructions()) if (inst.GetOpcode() == IR::Opcode::SetAttribute) {
        REQUIRE(inst.Arg(0).Attribute() == IR::Attribute::VprsTableIndex);
        REQUIRE(inst.Arg(1).Inst()->Arg(0).Inst()->Arg(0).Reg() == IR::Reg{4});
        ++exports;
    }
    REQUIRE(exports == 1);
    REQUIRE(IR::Attribute::VprsTableIndex != IR::Attribute::ViewportMask);
}

TEST_CASE("Captured native cursor vertex retains the four exact UV corners", "[.native-cursor-captured]") {
    const char* directory = std::getenv("EDEN_NATIVE_SHADER_FIXTURES");
    REQUIRE(directory != nullptr);
    const auto root = std::filesystem::path{directory};
    std::ifstream shader(root / "native-graphics-shader-1.bin", std::ios::binary | std::ios::ate);
    REQUIRE(shader.good());
    const size_t bytes = static_cast<size_t>(shader.tellg());
    REQUIRE(bytes > 128);
    std::vector<Native> source((bytes - 128) / sizeof(Native));
    shader.seekg(128);
    shader.read(reinterpret_cast<char*>(source.data()), source.size() * sizeof(Native));
    std::array<std::array<float,2>,4> inputs{};
    std::ifstream vertices(root / "vertex-input.bin", std::ios::binary);
    REQUIRE(vertices.good());
    vertices.read(reinterpret_cast<char*>(inputs.data()), sizeof(inputs));
    REQUIRE(vertices.gcount() == sizeof(inputs));
    REQUIRE(inputs == std::array<std::array<float,2>,4>{{{0,0},{1,0},{0,1},{1,1}}});
    for (const auto& input : inputs) {
        ScalarNativeMachine machine;
        machine.attributes[static_cast<u32>(IR::Attribute::Generic0X)] = std::bit_cast<u32>(input[0]);
        machine.attributes[static_cast<u32>(IR::Attribute::Generic0Y)] = std::bit_cast<u32>(input[1]);
        for (u32 bank = 0; bank < Shader::Info::MAX_CBUFS; ++bank) {
            std::ifstream constants(root / ("cbuf" + std::to_string(bank) + "-1.bin"), std::ios::binary);
            u32 value{}, offset{};
            while (constants.read(reinterpret_cast<char*>(&value), sizeof(value))) {
                machine.constants[(u64{bank} << 32) | offset] = value;
                offset += 4;
            }
        }
        ExecutePredicatedScalarNative(source, machine);
        REQUIRE(std::bit_cast<float>(machine.attributes.at(static_cast<u32>(IR::Attribute::Generic1X))) == input[0]);
        REQUIRE(std::bit_cast<float>(machine.attributes.at(static_cast<u32>(IR::Attribute::Generic1Y))) == input[1]);
        // Captured camera projection uses w=-view_z, with a planar UI transform.
        REQUIRE(std::bit_cast<float>(machine.constants.at((u64{3} << 32) | 0x38)) == -1.0f);
        const float camera_depth = -std::bit_cast<float>(machine.constants.at((u64{3} << 32) | 0x6c));
        REQUIRE(std::bit_cast<float>(machine.attributes.at(static_cast<u32>(IR::Attribute::PositionW))) == camera_depth);
    }
}

TEST_CASE("Captured native intro vertex preserves visible clip positions", "[.native-intro-position]") {
    const char* directory = std::getenv("EDEN_NATIVE_SHADER_FIXTURES");
    REQUIRE(directory != nullptr);
    const auto root = std::filesystem::path{directory};
    std::ifstream shader(root / "native-graphics-shader-1.bin", std::ios::binary | std::ios::ate);
    REQUIRE(shader.good());
    const size_t bytes = static_cast<size_t>(shader.tellg());
    REQUIRE(bytes > 128);
    std::vector<Native> source((bytes - 128) / sizeof(Native));
    shader.seekg(128);
    shader.read(reinterpret_cast<char*>(source.data()), source.size() * sizeof(Native));
    std::array<std::array<float, 3>, 3> inputs{};
    std::ifstream vertices(root / "vertex-input.bin", std::ios::binary);
    vertices.read(reinterpret_cast<char*>(inputs.data()), sizeof(inputs));
    REQUIRE(vertices.gcount() == sizeof(inputs));
    for (const auto& input : inputs) {
        ScalarNativeMachine machine;
        for (u32 component = 0; component < 3; ++component)
            machine.attributes[static_cast<u32>(IR::Attribute::Generic0X) + component] = std::bit_cast<u32>(input[component]);
        for (u32 bank : {3U, 4U}) {
            std::ifstream constants(root / ("cbuf" + std::to_string(bank) + "-1.bin"), std::ios::binary);
            REQUIRE(constants.good());
            u32 value{}, offset{};
            while (constants.read(reinterpret_cast<char*>(&value), sizeof(value))) {
                machine.constants[(u64{bank} << 32) | offset] = value;
                offset += 4;
            }
        }
        const auto matrix = [&](u32 bank, u32 offset, const std::array<double, 4>& value) {
            std::array<double, 4> result{};
            for (u32 row = 0; row < (bank == 4 || offset == 0 ? 3U : 4U); ++row)
                for (u32 column = 0; column < 4; ++column)
                    result[row] += value[column] * std::bit_cast<float>(machine.constants.at((u64{bank} << 32) | (offset + row * 16 + column * 4)));
            if (bank == 4 || offset == 0) result[3] = 1.0;
            return result;
        };
        const auto expected = matrix(3, 0x70, matrix(3, 0, matrix(4, 0, {input[0], input[1], input[2], 1.0})));
        ExecutePredicatedScalarNative(source, machine);
        for (u32 component = 0; component < 4; ++component) {
            const float actual = std::bit_cast<float>(machine.attributes.at(static_cast<u32>(IR::Attribute::PositionX) + component));
            REQUIRE(std::abs(actual - expected[component]) < 0.0001 * std::max(1.0, std::abs(expected[component])));
        }
        REQUIRE(expected[3] > 0.0);
        REQUIRE(std::abs(expected[0]) < expected[3]);
        REQUIRE(std::abs(expected[1]) < expected[3]);
        REQUIRE(expected[2] > 0.0);
        REQUIRE(expected[2] < expected[3]);
    }
}

TEST_CASE("Native 2D mipmap query preserves selected channel coordinates and descriptor", "[native-shader]") {
    const u32 mask = GENERATE(1U, 2U);
    const u32 destination = GENERATE(0U, 4U, 20U);
    const std::array<Native, 2> input{{{{0x04007b69U | (destination << 16),
        0x208008ff, 0x000000ffU | (mask << 8), 0x100f4200}}, VertexTransform.back()}};
    const auto lowered = Shader::Ampere::LowerToMaxwell(input);
    REQUIRE(lowered);
    REQUIRE(lowered.required_texture_buffer == 2);
    REQUIRE(lowered.instructions.front() == (0xdf58008020070400ULL | destination | (u64{mask} << 31)));
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    u32 queries{}, writes{};
    for (const auto& inst : block.Instructions()) {
        if (inst.GetOpcode() == IR::Opcode::BoundImageQueryLod) {
            REQUIRE(inst.Arg(0).U32() == 32);
            REQUIRE(inst.Flags<IR::TextureInstInfo>().type.Value() == Shader::TextureType::Color2D);
            for (u32 i = 0; i < 2; ++i)
                REQUIRE(inst.Arg(1).Inst()->Arg(i).Inst()->Arg(0).Inst()->Arg(0).Reg() == IR::Reg{4 + i});
            ++queries;
        }
        if (inst.GetOpcode() == IR::Opcode::CompositeExtractF32x4) {
            REQUIRE(inst.Arg(1).U32() == ((mask - 1) ^ 1U));
        }
        if (inst.GetOpcode() == IR::Opcode::SetRegister) {
            REQUIRE(inst.Arg(0).Reg() == IR::Reg{destination});
            ++writes;
        }
    }
    REQUIRE(queries == 1);
    REQUIRE(writes == 1);
}

TEST_CASE("Native mipmap queries reject unsupported modes and descriptor bank conflicts", "[native-shader]") {
    const Native query{{0x04007b69, 0x208008ff, 0x000002ff, 0x100f4200}};
    for (const auto [word, bit] : std::array<std::pair<u32,u32>, 7>{{{1,0}, {1,27}, {1,28}, {1,30}, {2,0}, {2,12}, {2,27}}}) {
        auto bad = query; bad.words[word] ^= 1U << bit;
        REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(std::array{bad, VertexTransform.back()}));
    }
    for (u32 mask : {0U, 3U, 4U, 8U, 15U}) {
        auto bad = query; bad.words[2] = 255 | (mask << 8);
        REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(std::array{bad, VertexTransform.back()}));
    }
    auto different_bank = query; different_bank.words[1] ^= 1U << 22;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(std::array{query, different_bank, VertexTransform.back()}));
    auto predicated = query; predicated.words[0] = (predicated.words[0] & ~0xf000U) | 0x9000;
    const auto lowered = Shader::Ampere::LowerToMaxwell(std::array{predicated, VertexTransform.back()});
    REQUIRE(lowered);
    const auto predicate = Shader::Maxwell::Instruction{lowered.instructions.front()}.Pred();
    REQUIRE(predicate.index == 1);
    REQUIRE(predicate.negated);
}

TEST_CASE("Native mipmap query encodes signed relative and unsigned absolute fractional LOD", "[native-shader]") {
    const u32 mask = GENERATE(1U, 2U);
    const std::array<Native, 2> input{{{{0x04007b69, 0x208008ff, 255U | (mask << 8), 0}}, VertexTransform.back()}};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    for (const auto [absolute, relative] : std::array<std::pair<float,float>, 5>{{
        {2.75f, -1.5f}, {0.0f, -0.00390625f}, {0.00390625f, 0.5f},
        {15.99609375f, 8.25f}, {3.25f, 2.75f}}}) {
        const auto evaluate = [&](auto&& self, const IR::Value& value) -> u32 {
            if (value.IsImmediate()) return value.Type() == IR::Type::F32 ? std::bit_cast<u32>(value.F32()) : value.U32();
            const auto& inst = *value.Inst();
            const auto arg = [&](size_t i) { return self(self, inst.Arg(i)); };
            switch (inst.GetOpcode()) {
            case IR::Opcode::CompositeExtractF32x4:
                REQUIRE(inst.Arg(0).Inst()->GetOpcode() == IR::Opcode::BoundImageQueryLod);
                return std::bit_cast<u32>(inst.Arg(1).U32() == 0 ? absolute : relative);
            case IR::Opcode::FPMul32:
                return std::bit_cast<u32>(std::bit_cast<float>(arg(0)) * std::bit_cast<float>(arg(1)));
            case IR::Opcode::ConvertU32F32: return static_cast<u32>(std::bit_cast<float>(arg(0)));
            case IR::Opcode::ConvertS32F32: return static_cast<u32>(static_cast<s32>(std::bit_cast<float>(arg(0))));
            case IR::Opcode::BitwiseAnd32: return arg(0) & arg(1);
            default: throw std::logic_error("unexpected LOD encoding operation");
            }
        };
        u32 writes{};
        for (const auto& inst : block.Instructions()) {
            if (inst.GetOpcode() != IR::Opcode::SetRegister) continue;
            const u32 bits = evaluate(evaluate, inst.Arg(1));
            const float decoded = mask == 1 ? static_cast<float>(static_cast<s16>(bits)) / 256.0f
                                            : static_cast<float>(bits) / 256.0f;
            REQUIRE(decoded == (mask == 1 ? relative : absolute));
            ++writes;
        }
        REQUIRE(writes == 1);
    }
}


TEST_CASE("Native mip count queries preserve aliased input and scalar output", "[native-shader]") {
    const u32 dest = GENERATE(0U, 3U, 249U);
    const std::array<Native, 2> source{{
        {{0x00007b6fU | (dest << 16) | (dest << 24), 0x00800800, 0x000008ff, 0x100f4200}},
        VertexTransform.back(),
    }};
    const auto lowered = Shader::Ampere::LowerToMaxwell(source);
    REQUIRE(lowered);
    REQUIRE(lowered.required_texture_buffer == 2);
    REQUIRE(Shader::Maxwell::Decode(lowered.instructions[0]) == Shader::Maxwell::Opcode::TXQ);
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    EmptyEnvironment env;
    Shader::Maxwell::TranslatorVisitor visitor{env, block};
    visitor.TXQ(lowered.instructions[0]);
    u32 queries = 0, writes = 0;
    for (const auto& inst : block.Instructions()) {
        if (inst.GetOpcode() == IR::Opcode::BoundImageQueryDimensions) {
            ++queries;
            REQUIRE(inst.Arg(0).U32() == 32);
            REQUIRE(inst.Arg(1).Inst()->Arg(0).Reg() == IR::Reg{dest});
            REQUIRE_FALSE(inst.Arg(2).U1());
        }
        if (inst.GetOpcode() == IR::Opcode::SetRegister) {
            ++writes;
            REQUIRE(inst.Arg(0).Reg() == IR::Reg{dest});
            const auto* extract = inst.Arg(1).Inst();
            REQUIRE(extract->GetOpcode() == IR::Opcode::CompositeExtractU32x4);
            REQUIRE(extract->Arg(1).U32() == 3);
        }
    }
    REQUIRE(queries == 1);
    REQUIRE(writes == 1);
    auto invalid = source;
    invalid[0].words[2] = 0x00000800;
    REQUIRE_FALSE(Shader::Ampere::LowerToMaxwell(invalid));
}

TEST_CASE("Heterogeneous buffer texture records require a proven matching tag branch", "[native-shader]") {
    const u32 mode = GENERATE(0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U);
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block entry{pool}, root{pool}, sample{pool}, exit{pool};
    entry.AddBranch(&root);
    if (mode == 4) entry.AddBranch(&sample);
    root.AddBranch(&sample);
    root.AddBranch(&exit);
    sample.AddBranch(&exit);
    if (mode == 6) sample.AddBranch(&sample);
    IR::IREmitter ir{root};
    const auto index = ir.GetReg(IR::Reg::R1);
    const auto pointer = ir.GetCbuf(ir.Imm32(0U), ir.Imm32(0x510U));
    const auto load_field = [&](IR::U32 record, u32 field, IR::Opcode opcode) {
        const auto relative = ir.IAdd(ir.IMul(record, ir.Imm32(160U)), ir.Imm32(field));
        const auto offset = ir.ISub(ir.IAdd(pointer, relative), ir.BitwiseAnd(pointer, ir.Imm32(0xfffffff0U)));
        const auto load = root.PrependNewInst(root.end(), opcode, {ir.Imm32(0U), offset});
        return IR::Value{&*load};
    };
    const IR::U32 handle{ir.CompositeExtract(load_field(index, 152U, IR::Opcode::LoadStorage64), 0)};
    IR::U32 tag{load_field(mode == 3 ? ir.GetReg(IR::Reg::R2) : index, 144U, IR::Opcode::LoadStorage32)};
    if (mode == 2) tag = ir.BitwiseAnd(tag, ir.Imm32(7U));
    auto condition = mode == 1 ? ir.INotEqual(tag, ir.Imm32(1U)) : ir.IEqual(tag, ir.Imm32(1U));
    if (mode == 7) condition = ir.GetPred(IR::Pred::P0);
    if (mode >= 8) {
        const auto slot = ir.ShiftLeftLogical(ir.UMin(ir.ISub(tag, ir.Imm32(1U)), ir.Imm32(4U)), ir.Imm32(2U));
        const auto target = ir.GetCbuf(ir.Imm32(1U), ir.IAdd(slot, ir.Imm32(0xf0U)));
        condition = ir.IEqual(target, ir.Imm32(0x100U));
    }
    const auto reference = ir.ConditionRef(condition);
    IR::Program program;
    program.blocks = {&entry, &root, &sample, &exit};
    program.info.storage_buffers_descriptors.push_back({0, 0x510, 1, false});
    IR::AbstractSyntaxNode branch;
    branch.type = IR::AbstractSyntaxNode::Type::If;
    branch.data.if_node = {reference, &sample, &exit};
    program.syntax_list = {branch};
    Shader::Optimization::BufferTexture::Tracker tracker{program};
    const auto source = tracker.Find(handle);
    REQUIRE(source);
    const auto exclusions = tracker.Exclusions(*source, mode == 5 ? &root : &sample,
        [&](u32 bank, u32 offset) -> std::optional<u32> {
            if (mode != 8 || bank != 1 || offset < 0xf0 || offset > 0x100 || (offset & 3)) return {};
            return offset == 0xf0 ? 0x100U : offset == 0xf4 ? 0x200U : 0x300U;
        });
    const auto count = std::ranges::count_if(exclusions, [](const auto& guard) { return guard.mask != 0; });
    if (mode <= 2) {
        REQUIRE(count == 1);
        const auto& guard = exclusions[0];
        REQUIRE(guard.offset == 144);
        REQUIRE(guard.mask == (mode == 2 ? 7U : ~0U));
        REQUIRE(guard.not_equal == (mode != 1));
        REQUIRE(guard.Matches(2U) == (mode != 1));
        REQUIRE(guard.Matches(1U) == (mode == 1));
        if (mode == 2) REQUIRE_FALSE(guard.Matches(0x80000001U));
    } else if (mode == 8) {
        REQUIRE(count > 0);
        const auto excluded = [&](u32 tag_value) {
            return std::ranges::any_of(exclusions, [&](const auto& guard) { return guard.Matches(tag_value); });
        };
        REQUIRE(excluded(2U));
        REQUIRE_FALSE(excluded(1U));
        REQUIRE_FALSE(excluded(0x100U)); // Unproved tags retain strict binding validation.
    } else {
        REQUIRE(count == 0);
    }
}

TEST_CASE("Native global pointers do not infer buffer length from adjacent constants", "[native-gpu][storage-ranges]") {
    constexpr u64 mapped = 0x708000;
    // Captured DLSS feature output pointer at CB0+0x2d8 is followed by TIC266.
    // The last workgroup accesses megabytes of features, not a 266-byte descriptor.
    for (u64 following : {u64{0}, u64{1}, u64{266}, u64{1280}, u64{0x8110000000}}) {
        REQUIRE(VideoCommon::StorageBufferSize(mapped, following, true) == mapped);
    }
    REQUIRE(VideoCommon::StorageBufferSize(0, 266, true) == 0);
    REQUIRE(VideoCommon::StorageBufferSize(128, 266, true) == 128);
    REQUIRE(VideoCommon::StorageBufferSize(u64{1} << 32, 266, true) == 8 * 1024 * 1024);
    REQUIRE(VideoCommon::StorageBufferSize(mapped, 266, false) == 266);
    REQUIRE(VideoCommon::StorageBufferSize(mapped, 0, false) == mapped);
    REQUIRE(VideoCommon::StorageBufferSize(mapped, mapped + 1, false) == mapped);
    REQUIRE(VideoCommon::StorageBufferSize(mapped, 0x8110000000, false) == mapped);
}

TEST_CASE("Native raw storage pointers do not inherit NVN descriptor spacing", "[native-shader]") {
    const u32 pointer_offset = GENERATE(0x168U, 0x178U, 0x198U, 0x208U, 0x258U, 0x2d8U);
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    IR::IREmitter ir{block};
    const auto pointer = ir.GetCbuf(ir.Imm32(0U), ir.Imm32(pointer_offset));
    const auto high = ir.GetCbuf(ir.Imm32(0U), ir.Imm32(pointer_offset + 4));
    const auto stride = ir.GetCbuf(ir.Imm32(0U), ir.Imm32(0x300U));
    const auto relative = ir.IMul(ir.GetReg(IR::Reg::R1), stride);
    const auto address = ir.PackUint2x32(ir.CompositeConstruct(ir.IAdd(pointer, relative), high));
    (void)ir.LoadGlobal32(address);
    IR::Program program;
    program.blocks = {&block};
    program.post_order_blocks = {&block};
    EmptyEnvironment env;
    Shader::HostTranslateInfo host;
    Shader::Optimization::GlobalMemoryToStorageBufferPass(env, program, host);
    REQUIRE(program.info.storage_buffers_descriptors.size() == 1);
    REQUIRE(program.info.storage_buffers_descriptors[0].cbuf_index == 0);
    REQUIRE(program.info.storage_buffers_descriptors[0].cbuf_offset == pointer_offset);
}

TEST_CASE("Native pointer tracing rejects a nearer additive stride", "[native-shader]") {
    class PointerEnvironment final : public EmptyEnvironment {
    public:
        bool IsCbufStorageBufferPointer(u32 bank, u32 offset) override {
            return bank == 0 && offset == 0x168;
        }
    } env;
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    IR::IREmitter ir{block};
    const auto pointer = ir.GetCbuf(ir.Imm32(0U), ir.Imm32(0x168U));
    const auto stride = ir.GetCbuf(ir.Imm32(0U), ir.Imm32(0x200U));
    const auto high = ir.GetCbuf(ir.Imm32(0U), ir.Imm32(0x16cU));
    const auto low = ir.IAdd(ir.IAdd(pointer, ir.GetReg(IR::Reg::R1)), stride);
    (void)ir.LoadGlobal32(ir.PackUint2x32(ir.CompositeConstruct(low, high)));
    IR::Program program;
    program.blocks = {&block};
    program.post_order_blocks = {&block};
    Shader::HostTranslateInfo host;
    Shader::Optimization::GlobalMemoryToStorageBufferPass(env, program, host);
    REQUIRE(program.info.storage_buffers_descriptors.size() == 1);
    REQUIRE(program.info.storage_buffers_descriptors[0].cbuf_offset == 0x168);
    for (const auto& inst : block.Instructions()) REQUIRE(inst.GetOpcode() != IR::Opcode::LoadGlobal32);
}

TEST_CASE("Native storage pointer classifications survive shader cache serialization", "[native-shader]") {
    class CacheEnvironment final : public VideoCommon::GenericEnvironment {
    public:
        CacheEnvironment() {
            stage = Shader::Stage::Compute;
            code = {0};
            cached_lowest = cached_highest = 0;
            storage_pointer_types.emplace(0x168, 1);
            storage_pointer_types.emplace(0x200, 0);
            cbuf_values.emplace(0x168, 0x12340000);
            workgroup_size = {32, 2, 1};
            shared_memory_size = 4096;
        }
        u32 ReadCbufValue(u32, u32) override { return 0; }
        Shader::TextureType ReadTextureType(u32) override { return {}; }
        Shader::TexturePixelFormat ReadTexturePixelFormat(u32) override { return {}; }
        bool IsTexturePixelFormatInteger(u32) override { return false; }
        u32 ReadViewportTransformState() override { return 1; }
        std::optional<Shader::ReplaceConstant> GetReplaceConstBuffer(u32, u32) override { return {}; }
    } env;
    const auto path = std::filesystem::temp_directory_path() / "eden-native-storage-pointer-cache-test.bin";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        REQUIRE(output.good());
        env.Serialize(output);
        REQUIRE(output.good());
    }
    VideoCommon::FileEnvironment restored;
    {
        std::ifstream input(path, std::ios::binary);
        restored.Deserialize(input);
        REQUIRE(input.good());
    }
    std::filesystem::remove(path);
    REQUIRE(restored.IsCbufStorageBufferPointer(0, 0x168));
    REQUIRE_FALSE(restored.IsCbufStorageBufferPointer(0, 0x200));
    REQUIRE_THROWS_AS(restored.IsCbufStorageBufferPointer(0, 0x208), Shader::LogicError);
    REQUIRE(restored.ReadCbufValue(0, 0x168) == 0x12340000);
    REQUIRE(restored.WorkgroupSize() == std::array<u32, 3>{32, 2, 1});
    REQUIRE(restored.SharedMemorySize() == 4096);
}

TEST_CASE("Native half matrix rounding matches measured hardware fragments", "[native-shader]") {
    const u32 depth = GENERATE(8U, 16U);
    // RTX 4090 driver-API MMA samples, seed zero with a nonzero C fragment.
    static constexpr std::array<std::array<u32, 64>, 2> hardware{{
        {{
            0x42af42edU, 0xc05ec08fU, 0x42334271U, 0xbff4c02cU, 0x41b641f5U, 0xbf2dbf90U, 0x413a4178U, 0xbe66bec9U,
            0x414e417eU, 0xc1bfc1ffU, 0x40ed411dU, 0xc140c17fU, 0x408d40bdU, 0xc0c0c100U, 0x402c405cU, 0xc040c080U,
            0x3fd8400eU, 0xc01dc042U, 0x3f4f3f94U, 0xbfa7bff0U, 0x3ec63f0bU, 0xbf13bf5dU, 0x3e3e3e82U, 0xbe80bec9U,
            0x3d153d3dU, 0x32de338dU, 0x3cc43cecU, 0x318d3234U, 0x3c733c9cU, 0x303630dfU, 0x3c223c4aU, 0x2dbf2f14U,
            0x38a438bcU, 0x42a742e1U, 0x3872388bU, 0x4233426dU, 0x38403859U, 0x41be41f9U, 0x380e3827U, 0x414a4184U,
            0xaf15b008U, 0x41454171U, 0xad24ae1cU, 0x40ed4119U, 0xaa63ac2bU, 0x409540c1U, 0xa4fea870U, 0x403d4069U,
            0xba69bac0U, 0x3fc84002U, 0xb9bbba12U, 0x3f4f3f8bU, 0xb90cb964U, 0x3ed73f13U, 0xb85eb8b5U, 0x3e5e3e9aU,
            0xbdf8be3fU, 0x3d043d24U, 0xbd69bdb0U, 0x3cc43ce4U, 0xbcd9bd21U, 0x3c833ca4U, 0xbc4abc92U, 0x3c433c63U,
        }},
        {{
            0x43d6441bU, 0xc207c249U, 0x43164376U, 0xc183c1c5U, 0x425542b6U, 0xc0fec140U, 0x419441f5U, 0xc07ac0bcU,
            0x42114255U, 0xc1cdc1edU, 0x418841ccU, 0xc18cc1acU, 0x40ff4144U, 0xc14ac16bU, 0x407640baU, 0xc108c129U,
            0x404b4073U, 0xbc95bc6aU, 0x3ff44023U, 0xbceabcbfU, 0x3f523fa3U, 0xbd3ebd14U, 0x3eb13f02U, 0xbd94bd69U,
            0x3d0a3d22U, 0x3b713c26U, 0x3cd83cf0U, 0x39bb3a96U, 0x3ca63cc0U, 0x380538e0U, 0x3c753c8dU, 0x349f3654U,
            0x35f83579U, 0x43b14405U, 0x36f13675U, 0x43014359U, 0x37eb376eU, 0x425042a9U, 0x38723834U, 0x41a041f8U,
            0xb81cb8cbU, 0x41eb4227U, 0xb57eb6dbU, 0x417241aeU, 0xb189b422U, 0x40fa4136U, 0x995cad9eU, 0x408240beU,
            0xbd9abe29U, 0x40254045U, 0xbc7bbd0bU, 0x3fc94005U, 0xbab9bbd9U, 0x3f493f89U, 0xb87db99bU, 0x3ec83f08U,
            0xc093c0f7U, 0x3cbe3cc6U, 0xbf98c02fU, 0x3cad3cb6U, 0xbe08bed0U, 0x3c9c3ca5U, 0xbc7abd42U, 0x3c8c3c94U,
        }}
    }};
    const std::array<Native, 2> input{{
        {{(8U << 24) | (32U << 16) | 0x723cU, 16, (depth == 16 ? 0x800U : 0U) | 24U, 0}},
        VertexTransform.back(),
    }};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    TranslateScalarNative(input, block);
    const auto a = [](u32 row, u32 k) { return (static_cast<int>((row * 17 + k * 5) % 199) - 99) * 0.00731; };
    const auto b = [](u32 k, u32 col) { return (static_cast<int>((k * 7 + col * 3) % 211) - 105) * 0.00917; };
    const auto c = [](u32 row, u32 col) { return (static_cast<int>((row * 5 + col * 3) % 97) - 48) * 0.00319; };
    const auto pack = [](double low, double high) { return u32{EncodeTestHalf(low)} | (u32{EncodeTestHalf(high)} << 16); };
    std::array<std::array<u32, IR::NUM_REGS>, 32> registers{};
    std::array<std::array<bool, 7>, 32> predicates{};
    for (u32 lane = 0; lane < 32; ++lane) {
        const u32 group = lane / 4, thread = lane % 4;
        for (u32 fragment = 0; fragment < depth / 4; ++fragment) {
            const u32 row = group + (fragment % 2) * 8, k = thread * 2 + (fragment / 2) * 8;
            registers[lane][8 + fragment] = pack(a(row, k), a(row, k + 1));
        }
        for (u32 fragment = 0; fragment < 2; ++fragment) {
            const u32 k = thread * 2 + fragment * 8;
            if (fragment < depth / 8) registers[lane][16 + fragment] = pack(b(k, group), b(k + 1, group));
            registers[lane][24 + fragment] = pack(c(group + fragment * 8, thread * 2), c(group + fragment * 8, thread * 2 + 1));
        }
    }
    ExecuteNativeWarp(block, 0xffffffffU, registers, predicates);
    for (u32 lane = 0; lane < 32; ++lane) for (u32 fragment = 0; fragment < 2; ++fragment) {
        INFO("depth=" << depth << " lane=" << lane << " fragment=" << fragment);
        REQUIRE(registers[lane][32 + fragment] == hardware[depth == 16][lane * 2 + fragment]);
    }
}
