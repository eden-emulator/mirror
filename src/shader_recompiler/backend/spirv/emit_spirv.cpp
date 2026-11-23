// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "common/settings.h"
#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/backend/spirv/emit_spirv_instructions.h"
#include "shader_recompiler/backend/spirv/spirv_emit_context.h"
#include "shader_recompiler/frontend/ir/basic_block.h"
#include "shader_recompiler/frontend/ir/program.h"

namespace Shader::Backend::SPIRV {
namespace {
template <class Func>
struct FuncTraits {};

template <class ReturnType_, class... Args>
struct FuncTraits<ReturnType_ (*)(Args...)> {
    using ReturnType = ReturnType_;

    static constexpr size_t NUM_ARGS = sizeof...(Args);

    template <size_t I>
    using ArgType = std::tuple_element_t<I, std::tuple<Args...>>;
};

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4702) // Ignore unreachable code warning
#endif

template <auto func, typename... Args>
void SetDefinition(EmitContext& ctx, IR::Inst* inst, Args... args) {
    inst->SetDefinition<Id>(func(ctx, std::forward<Args>(args)...));
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif

template <typename ArgType>
ArgType Arg(EmitContext& ctx, const IR::Value& arg) {
    if constexpr (std::is_same_v<ArgType, Id>) {
        return ctx.Def(arg);
    } else if constexpr (std::is_same_v<ArgType, const IR::Value&>) {
        return arg;
    } else if constexpr (std::is_same_v<ArgType, u32>) {
        return arg.U32();
    } else if constexpr (std::is_same_v<ArgType, IR::Attribute>) {
        return arg.Attribute();
    } else if constexpr (std::is_same_v<ArgType, IR::Patch>) {
        return arg.Patch();
    } else if constexpr (std::is_same_v<ArgType, IR::Reg>) {
        return arg.Reg();
    }
}

template <auto func, bool is_first_arg_inst, size_t... I>
void Invoke(EmitContext& ctx, IR::Inst* inst, std::index_sequence<I...>) {
    using Traits = FuncTraits<decltype(func)>;
    if constexpr (std::is_same_v<typename Traits::ReturnType, Id>) {
        if constexpr (is_first_arg_inst) {
            SetDefinition<func>(
                ctx, inst, inst,
                Arg<typename Traits::template ArgType<I + 2>>(ctx, inst->Arg(I))...);
        } else {
            SetDefinition<func>(
                ctx, inst, Arg<typename Traits::template ArgType<I + 1>>(ctx, inst->Arg(I))...);
        }
    } else {
        if constexpr (is_first_arg_inst) {
            func(ctx, inst, Arg<typename Traits::template ArgType<I + 2>>(ctx, inst->Arg(I))...);
        } else {
            func(ctx, Arg<typename Traits::template ArgType<I + 1>>(ctx, inst->Arg(I))...);
        }
    }
}

template <auto func>
void Invoke(EmitContext& ctx, IR::Inst* inst) {
    using Traits = FuncTraits<decltype(func)>;
    static_assert(Traits::NUM_ARGS >= 1, "Insufficient arguments");
    if constexpr (Traits::NUM_ARGS == 1) {
        Invoke<func, false>(ctx, inst, std::make_index_sequence<0>{});
    } else {
        using FirstArgType = typename Traits::template ArgType<1>;
        static constexpr bool is_first_arg_inst = std::is_same_v<FirstArgType, IR::Inst*>;
        using Indices = std::make_index_sequence<Traits::NUM_ARGS - (is_first_arg_inst ? 2 : 1)>;
        Invoke<func, is_first_arg_inst>(ctx, inst, Indices{});
    }
}

void EmitInst(EmitContext& ctx, IR::Inst* inst) {
    switch (inst->GetOpcode()) {
#define OPCODE(name, result_type, ...)                                                             \
    case IR::Opcode::name:                                                                         \
        return Invoke<&Emit##name>(ctx, inst);
#include "shader_recompiler/frontend/ir/opcodes.inc"
#undef OPCODE
    }
    throw LogicError("Invalid opcode {}", inst->GetOpcode());
}

Id TypeId(const EmitContext& ctx, IR::Type type) {
    switch (type) {
    case IR::Type::Void:
        return ctx.void_id;
    case IR::Type::U1:
        return ctx.U1;
    case IR::Type::U32:
        return ctx.U32[1];
    case IR::Type::U64:
        return ctx.U64;
    case IR::Type::U32x2:
        return ctx.U32[2];
    case IR::Type::U32x4:
        return ctx.U32[4];
    case IR::Type::F16x2:
        return ctx.F16[2];
    case IR::Type::F32:
        return ctx.F32[1];
    case IR::Type::F32x2:
        return ctx.F32[2];
    default:
        throw NotImplementedException("Phi node type {}", type);
    }
}

void Traverse(EmitContext& ctx, IR::Program& program) {
    IR::Block* current_block{};
    for (const IR::AbstractSyntaxNode& node : program.syntax_list) {
        switch (node.type) {
        case IR::AbstractSyntaxNode::Type::Block: {
            const Id label{node.data.block->Definition<Id>()};
            if (current_block) {
                ctx.OpBranch(label);
            }
            current_block = node.data.block;
            ctx.AddLabel(label);
            for (IR::Inst& inst : node.data.block->Instructions()) {
                const auto opcode{inst.GetOpcode()};
                if (ctx.profile.support_large_storage_buffers &&
                    ((opcode >= IR::Opcode::LoadStorageU8 &&
                      opcode <= IR::Opcode::WriteStorage128) ||
                     (opcode >= IR::Opcode::StorageAtomicIAdd32 &&
                      opcode <= IR::Opcode::StorageAtomicMaxF32x2))) {
                    const Id function{ctx.storage_access_functions.at({opcode, inst.Arg(0).U32()})};
                    std::array<Id, 2> arguments{};
                    for (size_t i = 1; i < inst.NumArgs(); ++i) {
                        arguments[i - 1] = ctx.Def(inst.Arg(i));
                    }
                    const Id result{
                        ctx.OpFunctionCall(TypeId(ctx, inst.Type()), function,
                                           std::span(arguments.data(), inst.NumArgs() - 1))};
                    if (inst.Type() != IR::Type::Void) {
                        inst.SetDefinition(result);
                    }
                } else {
                    EmitInst(ctx, &inst);
                }
            }
            break;
        }
        case IR::AbstractSyntaxNode::Type::If: {
            const Id if_label{node.data.if_node.body->Definition<Id>()};
            const Id endif_label{node.data.if_node.merge->Definition<Id>()};
            ctx.OpSelectionMerge(endif_label, spv::SelectionControlMask::MaskNone);
            ctx.OpBranchConditional(ctx.Def(node.data.if_node.cond), if_label, endif_label);
            break;
        }
        case IR::AbstractSyntaxNode::Type::Loop: {
            const Id body_label{node.data.loop.body->Definition<Id>()};
            const Id continue_label{node.data.loop.continue_block->Definition<Id>()};
            const Id endloop_label{node.data.loop.merge->Definition<Id>()};

            ctx.OpLoopMerge(endloop_label, continue_label, spv::LoopControlMask::MaskNone);
            ctx.OpBranch(body_label);
            break;
        }
        case IR::AbstractSyntaxNode::Type::Break: {
            const Id break_label{node.data.break_node.merge->Definition<Id>()};
            const Id skip_label{node.data.break_node.skip->Definition<Id>()};
            ctx.OpBranchConditional(ctx.Def(node.data.break_node.cond), break_label, skip_label);
            break;
        }
        case IR::AbstractSyntaxNode::Type::EndIf:
            if (current_block) {
                ctx.OpBranch(node.data.end_if.merge->Definition<Id>());
            }
            break;
        case IR::AbstractSyntaxNode::Type::Repeat: {
            Id cond{ctx.Def(node.data.repeat.cond)};
            if (!Settings::values.disable_shader_loop_safety_checks) {
                const Id pointer_type{ctx.TypePointer(spv::StorageClass::Private, ctx.U32[1])};
                const Id safety_counter{ctx.AddGlobalVariable(
                    pointer_type, spv::StorageClass::Private, ctx.Const(0x2000u))};
                if (ctx.profile.supported_spirv >= 0x00010400) {
                    ctx.interfaces.push_back(safety_counter);
                }
                const Id old_counter{ctx.OpLoad(ctx.U32[1], safety_counter)};
                const Id new_counter{ctx.OpISub(ctx.U32[1], old_counter, ctx.Const(1u))};
                ctx.OpStore(safety_counter, new_counter);

                const Id safety_cond{
                    ctx.OpSGreaterThanEqual(ctx.U1, new_counter, ctx.u32_zero_value)};
                cond = ctx.OpLogicalAnd(ctx.U1, cond, safety_cond);
            }
            const Id loop_header_label{node.data.repeat.loop_header->Definition<Id>()};
            const Id merge_label{node.data.repeat.merge->Definition<Id>()};
            ctx.OpBranchConditional(cond, loop_header_label, merge_label);
            break;
        }
        case IR::AbstractSyntaxNode::Type::Return:
            ctx.OpReturn();
            break;
        case IR::AbstractSyntaxNode::Type::Unreachable:
            ctx.OpUnreachable();
            break;
        }
        if (node.type != IR::AbstractSyntaxNode::Type::Block) {
            current_block = nullptr;
        }
    }
}

Id DefineMain(EmitContext& ctx, IR::Program& program) {
    const Id void_function{ctx.TypeFunction(ctx.void_id)};
    const Id main{ctx.OpFunction(ctx.void_id, spv::FunctionControlMask::MaskNone, void_function)};
    for (IR::Block* const block : program.blocks) {
        block->SetDefinition(ctx.OpLabel());
    }
    Traverse(ctx, program);
    ctx.OpFunctionEnd();
    return main;
}

spv::ExecutionMode ExecutionMode(TessPrimitive primitive) {
    switch (primitive) {
    case TessPrimitive::Isolines:
        return spv::ExecutionMode::Isolines;
    case TessPrimitive::Triangles:
        return spv::ExecutionMode::Triangles;
    case TessPrimitive::Quads:
        return spv::ExecutionMode::Quads;
    }
    throw InvalidArgument("Tessellation primitive {}", primitive);
}

spv::ExecutionMode ExecutionMode(TessSpacing spacing) {
    switch (spacing) {
    case TessSpacing::Equal:
        return spv::ExecutionMode::SpacingEqual;
    case TessSpacing::FractionalOdd:
        return spv::ExecutionMode::SpacingFractionalOdd;
    case TessSpacing::FractionalEven:
        return spv::ExecutionMode::SpacingFractionalEven;
    }
    throw InvalidArgument("Tessellation spacing {}", spacing);
}

void DefineEntryPoint(const IR::Program& program, EmitContext& ctx, Id main) {
    const std::span interfaces(ctx.interfaces.data(), ctx.interfaces.size());
    spv::ExecutionModel execution_model{};
    switch (program.stage) {
    case Stage::Compute: {
        const std::array<u32, 3> workgroup_size{program.workgroup_size};
        execution_model = spv::ExecutionModel::GLCompute;
        ctx.AddExecutionMode(main, spv::ExecutionMode::LocalSize, workgroup_size[0],
                             workgroup_size[1], workgroup_size[2]);
        break;
    }
    case Stage::VertexB:
        execution_model = spv::ExecutionModel::Vertex;
        break;
    case Stage::TessellationControl:
        execution_model = spv::ExecutionModel::TessellationControl;
        ctx.AddCapability(spv::Capability::Tessellation);
        ctx.AddExecutionMode(main, spv::ExecutionMode::OutputVertices, program.invocations);
        break;
    case Stage::TessellationEval:
        execution_model = spv::ExecutionModel::TessellationEvaluation;
        ctx.AddCapability(spv::Capability::Tessellation);
        ctx.AddExecutionMode(main, ExecutionMode(ctx.runtime_info.tess_primitive));
        ctx.AddExecutionMode(main, ExecutionMode(ctx.runtime_info.tess_spacing));
        ctx.AddExecutionMode(main, ctx.runtime_info.tess_clockwise
                                       ? spv::ExecutionMode::VertexOrderCw
                                       : spv::ExecutionMode::VertexOrderCcw);
        break;
    case Stage::Geometry:
        execution_model = spv::ExecutionModel::Geometry;
        ctx.AddCapability(spv::Capability::Geometry);
        if (ctx.profile.support_geometry_streams) {
            ctx.AddCapability(spv::Capability::GeometryStreams);
        }
        switch (ctx.runtime_info.input_topology) {
        case InputTopology::Points:
            ctx.AddExecutionMode(main, spv::ExecutionMode::InputPoints);
            break;
        case InputTopology::Lines:
            ctx.AddExecutionMode(main, spv::ExecutionMode::InputLines);
            break;
        case InputTopology::LinesAdjacency:
            ctx.AddExecutionMode(main, spv::ExecutionMode::InputLinesAdjacency);
            break;
        case InputTopology::Triangles:
            ctx.AddExecutionMode(main, spv::ExecutionMode::Triangles);
            break;
        case InputTopology::TrianglesAdjacency:
            ctx.AddExecutionMode(main, spv::ExecutionMode::InputTrianglesAdjacency);
            break;
        }
        switch (program.output_topology) {
        case OutputTopology::PointList:
            ctx.AddExecutionMode(main, spv::ExecutionMode::OutputPoints);
            break;
        case OutputTopology::LineStrip:
            ctx.AddExecutionMode(main, spv::ExecutionMode::OutputLineStrip);
            break;
        case OutputTopology::TriangleStrip:
            ctx.AddExecutionMode(main, spv::ExecutionMode::OutputTriangleStrip);
            break;
        }
        if (program.info.stores[IR::Attribute::PointSize]) {
            ctx.AddCapability(spv::Capability::GeometryPointSize);
        }
        ctx.AddExecutionMode(main, spv::ExecutionMode::OutputVertices, program.output_vertices);
        ctx.AddExecutionMode(main, spv::ExecutionMode::Invocations, program.invocations);
        if (program.is_geometry_passthrough) {
            if (ctx.profile.support_geometry_shader_passthrough) {
                ctx.AddExtension("SPV_NV_geometry_shader_passthrough");
                ctx.AddCapability(spv::Capability::GeometryShaderPassthroughNV);
            } else {
                LOG_WARNING(Shader_SPIRV, "Geometry shader passthrough used with no support");
            }
        }
        break;
    case Stage::Fragment:
        execution_model = spv::ExecutionModel::Fragment;
        if (ctx.profile.lower_left_origin_mode) {
            ctx.AddExecutionMode(main, spv::ExecutionMode::OriginLowerLeft);
        } else {
            ctx.AddExecutionMode(main, spv::ExecutionMode::OriginUpperLeft);
        }
        if (program.info.stores_frag_depth) {
            ctx.AddExecutionMode(main, spv::ExecutionMode::DepthReplacing);
        }
        if (ctx.runtime_info.force_early_z) {
            ctx.AddExecutionMode(main, spv::ExecutionMode::EarlyFragmentTests);
        }
        if (ctx.profile.support_shader_quad_control && program.info.uses_quad_shuffles) {
            ctx.AddExtension("SPV_KHR_quad_control");
            ctx.AddCapability(spv::Capability::QuadControlKHR);
            ctx.AddExecutionMode(main, spv::ExecutionMode::RequireFullQuadsKHR);
        }
        break;
    default:
        throw NotImplementedException("Stage {}", program.stage);
    }
    ctx.AddEntryPoint(execution_model, main, "main", interfaces);
}

void SetupDenormControl(const Profile& profile, const IR::Program& program, EmitContext& ctx,
                        Id main_func) {
    const Info& info{program.info};
    if (info.uses_fp32_denorms_flush && info.uses_fp32_denorms_preserve) {
        LOG_DEBUG(Shader_SPIRV, "Fp32 denorm flush and preserve on the same shader");
    } else if (info.uses_fp32_denorms_flush) {
        if (profile.support_fp32_denorm_flush && !profile.has_broken_fp32_denorm_flush) {
            ctx.AddCapability(spv::Capability::DenormFlushToZero);
            ctx.AddExecutionMode(main_func, spv::ExecutionMode::DenormFlushToZero, 32U);
        } else {
            // Drivers will most likely flush denorms by default, no need to warn
        }
    } else if (info.uses_fp32_denorms_preserve) {
        if (profile.support_fp32_denorm_preserve) {
            ctx.AddCapability(spv::Capability::DenormPreserve);
            ctx.AddExecutionMode(main_func, spv::ExecutionMode::DenormPreserve, 32U);
        } else {
            LOG_DEBUG(Shader_SPIRV, "Fp32 denorm preserve used in shader without host support");
        }
    }
    if (!profile.support_separate_denorm_behavior || profile.has_broken_fp16_float_controls) {
        // No separate denorm behavior
        return;
    }
    if (info.uses_fp16_denorms_flush && info.uses_fp16_denorms_preserve) {
        LOG_DEBUG(Shader_SPIRV, "Fp16 denorm flush and preserve on the same shader");
    } else if (info.uses_fp16_denorms_flush) {
        if (profile.support_fp16_denorm_flush) {
            ctx.AddCapability(spv::Capability::DenormFlushToZero);
            ctx.AddExecutionMode(main_func, spv::ExecutionMode::DenormFlushToZero, 16U);
        } else {
            // Same as fp32, no need to warn as most drivers will flush by default
        }
    } else if (info.uses_fp16_denorms_preserve) {
        if (profile.support_fp16_denorm_preserve) {
            ctx.AddCapability(spv::Capability::DenormPreserve);
            ctx.AddExecutionMode(main_func, spv::ExecutionMode::DenormPreserve, 16U);
        } else {
            LOG_DEBUG(Shader_SPIRV, "Fp16 denorm preserve used in shader without host support");
        }
    }
}

void SetupSignedNanCapabilities(const Profile& profile, const IR::Program& program,
                                EmitContext& ctx, Id main_func) {
    if (profile.has_broken_fp16_float_controls && program.info.uses_fp16) {
        return;
    }
    if (program.info.uses_fp16 && profile.support_fp16_signed_zero_nan_preserve) {
        ctx.AddCapability(spv::Capability::SignedZeroInfNanPreserve);
        ctx.AddExecutionMode(main_func, spv::ExecutionMode::SignedZeroInfNanPreserve, 16U);
    }
    if (profile.support_fp32_signed_zero_nan_preserve) {
        ctx.AddCapability(spv::Capability::SignedZeroInfNanPreserve);
        ctx.AddExecutionMode(main_func, spv::ExecutionMode::SignedZeroInfNanPreserve, 32U);
    }
    if (program.info.uses_fp64 && profile.support_fp64_signed_zero_nan_preserve) {
        ctx.AddCapability(spv::Capability::SignedZeroInfNanPreserve);
        ctx.AddExecutionMode(main_func, spv::ExecutionMode::SignedZeroInfNanPreserve, 64U);
    }
}

void SetupTransformFeedbackCapabilities(EmitContext& ctx, Id main_func) {
    if (ctx.runtime_info.xfb_count == 0) {
        return;
    }
    ctx.AddCapability(spv::Capability::TransformFeedback);
    ctx.AddExecutionMode(main_func, spv::ExecutionMode::Xfb);
}

void SetupCapabilities(const Profile& profile, const Info& info, EmitContext& ctx) {
    if (info.uses_sampled_1d) {
        ctx.AddCapability(spv::Capability::Sampled1D);
    }
    if (info.uses_image_1d) {
        ctx.AddCapability(spv::Capability::Image1D);
    }
    if (info.uses_sparse_residency) {
        ctx.AddCapability(spv::Capability::SparseResidency);
    }
    if (info.uses_demote_to_helper_invocation && profile.support_demote_to_helper_invocation) {
        if (profile.supported_spirv < 0x00010600) {
            ctx.AddExtension("SPV_EXT_demote_to_helper_invocation");
        }
        ctx.AddCapability(spv::Capability::DemoteToHelperInvocation);
    }
    if (info.stores[IR::Attribute::ViewportIndex] && profile.support_multi_viewport) {
        ctx.AddCapability(spv::Capability::MultiViewport);
    }
    if (info.stores[IR::Attribute::ViewportMask] && profile.support_viewport_mask) {
        ctx.AddExtension("SPV_NV_viewport_array2");
        ctx.AddCapability(spv::Capability::ShaderViewportMaskNV);
    }
    if (info.stores[IR::Attribute::Layer] || info.stores[IR::Attribute::ViewportIndex]) {
        if (profile.support_viewport_index_layer_non_geometry && ctx.stage != Stage::Geometry) {
            ctx.AddExtension("SPV_EXT_shader_viewport_index_layer");
            ctx.AddCapability(spv::Capability::ShaderViewportIndexLayerEXT);
        }
    }
    if (!profile.support_vertex_instance_id &&
        (info.loads[IR::Attribute::InstanceId] || info.loads[IR::Attribute::VertexId])) {
        ctx.AddExtension("SPV_KHR_shader_draw_parameters");
        ctx.AddCapability(spv::Capability::DrawParameters);
    }
    if ((info.uses_subgroup_vote || info.uses_subgroup_invocation_id ||
         info.uses_subgroup_shuffles || info.uses_subgroup_mask) &&
        profile.support_vote && profile.SupportsSubgroupStage(ctx.stage)) {
        ctx.AddCapability(spv::Capability::GroupNonUniformBallot);
        ctx.AddCapability(spv::Capability::GroupNonUniformShuffle);
        if (info.uses_subgroup_shuffles && profile.support_shuffle_relative) {
            ctx.AddCapability(spv::Capability::GroupNonUniformShuffleRelative);
        }
        if (!profile.warp_size_potentially_larger_than_guest) {
            // vote ops are only used when not taking the long path
            ctx.AddCapability(spv::Capability::GroupNonUniformVote);
        }
    }
    if (info.uses_quad_shuffles) {
        if (profile.support_quad_shuffles) {
            ctx.AddCapability(spv::Capability::GroupNonUniformQuad);
        }
        ctx.AddCapability(spv::Capability::GroupNonUniformShuffle);
    }
    if (info.uses_int64_bit_atomics && profile.support_int64_atomics) {
        ctx.AddCapability(spv::Capability::Int64Atomics);
    }
    if (info.uses_typeless_image_reads && profile.support_typeless_image_loads) {
        ctx.AddCapability(spv::Capability::StorageImageReadWithoutFormat);
    }
    if (info.uses_typeless_image_writes) {
        ctx.AddCapability(spv::Capability::StorageImageWriteWithoutFormat);
    }
    if (info.uses_image_buffers) {
        ctx.AddCapability(spv::Capability::ImageBuffer);
    }
    if (info.uses_sample_id) {
        ctx.AddCapability(spv::Capability::SampleRateShading);
    }
    if (info.uses_derivatives) {
        ctx.AddCapability(spv::Capability::DerivativeControl);
    }
    // TODO: Track this usage
    ctx.AddCapability(spv::Capability::ImageGatherExtended);
    ctx.AddCapability(spv::Capability::ImageQuery);
    ctx.AddCapability(spv::Capability::SampledBuffer);
    if (!ctx.non_uniform_ids.empty()) {
        if (ctx.profile.supported_spirv < 0x00010500)
            ctx.AddExtension("SPV_EXT_descriptor_indexing");
        ctx.AddCapability(spv::Capability::ShaderNonUniform);
        if (ctx.uses_nonuniform_sampled_image) {
            ctx.AddCapability(spv::Capability::SampledImageArrayNonUniformIndexing);
        }
        if (ctx.uses_nonuniform_storage_image) {
            ctx.AddCapability(spv::Capability::StorageImageArrayNonUniformIndexing);
        }
        if (ctx.uses_nonuniform_uniform_texel_buffer) {
            ctx.AddCapability(spv::Capability::UniformTexelBufferArrayNonUniformIndexing);
        }
        if (ctx.uses_nonuniform_storage_texel_buffer) {
            ctx.AddCapability(spv::Capability::StorageTexelBufferArrayNonUniformIndexing);
        }
    }
}

void PatchPhiNodes(IR::Program& program, EmitContext& ctx) {
            // Flatten all leading PHIs from each block into a vector
            std::vector<IR::Inst*> phi_instructions;
            for (IR::Block* block : program.blocks) {
                for (auto it = block->begin(); it != block->end(); ++it) {
                    if (it->GetOpcode() != IR::Opcode::Phi)
                        break;
                    phi_instructions.push_back(&*it);
                }
            }

            if (phi_instructions.empty()) {
                return; // nothing to patch
            }

            // Start "before" first PHI; advance on phi_arg == 0
            size_t phi_index = static_cast<size_t>(-1);

            ctx.PatchDeferredPhi([&](size_t phi_arg, Id parent) -> std::pair<Id, Id> {
                if (phi_arg == 0) {
                    ++phi_index;
                }
                IR::Inst* phi = phi_instructions[phi_index];
                return { ctx.Def(phi->Arg(phi_arg)), parent };
            });
        }

void RewriteOpcodes(std::vector<u32>& code, std::span<const std::pair<u32, spv::Op>> rewrites) {
    if (rewrites.empty()) {
        return;
    }
    size_t offset = 5;
    while (offset + 2 < code.size()) {
        const u32 word_count{code[offset] >> 16};
        const auto opcode{static_cast<spv::Op>(code[offset] & 0xFFFFu)};
        if (word_count == 0) {
            return;
        }
        if (opcode == spv::Op::OpGroupNonUniformShuffleXor ||
            opcode == spv::Op::OpGroupNonUniformQuadBroadcast) {
            const auto it{std::ranges::find(rewrites, code[offset + 2],
                                            &std::pair<u32, spv::Op>::first)};
            if (it != rewrites.end()) {
                code[offset] = (code[offset] & 0xFFFF0000u) | static_cast<u32>(it->second);
            }
        }
        offset += word_count;
    }
}
} // Anonymous namespace

Id EmitContext::DefineStorageAccess(IR::Opcode opcode, u32 binding) {
    // Outline the bounds checks to preserve the guest block labels used by OpPhi.
    const auto key{std::make_pair(opcode, binding)};
    if (const auto it{storage_access_functions.find(key)}; it != storage_access_functions.end()) {
        return it->second;
    }
    const Id result_type{TypeId(*this, IR::TypeOf(opcode))};
    const bool has_value{IR::NumArgsOf(opcode) == 3};
    const Id value_type{has_value ? TypeId(*this, IR::ArgTypeOf(opcode, 2)) : void_id};
    const Id function_type{has_value ? TypeFunction(result_type, U32[1], value_type)
                                     : TypeFunction(result_type, U32[1])};
    const Id function{OpFunction(result_type, spv::FunctionControlMask::Inline, function_type)};
    const Id offset{OpFunctionParameter(U32[1])};
    const Id value{has_value ? OpFunctionParameter(value_type) : Id{}};
    IR::Inst offset_arg{IR::Opcode::Void, 0};
    IR::Inst value_arg{IR::Opcode::Void, 0};
    offset_arg.SetDefinition(offset);
    value_arg.SetDefinition(value);
    IR::Inst access{opcode, 0};
    access.SetArg(0, IR::Value{binding});
    access.SetArg(1, IR::Value{&offset_arg});
    if (has_value) {
        access.SetArg(2, IR::Value{&value_arg});
    }

    u32 width{sizeof(u32)};
    switch (opcode) {
    case IR::Opcode::LoadStorageU8:
    case IR::Opcode::LoadStorageS8:
    case IR::Opcode::WriteStorageU8:
    case IR::Opcode::WriteStorageS8:
        if (profile.support_int8 && profile.support_storage_buffer_8bit &&
            profile.support_descriptor_aliasing) {
            width = sizeof(u8);
        }
        break;
    case IR::Opcode::LoadStorageU16:
    case IR::Opcode::LoadStorageS16:
    case IR::Opcode::WriteStorageU16:
    case IR::Opcode::WriteStorageS16:
        if (profile.support_int16 && profile.support_storage_buffer_16bit &&
            profile.support_descriptor_aliasing) {
            width = sizeof(u16);
        }
        break;
    case IR::Opcode::LoadStorage64:
    case IR::Opcode::WriteStorage64:
        width = sizeof(u32[2]);
        break;
    case IR::Opcode::LoadStorage128:
    case IR::Opcode::WriteStorage128:
        width = sizeof(u32[4]);
        break;
    default:
        if (IR::TypeOf(opcode) == IR::Type::U64 || IR::TypeOf(opcode) == IR::Type::U32x2) {
            width = sizeof(u64);
        }
        break;
    }
    const u32 alignment{profile.support_descriptor_aliasing ? width
                                                            : std::min(width, u32{sizeof(u32)})};
    const Id native{OpLabel()};
    const Id lookup{OpLabel()};
    const Id end_lookup{OpLabel()};
    const Id bounds{OpLabel()};
    const Id physical{OpLabel()};
    const Id out_of_bounds{OpLabel()};
    AddLabel();
    const Id table_pointer{OpAccessChain(TypePointer(spv::StorageClass::PushConstant, U64),
                                         rescaling_push_constants,
                                         Const(storage_buffer_table_member_index))};
    const Id table{OpLoad(U64, table_pointer)};
    const Id has_table{OpINotEqual(U1, table, Constant(U64, u64{0}))};
    OpSelectionMerge(native, spv::SelectionControlMask::MaskNone);
    OpBranchConditional(has_table, lookup, native);
    AddLabel(lookup);
    const Id entry_address{OpIAdd(
        U64, table,
        Constant(U64, u64{storage_buffer_index_base + binding} * sizeof(StorageBufferAddress)))};
    const Id entry_type{TypeVector(U64, 2)};
    const Id entry_pointer{OpConvertUToPtr(
        TypePointer(spv::StorageClass::PhysicalStorageBuffer, entry_type), entry_address)};
    const Id entry{OpLoad(entry_type, entry_pointer, spv::MemoryAccessMask::Aligned,
                          u32{sizeof(StorageBufferAddress)})};
    large_storage_buffer_address = OpCompositeExtract(U64, entry, 0U);
    const Id size{OpCompositeExtract(U64, entry, 1U)};
    const Id has_address{
        OpINotEqual(U1, large_storage_buffer_address, Constant(U64, u64{0}))};
    OpSelectionMerge(end_lookup, spv::SelectionControlMask::MaskNone);
    OpBranchConditional(has_address, bounds, end_lookup);
    AddLabel(bounds);
    const Id aligned_offset{OpUConvert(U64, OpBitwiseAnd(U32[1], offset, Const(~(alignment - 1))))};
    const Id end{OpIAdd(U64, aligned_offset, Constant(U64, u64{width}))};
    const Id in_bounds{OpULessThanEqual(U1, end, size)};
    OpSelectionMerge(out_of_bounds, spv::SelectionControlMask::MaskNone);
    OpBranchConditional(in_bounds, physical, out_of_bounds);
    AddLabel(physical);
    emitting_large_storage = true;
    EmitInst(*this, &access);
    emitting_large_storage = false;
    if (IR::TypeOf(opcode) == IR::Type::Void) {
        OpReturn();
    } else {
        OpReturnValue(access.Definition<Id>());
    }
    AddLabel(out_of_bounds);
    if (IR::TypeOf(opcode) == IR::Type::Void) {
        OpReturn();
    } else {
        OpReturnValue(ConstantNull(result_type));
    }
    AddLabel(end_lookup);
    OpBranch(native);
    AddLabel(native);
    EmitInst(*this, &access);
    if (IR::TypeOf(opcode) == IR::Type::Void) {
        OpReturn();
    } else {
        OpReturnValue(access.Definition<Id>());
    }
    OpFunctionEnd();
    storage_access_functions.emplace(key, function);
    return function;
}

std::vector<u32> EmitSPIRV(const Profile& profile, const RuntimeInfo& runtime_info, IR::Program& program, Bindings& bindings) {
    EmitContext ctx{profile, runtime_info, program, bindings};
    const Id main{DefineMain(ctx, program)};
    DefineEntryPoint(program, ctx, main);
    if (profile.support_float_controls) {
        ctx.AddExtension("SPV_KHR_float_controls");
        SetupDenormControl(profile, program, ctx, main);
        SetupSignedNanCapabilities(profile, program, ctx, main);
    }
    SetupCapabilities(profile, program.info, ctx);
    SetupTransformFeedbackCapabilities(ctx, main);
    PatchPhiNodes(program, ctx);
    std::vector<u32> code{ctx.Assemble()};
    RewriteOpcodes(code, ctx.opcode_rewrites);
    return code;
}

Id EmitPhi(EmitContext& ctx, IR::Inst* inst) {
    const size_t num_args{inst->NumArgs()};
    boost::container::small_vector<Id, 32> blocks;
    blocks.reserve(num_args);
    for (size_t index = 0; index < num_args; ++index) {
        blocks.push_back(inst->PhiBlock(index)->Definition<Id>());
    }
    // The type of a phi instruction is stored in its flags
    const Id result_type{TypeId(ctx, inst->Flags<IR::Type>())};
    return ctx.DeferredOpPhi(result_type, std::span(blocks.data(), blocks.size()));
}

void EmitVoid(EmitContext&) {}

Id EmitIdentity(EmitContext& ctx, const IR::Value& value) {
    const Id id{ctx.Def(value)};
    if (!Sirit::ValidId(id)) {
        throw NotImplementedException("Forward identity declaration");
    }
    return id;
}

Id EmitConditionRef(EmitContext& ctx, const IR::Value& value) {
    const Id id{ctx.Def(value)};
    if (!Sirit::ValidId(id)) {
        throw NotImplementedException("Forward identity declaration");
    }
    return id;
}

void EmitReference(EmitContext&) {}

void EmitPhiMove(EmitContext&) {
    throw LogicError("Unreachable instruction");
}

void EmitGetZeroFromOp(EmitContext&) {
    throw LogicError("Unreachable instruction");
}

void EmitGetSignFromOp(EmitContext&) {
    throw LogicError("Unreachable instruction");
}

void EmitGetCarryFromOp(EmitContext&) {
    throw LogicError("Unreachable instruction");
}

void EmitGetOverflowFromOp(EmitContext&) {
    throw LogicError("Unreachable instruction");
}

void EmitGetSparseFromOp(EmitContext&) {
    throw LogicError("Unreachable instruction");
}

void EmitGetInBoundsFromOp(EmitContext&) {
    throw LogicError("Unreachable instruction");
}

} // namespace Shader::Backend::SPIRV
