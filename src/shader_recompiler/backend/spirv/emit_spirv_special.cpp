// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/backend/spirv/emit_spirv_instructions.h"
#include "shader_recompiler/backend/spirv/spirv_emit_context.h"

namespace Shader::Backend::SPIRV {
namespace {
void ConvertDepthMode(EmitContext& ctx) {
    const Id type{ctx.F32[1]};
    const Id position{ctx.OpLoad(ctx.F32[4], ctx.output_position)};
    const Id z{ctx.OpCompositeExtract(type, position, 2u)};
    const Id w{ctx.OpCompositeExtract(type, position, 3u)};
    const Id screen_depth{ctx.OpFMul(type, ctx.OpFAdd(type, z, w), ctx.Constant(type, 0.5f))};
    const Id vector{ctx.OpCompositeInsert(ctx.F32[4], screen_depth, position, 2u)};
    ctx.OpStore(ctx.output_position, vector);
}

void SetFixedPipelinePointSize(EmitContext& ctx) {
    if (ctx.runtime_info.fixed_state_point_size) {
        const float point_size{*ctx.runtime_info.fixed_state_point_size};
        ctx.OpStore(ctx.output_point_size, ctx.Const(point_size));
    }
}

Id DefaultVarying(EmitContext& ctx, u32 num_components, u32 element, Id zero, Id one,
                  Id default_vector) {
    switch (num_components) {
    case 1:
        return element == 3 ? one : zero;
    case 2:
        return ctx.ConstantComposite(ctx.F32[2], zero, element + 1 == 3 ? one : zero);
    case 3:
        return ctx.ConstantComposite(ctx.F32[3], zero, zero, element + 2 == 3 ? one : zero);
    case 4:
        return default_vector;
    }
    throw InvalidArgument("Bad element");
}

Id CapturedVertexPointer(EmitContext& ctx, Id vertex, u32 element) {
    const Id index{ctx.OpIAdd(ctx.U32[1], ctx.OpIMul(ctx.U32[1], vertex,
                                                     ctx.Const(ctx.gs_input_stride)),
                              ctx.Const(element))};
    return ctx.OpAccessChain(ctx.gs_vec4_ptr, ctx.gs_input_buffer, ctx.u32_zero_value, index);
}

/**
 * Fills the private per vertex input from captured vertex buffer,
 * clears this thread slice of the output buffer.
 */
void GeometryAsComputePrologue(EmitContext& ctx) {
    const Id thread{
        ctx.OpCompositeExtract(ctx.U32[1], ctx.OpLoad(ctx.U32[3], ctx.gs_global_id), 0u)};
    ctx.OpStore(ctx.gs_primitive, thread);

    const u32 num_vertices{GeometryAsComputeInputVertices(ctx.runtime_info.input_topology)};
    const Id base{ctx.OpIMul(ctx.U32[1], thread, ctx.Const(num_vertices))};

    for (u32 vertex = 0; vertex < num_vertices; ++vertex) {
        const Id captured{ctx.OpIAdd(ctx.U32[1], base, ctx.Const(vertex))};

        if (Sirit::ValidId(ctx.input_position)) {
            const Id value{ctx.OpLoad(
                ctx.F32[4], CapturedVertexPointer(ctx, captured, ctx.gs_input_position_index))};
            const Id destination{ctx.OpAccessChain(
                ctx.TypePointer(spv::StorageClass::Private, ctx.F32[4]), ctx.input_position,
                ctx.Const(vertex))};
            ctx.OpStore(destination, value);
        }

        for (u32 location = 0; location < ctx.input_generics.size(); ++location) {
            const auto& generic{ctx.input_generics[location]};
            if (!Sirit::ValidId(generic.id) || ctx.gs_input_attr_index[location] == 0xff) {
                continue;
            }
            const Id value{ctx.OpLoad(
                ctx.F32[4],
                CapturedVertexPointer(ctx, captured, ctx.gs_input_attr_index[location]))};
            // a captured vertex is always float4
            for (u32 component = 0; component < 4; ++component) {
                const Id destination{ctx.OpAccessChain(ctx.input_f32, generic.id,
                                                       ctx.Const(vertex), ctx.Const(component))};
                ctx.OpStore(destination,
                            ctx.OpCompositeExtract(ctx.F32[1], value, component));
            }
        }
    }

    // A clip position no rasteriser can produce a fragment from.
    //
    // NOTE: This was (0,0,0,0), on the reasoning that w=0 is behind the near plane.
    // Could be completely wrong! But this fixed ACNH and SMM2 clipping
    const Id discard_vec{ctx.ConstantComposite(ctx.F32[4], ctx.Const(2.0f), ctx.Const(2.0f),
                                               ctx.Const(2.0f), ctx.Const(1.0f))};
    const Id slot_base{ctx.OpIMul(ctx.U32[1], thread,
                                  ctx.Const(ctx.gs_output_capacity * ctx.gs_output_stride))};
    for (u32 slot = 0; slot < ctx.gs_output_capacity; ++slot) {
        const Id index{ctx.OpIAdd(ctx.U32[1], slot_base,
                                  ctx.Const(slot * ctx.gs_output_stride))};
        ctx.OpStore(ctx.OpAccessChain(ctx.gs_vec4_ptr, ctx.gs_output_buffer, ctx.u32_zero_value,
                                      index),
                    discard_vec);
    }
}

void LockEmittedVertex(EmitContext& ctx, Id destination) {
    const Id private_vec4{ctx.TypePointer(spv::StorageClass::Private, ctx.F32[4])};
    const Id position{Sirit::ValidId(ctx.output_position)
                          ? ctx.OpLoad(ctx.F32[4], ctx.output_position)
                          : ctx.ConstantComposite(ctx.F32[4], ctx.Const(0.0f), ctx.Const(0.0f),
                                                  ctx.Const(0.0f), ctx.Const(1.0f))};
    ctx.OpStore(ctx.OpAccessChain(private_vec4, destination, ctx.u32_zero_value), position);

    for (u32 location = 0; location < ctx.output_generics.size(); ++location) {
        const auto& info{ctx.output_generics[location]};
        if (info[0].num_components == 0) {
            continue;
        }
        std::array<Id, 4> components{ctx.Const(0.0f), ctx.Const(0.0f), ctx.Const(0.0f),
                                     ctx.Const(1.0f)};
        u32 element{0};
        while (element < 4) {
            const auto& piece{info[element]};
            if (piece.num_components == 0) {
                break;
            }
            const Id value{ctx.OpLoad(ctx.F32[piece.num_components], piece.id)};
            for (u32 component = 0; component < piece.num_components; ++component) {
                components[element + component] =
                    piece.num_components == 1
                        ? value
                        : ctx.OpCompositeExtract(ctx.F32[1], value, component);
            }
            element += piece.num_components;
        }
        const Id vector{ctx.OpCompositeConstruct(ctx.F32[4], components[0], components[1],
                                                 components[2], components[3])};
        ctx.OpStore(ctx.OpAccessChain(private_vec4, destination, ctx.Const(1u + location)),
                    vector);
    }
}

void CopyLockedVertex(EmitContext& ctx, Id source, Id destination) {
    const Id private_vec4{ctx.TypePointer(spv::StorageClass::Private, ctx.F32[4])};
    for (u32 element = 0; element < ctx.gs_output_stride; ++element) {
        const Id index{ctx.Const(element)};
        const Id value{
            ctx.OpLoad(ctx.F32[4], ctx.OpAccessChain(private_vec4, source, index))};
        ctx.OpStore(ctx.OpAccessChain(private_vec4, destination, index), value);
    }
}

void WriteEmittedVertex(EmitContext& ctx, Id source, Id slot) {
    const Id private_vec4{ctx.TypePointer(spv::StorageClass::Private, ctx.F32[4])};
    const Id slice{ctx.OpIMul(ctx.U32[1], ctx.OpLoad(ctx.U32[1], ctx.gs_primitive),
                              ctx.Const(ctx.gs_output_capacity))};
    const Id base{ctx.OpIMul(ctx.U32[1], ctx.OpIAdd(ctx.U32[1], slice, slot),
                             ctx.Const(ctx.gs_output_stride))};
    for (u32 element = 0; element < ctx.gs_output_stride; ++element) {
        const Id value{ctx.OpLoad(
            ctx.F32[4], ctx.OpAccessChain(private_vec4, source, ctx.Const(element)))};
        const Id index{ctx.OpIAdd(ctx.U32[1], base, ctx.Const(element))};
        ctx.OpStore(ctx.OpAccessChain(ctx.gs_vec4_ptr, ctx.gs_output_buffer, ctx.u32_zero_value,
                                      index),
                    value);
    }
}

Id ComparisonFunction(EmitContext& ctx, CompareFunction comparison, Id operand_1, Id operand_2) {
    switch (comparison) {
    case CompareFunction::Never:
        return ctx.false_value;
    case CompareFunction::Less:
        return ctx.OpFOrdLessThan(ctx.U1, operand_1, operand_2);
    case CompareFunction::Equal:
        return ctx.OpFOrdEqual(ctx.U1, operand_1, operand_2);
    case CompareFunction::LessThanEqual:
        return ctx.OpFOrdLessThanEqual(ctx.U1, operand_1, operand_2);
    case CompareFunction::Greater:
        return ctx.OpFOrdGreaterThan(ctx.U1, operand_1, operand_2);
    case CompareFunction::NotEqual:
        return ctx.OpFOrdNotEqual(ctx.U1, operand_1, operand_2);
    case CompareFunction::GreaterThanEqual:
        return ctx.OpFOrdGreaterThanEqual(ctx.U1, operand_1, operand_2);
    case CompareFunction::Always:
        return ctx.true_value;
    }
    throw InvalidArgument("Comparison function {}", comparison);
}

void AlphaTest(EmitContext& ctx) {
    if (!ctx.runtime_info.alpha_test_func) {
        return;
    }
    const auto comparison{*ctx.runtime_info.alpha_test_func};
    if (comparison == CompareFunction::Always) {
        return;
    }
    if (!Sirit::ValidId(ctx.frag_color[0])) {
        return;
    }

    const Id type{ctx.F32[1]};
    const Id rt0_color{ctx.OpLoad(ctx.F32[4], ctx.frag_color[0])};
    const Id alpha{ctx.OpCompositeExtract(type, rt0_color, 3u)};

    const Id true_label{ctx.OpLabel()};
    const Id discard_label{ctx.OpLabel()};
    const Id alpha_reference{ctx.Const(ctx.runtime_info.alpha_test_reference)};
    const Id condition{ComparisonFunction(ctx, comparison, alpha, alpha_reference)};

    ctx.OpSelectionMerge(true_label, spv::SelectionControlMask::MaskNone);
    ctx.OpBranchConditional(condition, true_label, discard_label);
    ctx.AddLabel(discard_label);
    ctx.OpKill();
    ctx.AddLabel(true_label);
}
} // Anonymous namespace

void EmitPrologue(EmitContext& ctx) {
    if (ctx.stage == Stage::Fragment && ctx.runtime_info.dual_source_blend) {
        // Initialize dual-source blending outputs - prevents MoltenVK crash.
        const Id zero{ctx.Const(0.0f)};
        const Id one{ctx.Const(1.0f)};
        const Id default_color{ctx.ConstantComposite(ctx.F32[4], zero, zero, zero, one)};
        for (u32 i = 0; i < 2; ++i) {
            if (Sirit::ValidId(ctx.frag_color[i])) {
                ctx.OpStore(ctx.frag_color[i], default_color);
            }
        }
    }
    if (ctx.stage == Stage::VertexB) {
        const Id zero{ctx.Const(0.0f)};
        const Id one{ctx.Const(1.0f)};
        const Id default_vector{ctx.ConstantComposite(ctx.F32[4], zero, zero, zero, one)};
        ctx.OpStore(ctx.output_position, default_vector);
        for (const auto& info : ctx.output_generics) {
            if (info[0].num_components == 0) {
                continue;
            }
            u32 element{0};
            while (element < 4) {
                const auto& element_info{info[element]};
                const u32 num{element_info.num_components};
                const Id value{DefaultVarying(ctx, num, element, zero, one, default_vector)};
                ctx.OpStore(element_info.id, value);
                element += num;
            }
        }

        if (Sirit::ValidId(ctx.clip_distances)) {
            for (u32 i = 0; i < ctx.profile.max_user_clip_distances; ++i) {
                if (!clip_distance_written.test(i)) {
                    const Id idx = ctx.Const(i);
                    const Id element = ctx.OpAccessChain(ctx.output_f32, ctx.clip_distances, idx);
                    ctx.OpStore(element, ctx.Const(0.0f));
                }
            }
        }
    }
    if (ctx.stage == Stage::VertexB || ctx.stage == Stage::Geometry) {
        SetFixedPipelinePointSize(ctx);
    }
    if (ctx.gs_as_compute) {
        GeometryAsComputePrologue(ctx);
    }
}

void EmitEpilogue(EmitContext& ctx) {
    if (ctx.stage == Stage::VertexB && ctx.runtime_info.convert_depth_mode &&
        !ctx.profile.support_native_ndc) {
        ConvertDepthMode(ctx);
    }
    if (ctx.stage == Stage::Fragment) {
        AlphaTest(ctx);
    }
}

void EmitEmitVertex(EmitContext& ctx, const IR::Value& stream) {
    if (ctx.runtime_info.convert_depth_mode && !ctx.profile.support_native_ndc) {
        ConvertDepthMode(ctx);
    }
    if (ctx.gs_as_compute) {
        // Flatten the strip as it is produced.
        if (!stream.IsImmediate() || stream.U32() != 0) {
            return;
        }
        const Id emitted{ctx.OpLoad(ctx.U32[1], ctx.gs_emitted)};
        LockEmittedVertex(ctx, ctx.gs_prev2);

        const Id enough{ctx.OpUGreaterThanEqual(ctx.U1, emitted, ctx.Const(2u))};
        const Id written{ctx.OpLoad(ctx.U32[1], ctx.gs_written)};
        const Id room{ctx.OpULessThan(ctx.U1, written, ctx.Const(ctx.gs_output_capacity / 3))};

        const Id emit_label{ctx.OpLabel()};
        const Id merge_label{ctx.OpLabel()};
        ctx.OpSelectionMerge(merge_label, spv::SelectionControlMask::MaskNone);
        ctx.OpBranchConditional(ctx.OpLogicalAnd(ctx.U1, enough, room), emit_label, merge_label);

        ctx.AddLabel(emit_label);
        {
            const Id base{ctx.OpIMul(ctx.U32[1], written, ctx.Const(3u))};
            const Id second{ctx.OpIAdd(ctx.U32[1], base, ctx.Const(1u))};
            const Id third{ctx.OpIAdd(ctx.U32[1], base, ctx.Const(2u))};
            const Id odd{ctx.OpINotEqual(
                ctx.U1, ctx.OpBitwiseAnd(ctx.U32[1], emitted, ctx.Const(1u)), ctx.u32_zero_value)};
            WriteEmittedVertex(ctx, ctx.gs_prev0, ctx.OpSelect(ctx.U32[1], odd, second, base));
            WriteEmittedVertex(ctx, ctx.gs_prev1, ctx.OpSelect(ctx.U32[1], odd, base, second));
            WriteEmittedVertex(ctx, ctx.gs_prev2, third);
            ctx.OpStore(ctx.gs_written, ctx.OpIAdd(ctx.U32[1], written, ctx.Const(1u)));
            ctx.OpBranch(merge_label);
        }
        ctx.AddLabel(merge_label);

        CopyLockedVertex(ctx, ctx.gs_prev1, ctx.gs_prev0);
        CopyLockedVertex(ctx, ctx.gs_prev2, ctx.gs_prev1);
        ctx.OpStore(ctx.gs_emitted, ctx.OpIAdd(ctx.U32[1], emitted, ctx.Const(1u)));
        SetFixedPipelinePointSize(ctx);
        return;
    }
    if (!ctx.profile.support_geometry_streams) {
        throw NotImplementedException("Geometry streams");
    } else if (stream.IsImmediate()) {
        ctx.OpEmitStreamVertex(ctx.Def(stream));
    } else {
        LOG_WARNING(Shader_SPIRV, "Stream is not immediate");
        ctx.OpEmitStreamVertex(ctx.u32_zero_value);
    }
    // Restore fixed pipeline point size after emitting the vertex
    SetFixedPipelinePointSize(ctx);
}

void EmitEndPrimitive(EmitContext& ctx, const IR::Value& stream) {
    if (ctx.gs_as_compute) {
        ctx.OpStore(ctx.gs_emitted, ctx.u32_zero_value);
        return;
    }
    if (!ctx.profile.support_geometry_streams) {
        throw NotImplementedException("Geometry streams");
    } else if (stream.IsImmediate()) {
        ctx.OpEndStreamPrimitive(ctx.Def(stream));
    } else {
        LOG_WARNING(Shader_SPIRV, "Stream is not immediate");
        ctx.OpEndStreamPrimitive(ctx.u32_zero_value);
    }
}

} // namespace Shader::Backend::SPIRV
