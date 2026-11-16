// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <chrono>
#include <bit>
#include <bitset>
#include <cstdlib>
#include <fstream>
#include <memory>

#include <format>
#include <boost/container/static_vector.hpp>

#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "common/cityhash.h"
#include "common/logging.h"
#include "common/settings.h"
#include "video_core/metal_common/metal_device.h"
#include "video_core/transform_feedback.h"
#include "common/scope_exit.h"
#include "video_core/shader_notify.h"
#include "video_core/renderer_metal/mtl_pipeline_cache.h"
#include "video_core/renderer_metal/mtl_scheduler.h"
#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/frontend/maxwell/control_flow.h"
#include "shader_recompiler/frontend/maxwell/translate_program.h"
#include "video_core/engines/kepler_compute.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/shader_environment.h"
#include "video_core/surface.h"
#include "video_core/renderer_metal/mtl_shader_util.h"

namespace Metal {

namespace {

using Shader::Backend::Bindings;

// Describes Metal to the shader recompiler. Flags the Vulkan backend sets true
// are false here on purpose, each one drops the recompiler onto an emulation
// path it already keeps around for OpenGL and older GPUs.
Shader::Profile MakeProfile(const Device& device) {
    Shader::Profile profile{};

    // SPIR-V 1.3 matches what host_shaders is built against, and it's the
    // highest version SPIRV-Cross takes cleanly for what we need.
    profile.supported_spirv = 0x00010300;

    profile.unified_descriptor_binding = true;
    // Argument buffers dislike aliasing distinct descriptors onto one slot.
    profile.support_descriptor_aliasing = false;

    profile.support_int8 = true;
    profile.support_int16 = true;
    profile.support_int64 = device.SupportsMetal3();
    profile.support_uniform_and_storage_buffer_8bit = true;
    profile.support_storage_buffer_8bit = true;
    profile.support_uniform_and_storage_buffer_16bit = true;
    profile.support_storage_buffer_16bit = true;

    // MSL has no gl_VertexIndex/gl_InstanceIndex distinction, the recompiler
    // emulates it with base vertex and base instance.
    profile.support_vertex_instance_id = false;

    // No SPV_KHR_float_controls analogue, MSL fixes rounding and denormal
    // behaviour instead of letting the shader pick.
    profile.support_float_controls = false;

    profile.support_vote = true;
    // Fragment and compute only, those are the stages where MSL exposes the
    // SIMD-group functions a subgroup vote lowers to. Claim more and the
    // recompiler emits a vote in a vertex shader, which won't compile.
    profile.supported_subgroup_stages = (1u << static_cast<u32>(Shader::Stage::Fragment)) |
                                        (1u << static_cast<u32>(Shader::Stage::Compute));

    // Apple GPUs are 32 wide, matching the guest, but discrete AMD parts are
    // 64. The recompiler masks ballot results down only when told to.
    profile.warp_size_potentially_larger_than_guest = device.GetSubgroupSize() > 32;

    // [[render_target_array_index]] is writable from a vertex shader on Apple
    // GPUs, so layered rendering doesn't need a geometry stage.
    profile.support_viewport_index_layer_non_geometry = true;
    profile.support_multi_viewport = true;

    // MSL textures are strongly typed, a typeless load has to be emulated.
    profile.support_typeless_image_loads = false;

    profile.support_demote_to_helper_invocation = true;
    profile.support_int64_atomics = false;
    profile.support_shared_int64_atomics = false;
    profile.support_derivative_control = false;

    // Metal has neither geometry shaders nor their passthrough extension.
    profile.support_geometry_shader_passthrough = false;
    profile.support_geometry_streams = false;
    // Metal has no geometry stage, so the recompiler lowers geometry programs to
    // compute kernels. Without this the stage and its primitives are dropped.
    // Opt-in, the emulation is incomplete and can hang.
    profile.emulate_geometry_as_compute = Settings::values.metal_geometry_emulation.GetValue();
    if (profile.emulate_geometry_as_compute) {
        LOG_WARNING(Render_Metal,
                    "Geometry shader emulation is enabled; this is incomplete and can stall the "
                    "emulator");
    }

    // Metal clip space is 0..1 only, so we have to emit the depth range fixup.
    profile.support_native_ndc = false;
    profile.lower_left_origin_mode = false;

    // Tier 2 argument buffers let us index a resource array with a value that
    // varies across the quad. All four kinds need it, leave any false and the
    // recompiler emits a scalar fallback loop.
    const bool nonuniform_indexing = device.SupportsArgumentBuffersTier2();
    profile.support_sampled_image_array_nonuniform_indexing = nonuniform_indexing;
    profile.support_storage_image_array_nonuniform_indexing = nonuniform_indexing;
    profile.support_uniform_texel_buffer_array_nonuniform_indexing = nonuniform_indexing;
    profile.support_storage_texel_buffer_array_nonuniform_indexing = nonuniform_indexing;

    // No NV_viewport_array2 equivalent, a vertex shader can write one viewport
    // index and not a mask.
    profile.support_viewport_mask = false;

    // Apple GPUs round the gather coordinate the way Maxwell does, so the
    // half-texel correction some desktop GPUs need would shift every gather.
    profile.need_gather_subpixel_offset = false;

    // MSL wants a complete fragment output struct, undeclared colour outputs
    // read as undefined otherwise.
    profile.need_declared_frag_colors = true;

    // Fast math changes NaN and infinity handling in ways guest shaders notice.
    profile.need_fastmath_off = true;

    profile.min_ssbo_alignment = 16;
    profile.max_user_clip_distances = 8;

    return profile;
}

Shader::HostTranslateInfo MakeHostTranslateInfo(const Device& device) {
    // Designated initialisers have to follow declaration order, and
    // min_ssbo_alignment comes before the descriptor limits and support_* flags.
    return Shader::HostTranslateInfo{
        .min_ssbo_alignment = 16,
        // Metal has no per-stage descriptor limits worth modelling, argument
        // buffers address resources by index and only buffer size bounds them.
        .max_per_stage_descriptor_sampled_images =
            Shader::HostTranslateInfo::DEFAULT_DESCRIPTOR_LIMIT,
        .max_per_stage_resources = Shader::HostTranslateInfo::DEFAULT_DESCRIPTOR_LIMIT,
        .max_descriptor_set_samplers = Shader::HostTranslateInfo::DEFAULT_DESCRIPTOR_LIMIT,
        .max_descriptor_set_uniform_buffers = Shader::HostTranslateInfo::DEFAULT_DESCRIPTOR_LIMIT,
        .max_descriptor_set_uniform_buffers_dynamic =
            Shader::HostTranslateInfo::DEFAULT_DESCRIPTOR_LIMIT,
        .max_descriptor_set_storage_buffers = Shader::HostTranslateInfo::DEFAULT_DESCRIPTOR_LIMIT,
        .max_descriptor_set_storage_buffers_dynamic =
            Shader::HostTranslateInfo::DEFAULT_DESCRIPTOR_LIMIT,
        .max_descriptor_set_sampled_images = Shader::HostTranslateInfo::DEFAULT_DESCRIPTOR_LIMIT,
        .max_descriptor_set_storage_images = Shader::HostTranslateInfo::DEFAULT_DESCRIPTOR_LIMIT,
        .max_descriptor_set_input_attachements =
            Shader::HostTranslateInfo::DEFAULT_DESCRIPTOR_LIMIT,
        .support_float64 = false, // MSL has no double precision at all.
        .support_float16 = true,  // half is native and fast on Apple GPUs.
        .support_int64 = device.SupportsMetal3(),
        .needs_demote_reorder = false,
        .support_snorm_render_buffer = true,
        .support_viewport_index_layer = true,
        .support_geometry_shader_passthrough = false,
        .support_conditional_barrier = false,
    };
}

// The recompiler's spelling of a Maxwell comparison, for the alpha test it bakes
// into the fragment shader.
Shader::CompareFunction MaxwellToCompareFunction(
    Tegra::Engines::Maxwell3D::Regs::ComparisonOp comparison) {
    using Op = Tegra::Engines::Maxwell3D::Regs::ComparisonOp;
    switch (comparison) {
    case Op::Never_D3D:
    case Op::Never_GL:
        return Shader::CompareFunction::Never;
    case Op::Less_D3D:
    case Op::Less_GL:
        return Shader::CompareFunction::Less;
    case Op::Equal_D3D:
    case Op::Equal_GL:
        return Shader::CompareFunction::Equal;
    case Op::LessEqual_D3D:
    case Op::LessEqual_GL:
        return Shader::CompareFunction::LessThanEqual;
    case Op::Greater_D3D:
    case Op::Greater_GL:
        return Shader::CompareFunction::Greater;
    case Op::NotEqual_D3D:
    case Op::NotEqual_GL:
        return Shader::CompareFunction::NotEqual;
    case Op::GreaterEqual_D3D:
    case Op::GreaterEqual_GL:
        return Shader::CompareFunction::GreaterThanEqual;
    case Op::Always_D3D:
    case Op::Always_GL:
        return Shader::CompareFunction::Always;
    default:
        return Shader::CompareFunction::Always;
    }
}

// Everything the recompiler needs that isn't in the guest bytecode. Taken from
// the pipeline key and never from live Maxwell registers, so a pipeline can be
// rebuilt outside the draw that first wanted it, which is what the disk shader
// cache is for. Every value here has to be in the key too, they all change the
// compiled shader.
Shader::RuntimeInfo MakeRuntimeInfo(const FixedPipelineState& state,
                                    const Shader::IR::Program& program,
                                    const Shader::IR::Program* previous_program,
                                    bool has_geometry,
                                    const std::array<Shader::TransformFeedbackVarying, 256>*
                                        xfb_varyings = nullptr,
                                    u32 xfb_count = 0) {
    using Maxwell = Tegra::Engines::Maxwell3D::Regs;
    Shader::RuntimeInfo info;
    if (previous_program != nullptr) {
        info.previous_stage_stores = previous_program->info.stores;
        info.previous_stage_legacy_stores_mapping = previous_program->info.legacy_stores_mapping;
        if (previous_program->is_geometry_passthrough) {
            info.previous_stage_stores.mask |= previous_program->info.passthrough.mask;
        }
    } else {
        info.previous_stage_stores.mask.set();
    }

    const float point_size = std::bit_cast<float>(state.point_size);
    const bool draws_points = state.topology_class == MTL::PrimitiveTopologyClassPoint;

    switch (program.stage) {
    case Shader::Stage::VertexB:
        if (!has_geometry) {
            if (draws_points) {
                info.fixed_state_point_size = point_size;
            }
            // Metal clip space is 0..1 only, so a guest using OpenGL-style -1..1
            // NDC needs the depth conversion emitted into the shader.
            info.convert_depth_mode = state.ndc_minus_one_to_one;
            // The constant the guest's shaders read as YDirection.
            info.y_negate = state.y_direction_negate;
        }
        // Every vertex input has to be declared with the type the vertex
        // descriptor actually delivers. A mismatch reads an integer attribute as
        // a float, or the other way round, and says nothing.
        for (size_t index = 0; index < state.attribute_types.size(); ++index) {
            info.generic_input_types[index] =
                static_cast<Shader::AttributeType>(state.attribute_types[index]);
        }
        break;
    case Shader::Stage::TessellationEval:
        // Domain, spacing and winding are execution modes on the evaluation
        // program so the recompiler needs them up front. This doesn't make
        // tessellation work, both stages are still dropped below, it only makes
        // the SPIR-V describe the guest's actual domain.
        info.tess_clockwise = state.tessellation_clockwise != 0;
        info.tess_primitive = [&state] {
            switch (static_cast<Maxwell::Tessellation::DomainType>(state.tessellation_domain)) {
            case Maxwell::Tessellation::DomainType::Isolines:
                return Shader::TessPrimitive::Isolines;
            case Maxwell::Tessellation::DomainType::Triangles:
                return Shader::TessPrimitive::Triangles;
            case Maxwell::Tessellation::DomainType::Quads:
                return Shader::TessPrimitive::Quads;
            }
            return Shader::TessPrimitive::Triangles;
        }();
        info.tess_spacing = [&state] {
            switch (static_cast<Maxwell::Tessellation::Spacing>(state.tessellation_spacing)) {
            case Maxwell::Tessellation::Spacing::Integer:
                return Shader::TessSpacing::Equal;
            case Maxwell::Tessellation::Spacing::FractionalOdd:
                return Shader::TessSpacing::FractionalOdd;
            case Maxwell::Tessellation::Spacing::FractionalEven:
                return Shader::TessSpacing::FractionalEven;
            }
            return Shader::TessSpacing::Equal;
        }();
        break;
    case Shader::Stage::Geometry:
        if (program.output_topology == Shader::OutputTopology::PointList) {
            info.fixed_state_point_size = point_size;
        }
        // Geometry is the last stage before rasterisation, so the -1..1 -> 0..1
        // clip space conversion belongs here and not in the vertex shader.
        // EmitEmitVertex applies it per emitted vertex.
        info.convert_depth_mode = state.ndc_minus_one_to_one;
        // The constant the guest's shaders read as YDirection.
        info.y_negate = state.y_direction_negate;
        break;
    case Shader::Stage::Fragment: {
        // Early-Z is an execution mode baked into the shader so it's part of the
        // key. Force it ONLY where it can't change results. Under
        // EarlyFragmentTests a discarding shader still writes depth for the
        // texels it threw away, and that geometry then occludes everything
        // behind it.
        //
        // The discard test has to include the alpha test. The SPIR-V backend
        // emits that as an OpKill after collect_shader_info_pass has run, so
        // uses_demote_to_helper_invocation is false for exactly the alpha-tested
        // surfaces that make this dangerous. Saying no costs nothing, the
        // hardware may apply early-Z anyway.
        const bool alpha_test_discards =
            static_cast<Tegra::Engines::Maxwell3D::Regs::ComparisonOp>(state.alpha_test_func) !=
            Tegra::Engines::Maxwell3D::Regs::ComparisonOp::Always_GL;
        const bool may_discard =
            program.info.uses_demote_to_helper_invocation || alpha_test_discards;
        info.force_early_z =
            state.force_early_z && !may_discard && !program.info.stores_frag_depth;
        // Alpha test is fixed function on Maxwell and Metal has no equivalent,
        // so the recompiler emits it. Without this every alpha-tested surface
        // draws as a solid quad.
        info.alpha_test_func = MaxwellToCompareFunction(
            static_cast<Tegra::Engines::Maxwell3D::Regs::ComparisonOp>(state.alpha_test_func));
        info.alpha_test_reference = std::bit_cast<float>(state.alpha_test_ref);

        info.dual_source_blend = state.dual_source_blend != 0;

        // MSL wants a fragment output's type to match its attachment's format.
        // Writing float4 into an integer attachment is a hard compilation error,
        // not a conversion.
        for (size_t index = 0; index < info.color_output_types.size(); ++index) {
            info.color_output_types[index] =
                static_cast<Shader::AttributeType>(state.color_output_types[index]);
        }
        break;
    }
    default:
        break;
    }

    // How many vertices one geometry shader invocation reads.
    using Topology = Tegra::Engines::Maxwell3D::Regs::PrimitiveTopology;
    switch (static_cast<Topology>(state.topology)) {
    case Topology::Points:
        info.input_topology = Shader::InputTopology::Points;
        break;
    case Topology::Lines:
    case Topology::LineLoop:
    case Topology::LineStrip:
        info.input_topology = Shader::InputTopology::Lines;
        break;
    case Topology::LinesAdjacency:
    case Topology::LineStripAdjacency:
        info.input_topology = Shader::InputTopology::LinesAdjacency;
        break;
    case Topology::TrianglesAdjacency:
    case Topology::TriangleStripAdjacency:
        info.input_topology = Shader::InputTopology::TrianglesAdjacency;
        break;
    default:
        info.input_topology = Shader::InputTopology::Triangles;
        break;
    }

    // No caller passes these right now, see the note in the stage loop on why the
    // capture path translates without xfb decorations on purpose.
    if (xfb_varyings != nullptr && program.stage == Shader::Stage::VertexB) {
        info.xfb_varyings = *xfb_varyings;
        info.xfb_count = xfb_count;
    }
    return info;
}

} // Anonymous namespace

// How the shader has to declare vertex input @p index, given what the vertex
// descriptor will hand it. MUST agree with VertexFormat() in maxwell_to_mtl, a
// disagreement isn't a validation error, the attribute just reads as the wrong
// thing. Scaled types are approximated, Metal has no *_USCALED equivalent so we
// deliver and declare the integer, and a shader wanting the widened float value
// gets an unscaled integer.
Shader::AttributeType AttributeTypeOf(const Tegra::Engines::Maxwell3D::Regs& regs, size_t index) {
    using Maxwell = Tegra::Engines::Maxwell3D::Regs;
    const auto& attribute = regs.vertex_attrib_format[index];
    if (attribute.constant != 0) {
        // Constant attributes are fed from the zero-filled null buffer, but the
        // shader has to declare them Disabled the same way Vulkan does
        // (vk_pipeline_cache.cpp CastAttributeType, off the enabled bit that
        // fixed_pipeline_state.cpp assigns as `input.constant ? 0 : 1`).
        //
        // Declare Float instead and it reads the null buffer and gives
        // (0,0,0,0). Disabled makes the recompiler substitute the architectural
        // default (0,0,0,1) at EmitGetAttribute. A mesh whose blend-weight
        // stream is constant collapses toward the origin on the w=0 form.
        return Shader::AttributeType::Disabled;
    }
    switch (attribute.type) {
    case Maxwell::VertexAttribute::Type::UNorm:
    case Maxwell::VertexAttribute::Type::SNorm:
    case Maxwell::VertexAttribute::Type::Float:
        return Shader::AttributeType::Float;
    case Maxwell::VertexAttribute::Type::UInt:
        return Shader::AttributeType::UnsignedInt;
    case Maxwell::VertexAttribute::Type::SInt:
        return Shader::AttributeType::SignedInt;
    case Maxwell::VertexAttribute::Type::UScaled:
        // Scaled isn't integer. The data is integer in memory but the shader
        // reads a float carrying its value, and the recompiler only does that
        // conversion if we tell it the attribute is scaled.
        return Shader::AttributeType::UnsignedScaled;
    case Maxwell::VertexAttribute::Type::SScaled:
        return Shader::AttributeType::SignedScaled;
    default:
        break;
    }
    return Shader::AttributeType::Float;
}

// Whether @p factor references the shader's second colour output.
bool IsDualSourceBlendFactor(Tegra::Engines::Maxwell3D::Regs::Blend::Factor factor) {
    using F = Tegra::Engines::Maxwell3D::Regs::Blend::Factor;
    switch (factor) {
    case F::Source1Color_D3D:
    case F::OneMinusSource1Color_D3D:
    case F::Source1Alpha_D3D:
    case F::OneMinusSource1Alpha_D3D:
    case F::Source1Color_GL:
    case F::OneMinusSource1Color_GL:
    case F::Source1Alpha_GL:
    case F::OneMinusSource1Alpha_GL:
        return true;
    default:
        return false;
    }
}

// Dual-source blending makes the fragment shader declare a second output for
// index 1 instead of a second attachment. That changes the shader's signature so
// it belongs in the pipeline key.
bool ComputeAttachment0DualSourceBlend(const Tegra::Engines::Maxwell3D::Regs& regs) {
    if (!regs.blend.enable[0]) {
        return false;
    }
    const auto uses_dual_source = [](const auto& blend) {
        return IsDualSourceBlendFactor(blend.color_source) ||
               IsDualSourceBlendFactor(blend.color_dest) ||
               IsDualSourceBlendFactor(blend.alpha_source) ||
               IsDualSourceBlendFactor(blend.alpha_dest);
    };
    return regs.blend_per_target_enabled != 0 ? uses_dual_source(regs.blend_per_target[0])
                                              : uses_dual_source(regs.blend);
}


size_t GraphicsPipelineCacheKey::Hash() const noexcept {
    const u64 hash = Common::CityHash64(reinterpret_cast<const char*>(this), sizeof(*this));
    return static_cast<size_t>(hash);
}

size_t ComputePipelineCacheKey::Hash() const noexcept {
    const u64 hash = Common::CityHash64(reinterpret_cast<const char*>(this), sizeof(*this));
    return static_cast<size_t>(hash);
}

PipelineCache::PipelineCache(Tegra::MaxwellDeviceMemoryManager& device_memory_,
                             const Device& device_, Scheduler& scheduler_,
                             VideoCore::ShaderNotify& shader_notify_)
    : VideoCommon::ShaderCache{device_memory_}, device{device_}, scheduler{scheduler_},
      shader_notify{shader_notify_},
      use_asynchronous_shaders{Settings::values.use_asynchronous_shaders.GetValue()},
      profile{MakeProfile(device_)}, host_info{MakeHostTranslateInfo(device_)},
      depth_stencil_cache{device_}, serialization_thread{1, "MtlPipelineSerialization"} {
    const size_t num_workers = std::max<size_t>(
        1, static_cast<size_t>(std::thread::hardware_concurrency()) - 1);
    workers = std::make_unique<Common::StatefulThreadWorker<>>(num_workers, "MetalShaderBuilder");
}

void PipelineCache::MaybeFlushMetal4Archive() {
    // ~PipelineCache used to be the only writer, so any hard kill, crash or
    // force-quit left the archive at zero bytes and the next session rebuilt
    // every pipeline from MSL.
    //
    // serializeAsArchiveAndFlushToURL rewrites the whole archive, so this is
    // bounded twice. Only once enough new pipelines have piled up to be worth
    // the write, and never more often than the interval below. Both checks run
    // on the serialisation thread, which is off the draw path and
    // single-threaded, so two writes can't overlap.
    constexpr u32 PIPELINES_PER_FLUSH = 32;
    constexpr auto MIN_FLUSH_INTERVAL = std::chrono::seconds{30};

    if (++pipelines_since_archive_flush < PIPELINES_PER_FLUSH) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (last_archive_flush != std::chrono::steady_clock::time_point{} &&
        now - last_archive_flush < MIN_FLUSH_INTERVAL) {
        return;
    }
    pipelines_since_archive_flush = 0;
    last_archive_flush = now;
    device.SaveMetal4Archive();
}

PipelineCache::~PipelineCache() {
    // Drain the pool before any pipeline is destroyed. The workers abandon queued
    // work on stop_requested(), so an unbuilt pipeline never signals completion
    // and ~GraphicsPipeline waits for exactly that signal.
    if (workers) {
        workers->WaitForRequests();
    }
    // After the drain, so everything this session compiled is in the serializer
    // before it gets written out.
    device.SaveMetal4Archive();
}

void PipelineCache::LoadDiskResources(u64 title_id, std::stop_token stop_loading,
                                      const VideoCore::DiskResourceLoadCallback& callback) {
    if (title_id == 0) {
        return;
    }
    // Two caches doing different jobs. The Metal 4 archive makes *recompiling* a
    // pipeline cheap (handed back through the compiler task options), the file
    // below remembers *which* pipelines the title needs, key plus guest
    // bytecode, so we can rebuild them before the game asks.
    device.LoadMetal4Archive(title_id);

    const auto shader_dir = Common::FS::GetEdenPath(Common::FS::EdenPath::ShaderDir);
    const auto base_dir = shader_dir / std::format("{:016x}", title_id);
    if (!Common::FS::CreateDir(shader_dir) || !Common::FS::CreateDir(base_dir)) {
        LOG_ERROR(Common_Filesystem, "Failed to create the pipeline cache directories");
        return;
    }
    pipeline_cache_filename = base_dir / "metal.bin";

    struct {
        std::mutex mutex;
        size_t total{};
        size_t built{};
        bool has_loaded{};
    } state;

    const auto load_compute = [&](std::ifstream& file, VideoCommon::FileEnvironment env) {
        ComputePipelineCacheKey key;
        file.read(reinterpret_cast<char*>(&key), sizeof(key));

        workers->QueueWork([this, key, env_ = std::move(env), &state, &callback]() mutable {
            // Its own arena, several of these run at once.
            ShaderPools pools;
            auto pipeline = CreateComputePipeline(pools, key, env_);

            std::scoped_lock lock{state.mutex};
            if (pipeline) {
                compute_cache.emplace(key, std::move(pipeline));
            }
            ++state.built;
            if (state.has_loaded) {
                callback(VideoCore::LoadCallbackStage::Build, state.built, state.total);
            }
        });
        ++state.total;
    };

    const auto load_graphics = [&](std::ifstream& file,
                                   std::vector<VideoCommon::FileEnvironment> envs) {
        GraphicsPipelineCacheKey key;
        file.read(reinterpret_cast<char*>(&key), sizeof(key));

        workers->QueueWork([this, key, envs_ = std::move(envs), &state, &callback]() mutable {
            // Its own arena, several of these run at once.
            ShaderPools pools;

            // Densely packed and not indexed by stage. GraphicsEnvironments::Span
            // stops at the first null entry so a gap would truncate it. The
            // consumer re-pairs entries with stages by walking unique_hashes in
            // serialization order.
            GraphicsEnvironments environments;
            const size_t num_envs = std::min(envs_.size(), environments.env_ptrs.size());
            for (size_t index = 0; index < num_envs; ++index) {
                environments.env_ptrs[index] = &envs_[index];
            }
            auto pipeline = CreateGraphicsPipeline(pools, key, environments, false);

            std::scoped_lock lock{state.mutex};
            if (pipeline) {
                graphics_cache.emplace(key, std::move(pipeline));
            }
            ++state.built;
            if (state.has_loaded) {
                callback(VideoCore::LoadCallbackStage::Build, state.built, state.total);
            }
        });
        ++state.total;
    };

    VideoCommon::LoadPipelines(stop_loading, pipeline_cache_filename, CACHE_VERSION, load_compute,
                               load_graphics);
    LOG_INFO(Render_Metal, "Preloading {} pipelines from the shader cache", state.total);
    const auto preload_started = std::chrono::steady_clock::now();
    std::unique_lock lock{state.mutex};
    callback(VideoCore::LoadCallbackStage::Build, 0, state.total);
    state.has_loaded = true;
    lock.unlock();

    workers->WaitForRequests(stop_loading);

    // The only way to tell a warm boot that reused compiled code from one that
    // quietly recompiled the lot.
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - preload_started)
                             .count();
    LOG_INFO(Render_Metal, "Preloaded {} pipelines in {} ms", state.total, elapsed);
}

GraphicsPipeline* PipelineCache::CurrentGraphicsPipeline() {
    if (!RefreshStages(graphics_key.unique_hashes)) {
        current_graphics_pipeline = nullptr;
        return nullptr;
    }
    graphics_key.state = graphics_state;
    // Without this a pipeline built outside a macro draw is reused inside one, where the base
    // vertex it reads from the constant buffer was never written, and the reverse.
    graphics_key.hle_replacements = 0;
    if (maxwell3d->engine_state == Tegra::Engines::Maxwell3D::EngineHint::OnHLEMacro) {
        for (const auto& [address, type] : maxwell3d->replace_table) {
            graphics_key.hle_replacements |= 1u << static_cast<u32>(type);
        }
    }

    const auto [pair, is_new] = graphics_cache.try_emplace(graphics_key);
    auto& pipeline = pair->second;

    if (is_new) {
        GraphicsEnvironments environments;
        GetGraphicsEnvironments(environments, graphics_key.unique_hashes);
        pipeline = CreateGraphicsPipeline(main_pools, graphics_key, environments);

        // Recorded to disk so the next run can rebuild it before the game asks.
        // On its own thread because it writes a file mid-draw.
        //
        // xfb pipelines are left out on purpose. The boot preload has no channel
        // state so it would build them without their capture kernel, and that
        // captureless copy would own the key for the whole session and capture
        // nothing.
        if (pipeline && !pipeline_cache_filename.empty() && !graphics_key.state.xfb_enabled) {
            serialization_thread.QueueWork(
                [this, key = graphics_key, envs = environments.envs]() mutable {
                    boost::container::static_vector<const VideoCommon::GenericEnvironment*, 5>
                        env_ptrs;
                    for (size_t index = 0; index < envs.size(); ++index) {
                        if (key.unique_hashes[index] != 0) {
                            env_ptrs.push_back(&envs[index]);
                        }
                    }
                    VideoCommon::SerializePipeline(key, env_ptrs, pipeline_cache_filename,
                                                   CACHE_VERSION);
                    MaybeFlushMetal4Archive();
                });
        }
    }

    current_graphics_pipeline = pipeline && pipeline->IsValid() ? pipeline.get() : nullptr;
    return BuiltPipeline(current_graphics_pipeline);
}

GraphicsPipeline* PipelineCache::BuiltPipeline(GraphicsPipeline* pipeline) const noexcept {
    if (pipeline == nullptr || pipeline->IsBuilt()) {
        return pipeline;
    }
    if (!use_asynchronous_shaders) {
        return pipeline;
    }
    // A handful of vertices is almost always a full-screen quad building a
    // texture later draws sample, so skipping one corrupts everything that reads
    // the result instead of delaying an object by a frame.
    const auto& draw_state = maxwell3d->draw_manager.draw_state;
    if (draw_state.index_buffer.count <= 6 || draw_state.vertex_buffer.count <= 6) {
        return pipeline;
    }
    // A transform feedback draw fills a buffer the next draws read as vertices. Skipping it
    // leaves whatever an earlier capture put there, so those draws come out as spikes.
    if (maxwell3d->regs.transform_feedback_enabled != 0) {
        return pipeline;
    }
    // Counted, the object is missing until its pipeline is ready, so look here
    // if a title shows constant pop-in.
    static std::atomic<u64> skipped{0};
    const u64 count = skipped.fetch_add(1, std::memory_order_relaxed) + 1;
    if (std::has_single_bit(count)) {
        LOG_DEBUG(Render_Metal, "Skipped {} draw(s) whose pipeline was still compiling", count);
    }
    return nullptr;
}

ComputePipeline* PipelineCache::CurrentComputePipeline() {
    const VideoCommon::ShaderInfo* const shader = ComputeShader();
    if (shader == nullptr || kepler_compute == nullptr) {
        return nullptr;
    }

    // The key has to carry the launch geometry and not just the shader hash. The
    // same kernel dispatched with a different threadgroup size or shared memory
    // allocation is a different Metal pipeline.
    const auto& qmd = kepler_compute->launch_description;
    compute_key = ComputePipelineCacheKey{
        .unique_hash = shader->unique_hash,
        .shared_memory_size = qmd.shared_alloc,
        .workgroup_size = {qmd.block_dim_x, qmd.block_dim_y, qmd.block_dim_z},
    };

    const auto MakeLive = [&]() -> std::unique_ptr<ComputePipeline> {
        const GPUVAddr program_base{kepler_compute->regs.code_loc.Address()};
        VideoCommon::ComputeEnvironment env{*kepler_compute, *gpu_memory, program_base,
                                            qmd.program_start};
        env.SetCachedSize(shader->size_bytes);
        auto built = CreateComputePipeline(main_pools, compute_key, env);
        // Recorded so the next run preloads it instead of compiling it again.
        if (built && !pipeline_cache_filename.empty()) {
            serialization_thread.QueueWork(
                [this, key = compute_key, env_ = std::move(env)]() mutable {
                    VideoCommon::SerializePipeline(
                        key, std::array<const VideoCommon::GenericEnvironment*, 1>{&env_},
                        pipeline_cache_filename, CACHE_VERSION);
                    MaybeFlushMetal4Archive();
                });
        }
        return built;
    };

    const auto [pair, is_new] = compute_cache.try_emplace(compute_key);
    auto& pipeline = pair->second;
    if (is_new) {
        pipeline = MakeLive();
    } else if (pipeline == nullptr) {
        // A build can fail just because the guest hasn't written the kernel yet
        // (the decoder reads zeros and throws) and a later dispatch gets it
        // right. Retry a few times, the cap stops an unsupported kernel from
        // re-running the recompiler on every dispatch.
        constexpr u32 MAX_RETRIES = 3;
        u32& attempts = compute_failures[compute_key];
        if (attempts < MAX_RETRIES) {
            ++attempts;
            pipeline = MakeLive();
        }
    }
    return pipeline && pipeline->IsValid() ? pipeline.get() : nullptr;
}

std::unique_ptr<GraphicsPipeline> PipelineCache::CreateGraphicsPipeline(
    ShaderPools& pools, const GraphicsPipelineCacheKey& key,
    const GraphicsEnvironments& environments, bool build_in_parallel) try {
    using Maxwell = Tegra::Engines::Maxwell3D::Regs;

    // No guest-register access from here down, and the pools are a parameter
    // because the disk cache rebuilds pipelines on several threads at once.
    pools.ReleaseContents();

    std::span<Shader::Environment* const> envs = environments.Span();

    // --- Guest bytecode -> IR --------------------------------------------
    std::array<Shader::IR::Program, Maxwell::MaxShaderProgram> programs;
    const bool uses_vertex_a{key.unique_hashes[0] != 0};
    const bool uses_vertex_b{key.unique_hashes[1] != 0};

    size_t env_index{0};
    for (size_t index = 0; index < Maxwell::MaxShaderProgram; ++index) {
        if (key.unique_hashes[index] == 0) {
            continue;
        }
        if (env_index >= envs.size()) {
            // The key names a stage with no environment. Live draws build both
            // together, but an older shader cache file can land here and
            // dereferencing past the span takes the process with it.
            LOG_WARNING(Render_Metal, "Pipeline {:016x} is missing stage {}", key.Hash(), index);
            return nullptr;
        }
        Shader::Environment& env{*envs[env_index]};
        ++env_index;

        const u32 cfg_offset{static_cast<u32>(env.StartAddress() + sizeof(Shader::ProgramHeader))};
        Shader::Maxwell::Flow::CFG cfg(env, pools.flow_block, cfg_offset, index == 0);

        if (!uses_vertex_a || index != 1) {
            programs[index] = Shader::Maxwell::TranslateProgram(pools.inst, pools.block,
                                                                env, cfg, host_info);
        } else {
            auto& program_va{programs[0]};
            auto program_vb{Shader::Maxwell::TranslateProgram(pools.inst, pools.block,
                                                              env, cfg, host_info)};
            programs[index] = Shader::Maxwell::MergeDualVertexPrograms(program_va, program_vb, env);
        }
    }

    // --- IR -> SPIR-V -> MSL ---------------------------------------------

    // Maxwell program index of the geometry stage. Not the index the buffer cache
    // or our own per-stage arrays use for it.
    constexpr size_t GEOMETRY_PROGRAM = 4;

    // Whether the geometry stage really runs as compute dispatches or gets
    // dropped. We need this before anything is emitted because it decides what
    // the VERTEX program is compiled for. MakeRuntimeInfo moves the depth
    // conversion and fixed point size off the vertex shader when a geometry
    // stage exists, and a dropped stage then leaves every vertex in OpenGL-style
    // NDC, outside Metal's 0..1 clip space.
    const bool emulate_geometry = [&] {
        if (!profile.emulate_geometry_as_compute || key.unique_hashes[GEOMETRY_PROGRAM] == 0) {
            return false;
        }
        const Shader::IR::Program& gs = programs[GEOMETRY_PROGRAM];
        if (gs.is_geometry_passthrough) {
            // Only forwards its input, so emulating it would be pure overhead.
            return false;
        }
        if (gs.output_topology != Shader::OutputTopology::TriangleStrip) {
            // The lowering flattens a triangle strip into independent triangles.
            // Point and line output would need a different assembler and a
            // different primitive type on the draw.
            LOG_WARNING(Render_Metal,
                        "Geometry shader not emulated: output topology {} is not a triangle strip",
                        static_cast<u32>(gs.output_topology));
            return false;
        }
        // The captured layout is SPIRV-Cross's stage-out struct, generic
        // locations ascending then gl_Position. We don't know where PointSize
        // and ClipDistance land, and a program writing one shifts everything
        // after it, so refuse instead of guessing.
        const size_t vertex_program = uses_vertex_b ? 1 : 0;
        if (key.unique_hashes[vertex_program] != 0) {
            const Shader::Info& vs = programs[vertex_program].info;
            if (vs.stores.ClipDistances() || vs.stores[Shader::IR::Attribute::PointSize]) {
                LOG_WARNING(Render_Metal,
                            "Geometry shader not emulated: the vertex program writes {}, whose "
                            "position in the captured layout is unverified",
                            vs.stores.ClipDistances() ? "clip distances" : "a point size");
                return false;
            }
        }
        return true;
    }();
    const bool has_geometry = emulate_geometry;

    // Metal has no stream-out stage so transform feedback rides the capture
    // kernel. The guest vertex program runs as compute and a gather kernel moves
    // each varying into the guest's buffers, while the draw still renders
    // through the real vertex function.
    //
    // The XFB layout is read from the registers and not the key, so maxwell3d
    // has to be non-null. It isn't during the disk-cache preload, which is why
    // we never serialize preloaded xfb pipelines (see CurrentGraphicsPipeline).
    const bool want_xfb_capture =
        !emulate_geometry && key.state.xfb_enabled && maxwell3d != nullptr;

    // Read from the registers here and handed to GeometryEmulation below, the
    // gather kernel needs them to place each varying in the guest's layout.
    std::array<Shader::TransformFeedbackVarying, 256> xfb_varyings{};
    u32 xfb_varying_count = 0;
    if (want_xfb_capture) {
        VideoCommon::TransformFeedbackState xfb_state{};
        const auto& regs = maxwell3d->regs;
        for (size_t i = 0; i < xfb_state.layouts.size(); ++i) {
            const auto& control = regs.transform_feedback.controls[i];
            xfb_state.layouts[i] = {
                .stream = control.stream,
                .varying_count = control.varying_count,
                .stride = control.stride,
            };
        }
        xfb_state.varyings = regs.stream_out_layout;
        std::tie(xfb_varyings, xfb_varying_count) =
            VideoCommon::MakeTransformFeedbackVaryings(xfb_state);
    }

    std::array<const Shader::Info*, NUM_PIPELINE_STAGES> infos{};
    std::array<MslShader, NUM_PIPELINE_STAGES> shaders{};
    std::array<const MslShader*, NUM_PIPELINE_STAGES> shader_ptrs{};

    const Shader::IR::Program* previous_stage{};
    Shader::Backend::Bindings binding;
    const BindingMap binding_map{};

    // Filled while walking the stages and used once they've all been seen. The
    // capture kernel comes from the vertex program (emitted first) and the
    // passthrough's varyings from the fragment program (emitted last).
    auto geometry_emulation = (emulate_geometry || want_xfb_capture)
                                  ? std::make_unique<GeometryEmulation>()
                                  : std::unique_ptr<GeometryEmulation>{};
    bool dropped_geometry = false;

    for (size_t index = uses_vertex_a && uses_vertex_b ? 1 : 0; index < Maxwell::MaxShaderProgram;
         ++index) {
        if (key.unique_hashes[index] == 0) {
            continue;
        }
        Shader::IR::Program& program{programs[index]};

        // A Metal render pipeline has only a vertex and a fragment function.
        // Tessellation and geometry use a completely different execution model,
        // so skip them instead of emitting something we can't bind.
        size_t slot{};
        if (program.stage == Shader::Stage::VertexA || program.stage == Shader::Stage::VertexB) {
            slot = static_cast<size_t>(PipelineStage::Vertex);
        } else if (program.stage == Shader::Stage::Fragment) {
            slot = static_cast<size_t>(PipelineStage::Fragment);
        } else if (program.stage == Shader::Stage::Geometry) {
            if (!emulate_geometry) {
                // The primitives this stage would have emitted are just gone, and
                // the vertex stream hits the rasteriser in the form the geometry
                // shader was meant to eat.
                LOG_WARNING(Render_Metal,
                            "Dropping geometry shader: passthrough={}, output_topology={}, "
                            "invocations={}",
                            program.is_geometry_passthrough,
                            static_cast<u32>(program.output_topology), program.invocations);
                dropped_geometry = true;
                continue;
            }
            slot = static_cast<size_t>(PipelineStage::Geometry);
        } else if (program.stage == Shader::Stage::TessellationControl ||
                   program.stage == Shader::Stage::TessellationEval) {
            // Dropped. The patch topology falls through PrimitiveTopology() to a
            // plain triangle list, so control points get rasterised undivided,
            // and unprojected too when the evaluation program was the stage that
            // would have projected them.
            //
            // Metal does have a tessellator so this is a gap and not an
            // impossibility, the domain state already reaches the recompiler.
            static std::atomic<bool> reported{false};
            if (!reported.exchange(true)) {
                LOG_WARNING(Render_Metal,
                            "Dropping tessellation: domain={} spacing={} clockwise={} "
                            "control_points={}. The patch control points are rasterised "
                            "undivided.",
                            key.state.tessellation_domain, key.state.tessellation_spacing,
                            key.state.tessellation_clockwise, key.state.patch_control_points);
            }
            continue;
        } else {
            LOG_WARNING(Render_Metal, "Skipping unsupported shader stage {}",
                        static_cast<u32>(program.stage));
            continue;
        }

        const bool is_geometry = slot == static_cast<size_t>(PipelineStage::Geometry);
        // The xfb varyings are NOT passed into the translation on purpose. Their
        // decorations make SPIRV-Cross emit component-split outputs whose
        // capture stores need an address-space cast MSL rejects, so the kernel
        // fails to build and every capture comes back zeros.
        const auto runtime_info{MakeRuntimeInfo(key.state, program, previous_stage, has_geometry)};
        Shader::Maxwell::ConvertLegacyToGeneric(program, runtime_info);

        // Its own module, compiled and dispatched separately, so it numbers
        // bindings from zero instead of continuing the render stages' run.
        Shader::Backend::Bindings geometry_binding{};
        const std::vector<u32> code{Shader::Backend::SPIRV::EmitSPIRV(
            profile, runtime_info, program, is_geometry ? geometry_binding : binding)};

        // SPIRV-Cross recurses deeply enough to overflow a worker thread's
        // default 512 KiB stack on macOS, so the translation hops to a pthread
        // with SPIRV_CROSS_STACK_SIZE.
        const Shader::Stage stage = program.stage;
        const Shader::Info& shader_info = program.info;

        if (is_geometry) {
            // The recompiler already emitted this with ExecutionModel GLCompute,
            // so by now it's an ordinary kernel and we just compile it.
            geometry_emulation->geometry = RunOnLargeStack([&]() {
                return TranslateGeometryToComputeKernel(device, code, shader_info, binding_map);
            });
            geometry_emulation->geometry_argument_slots =
                geometry_emulation->geometry.argument_buffer_slots;
            geometry_emulation->input_vertices =
                Shader::Backend::SPIRV::GeometryAsComputeInputVertices(runtime_info.input_topology);
            // Mirrors EmitContext::SetupGeometryAsCompute: position, then one
            // slot per generic location up to the highest this program writes.
            u32 generic_count{0};
            for (u32 location = 0; location < 32; ++location) {
                if (program.info.stores.Generic(location)) {
                    generic_count = location + 1;
                }
            }
            geometry_emulation->output_stride =
                Shader::Backend::SPIRV::GeometryAsComputeOutputStride(generic_count);
            geometry_emulation->output_capacity =
                Shader::Backend::SPIRV::GeometryAsComputeOutputCapacity(program.output_vertices);
            // Has to match EmitContext::SetupGeometryAsCompute exactly, one
            // float4 per stored generic location ascending, then gl_Position.
            // Both read previous_stage_stores, which stops them drifting.
            u32 captured{0};
            for (u32 location = 0; location < 32; ++location) {
                if (runtime_info.previous_stage_stores.Generic(location)) {
                    ++captured;
                }
            }
            geometry_emulation->input_stride = captured + 1;

            infos[slot] = &program.info;
            previous_stage = &program;
            continue;
        }

        if ((emulate_geometry || want_xfb_capture) &&
            slot == static_cast<size_t>(PipelineStage::Vertex)) {
            // Under geometry emulation the guest vertex program only runs as a
            // capture kernel, shaders[Vertex] gets the generated passthrough.
            geometry_emulation->capture = RunOnLargeStack([&]() {
                return TranslateVertexToCaptureKernel(device, code, shader_info, binding_map, key.state);
            });
            if (emulate_geometry) {
                infos[slot] = &program.info;
                previous_stage = &program;
                continue;
            }
            // For xfb the kernel is an *addition*, so fall through and translate
            // the vertex program normally as well.
        }

        const bool rasterizes_points =
            key.state.topology_class == MTL::PrimitiveTopologyClassPoint;
        shaders[slot] = RunOnLargeStack([&]() {
            return TranslateSpirvToMsl(device, code, stage, shader_info, binding_map,
                                       rasterizes_points);
        });
        infos[slot] = &program.info;
        shader_ptrs[slot] = &shaders[slot];


        previous_stage = &program;
    }

    if (emulate_geometry) {
        if (infos[static_cast<size_t>(PipelineStage::Geometry)] == nullptr ||
            geometry_emulation->capture.source.empty()) {
            // One kernel missing leaves the other reading or writing a buffer
            // nothing fills, and falling back to a dropped geometry stage isn't
            // an option, the vertex program was compiled without the depth
            // conversion on the assumption that this stage applies it.
            LOG_WARNING(Render_Metal,
                        "Pipeline {:016x} wanted geometry emulation but is missing a stage",
                        key.Hash());
            return nullptr;
        }

        // Which varyings the passthrough re-exports, what the geometry program
        // stores AND the fragment program reads. Not all 32, that alone blows
        // Metal's per-stage varying budget.
        std::bitset<32> passthrough_locations;
        const Shader::Info& gs_info = *infos[static_cast<size_t>(PipelineStage::Geometry)];
        const Shader::Info* const fs_info = infos[static_cast<size_t>(PipelineStage::Fragment)];
        for (u32 location = 0; location < 32; ++location) {
            if (gs_info.stores.Generic(location) &&
                (fs_info == nullptr || fs_info->loads.Generic(location))) {
                passthrough_locations.set(location);
            }
        }
        // A depth-only pass has no fragment program so there are no varyings to
        // carry, position alone is what it rasterises.
        if (fs_info == nullptr) {
            passthrough_locations.reset();
        }

        constexpr size_t VERTEX_SLOT = static_cast<size_t>(PipelineStage::Vertex);
        shaders[VERTEX_SLOT] = BuildGeometryPassthroughVertexShader(
            geometry_emulation->output_stride, passthrough_locations);
        shader_ptrs[VERTEX_SLOT] = &shaders[VERTEX_SLOT];

        LOG_INFO(Render_Metal,
                 "Pipeline {:016x} emulates its geometry stage: {} vertices in, up to {} out, "
                 "capture stride {}, {} varyings",
                 key.Hash(), geometry_emulation->input_vertices,
                 geometry_emulation->output_capacity, geometry_emulation->input_stride,
                 passthrough_locations.count());
    }

    if (shader_ptrs[static_cast<size_t>(PipelineStage::Vertex)] == nullptr) {
        LOG_WARNING(Render_Metal, "Pipeline {:016x} has no vertex stage", key.Hash());
        return nullptr;
    }

    if (want_xfb_capture && geometry_emulation != nullptr) {
        constexpr size_t VERTEX_SLOT = static_cast<size_t>(PipelineStage::Vertex);
        const Shader::Info* const vs_info = infos[VERTEX_SLOT];
        if (geometry_emulation->capture.source.empty() || vs_info == nullptr) {
            // The pipeline still renders, the guest's feedback buffers just stay
            // unwritten.
            LOG_WARNING(Render_Metal, "Pipeline {:016x} wanted XFB capture but has no kernel",
                        key.Hash());
            geometry_emulation.reset();
        } else {
            geometry_emulation->xfb_only = true;

            // Capture buffer layout, stored generics in ascending location order
            // then gl_Position last. Has to match what SPIRV-Cross emits for
            // capture_output_to_buffer, see SetupGeometryAsCompute.
            geometry_emulation->xfb_slot_of_generic.fill(-1);
            s8 next = 0;
            for (u32 location = 0; location < 32; ++location) {
                if (vs_info->stores.Generic(location)) {
                    geometry_emulation->xfb_slot_of_generic[location] = next++;
                }
            }
            geometry_emulation->xfb_position_slot = next;

            // Measure the stride off the generated struct, never off the stores
            // mask. SPIRV-Cross appends members (gl_PointSize) the program never
            // stored, and a wrong stride drifts every vertex after the first.
            {
                u32 struct_bytes = 0;
                const std::string& src = geometry_emulation->capture.source;
                const size_t begin = src.find("struct main0_out");
                const size_t end = begin == std::string::npos ? std::string::npos
                                                              : src.find("};", begin);
                if (begin != std::string::npos && end != std::string::npos) {
                    size_t cursor = begin;
                    while (cursor < end) {
                        const size_t line_end = src.find('\n', cursor);
                        const std::string_view line{src.data() + cursor,
                                                    std::min(line_end, end) - cursor};
                        if (line.find("float4 ") != std::string_view::npos) {
                            struct_bytes += 16;
                        } else if (line.find("float3 ") != std::string_view::npos) {
                            struct_bytes += 12;
                        } else if (line.find("float2 ") != std::string_view::npos) {
                            struct_bytes += 8;
                        } else if (line.find("float ") != std::string_view::npos ||
                                   line.find("uint ") != std::string_view::npos) {
                            struct_bytes += 4;
                        }
                        cursor = line_end == std::string::npos ? end : line_end + 1;
                    }
                }
                // A location split into partial members (float2 halves and the
                // like) breaks the one-float4-per-location slot table. Capture
                // nothing instead of scattering from wrong offsets.
                const bool split_members =
                    begin != std::string::npos && end != std::string::npos &&
                    (src.find("float3 ", begin) < end || src.find("float2 ", begin) < end);
                if (struct_bytes == 0 || split_members) {
                    LOG_WARNING(Render_Metal,
                                "Pipeline {:016x} XFB capture skipped: capture struct is {} ",
                                key.Hash(), struct_bytes == 0 ? "unmeasurable" : "split");
                    geometry_emulation.reset();
                } else {
                    geometry_emulation->input_stride = Common::AlignUp(struct_bytes, 16) / 16;
                }
            }

            // The guest's capture table, from the registers. This key doesn't
            // carry XFB state.
            if (geometry_emulation != nullptr) {
            const auto& varyings = xfb_varyings;
            const u32 count = xfb_varying_count;
            geometry_emulation->xfb_varyings = varyings;
            geometry_emulation->xfb_varying_count = count;

            LOG_INFO(Render_Metal,
                     "Pipeline {:016x} captures transform feedback: {} varyings, capture stride "
                     "{} float4s",
                     key.Hash(), count, geometry_emulation->input_stride);

            if (geometry_emulation->capture.argument_buffer_slots !=
                shaders[VERTEX_SLOT].argument_buffer_slots) {
                // The capture kernel is fed the render vertex stage's argument
                // buffer, so a layout disagreement makes every resource the
                // kernel reads come from the wrong slot.
                LOG_WARNING(Render_Metal,
                            "Pipeline {:016x} XFB capture argument layout drift", key.Hash());
            }

            }
        }
    }

    // The MSL compile runs on the worker pool and we return the pipeline right
    // away, the draw path waits for the build on the scheduler thread. A null
    // worker compiles inline, which is what the disk loader wants. It's already
    // on a worker, and re-queuing would have it report every pipeline as built
    // the moment it was queued.
    auto pipeline = std::make_unique<GraphicsPipeline>(
        device, scheduler, build_in_parallel ? workers.get() : nullptr, key.state, infos,
        shader_ptrs, &shader_notify, std::move(geometry_emulation));
    if (dropped_geometry) {
        pipeline->SetDroppedGeometry();
    }
    LOG_DEBUG(Render_Metal, "Queued graphics pipeline {:016x}", key.Hash());
    return pipeline;
} catch (const std::exception& error) {
    LOG_ERROR(Render_Metal, "Failed to build a graphics pipeline: {}", error.what());
    return nullptr;
}

std::unique_ptr<ComputePipeline> PipelineCache::CreateComputePipeline(
    ShaderPools& pools, const ComputePipelineCacheKey& key, Shader::Environment& env) try {
    // Compute builds synchronously so the notify lives here instead of on the
    // pipeline. It has to balance on the throwing paths too.
    shader_notify.MarkShaderBuilding();
    SCOPE_EXIT {
        shader_notify.MarkShaderComplete();
    };

    pools.ReleaseContents();

    Shader::Maxwell::Flow::CFG cfg{env, pools.flow_block, env.StartAddress()};
    auto program{Shader::Maxwell::TranslateProgram(pools.inst, pools.block, env, cfg, host_info)};

    // A kernel asking for more threadgroup memory than the device has can't be
    // dispatched at all, so clamp instead of letting the pipeline fail to build.
    const u32 max_shared = static_cast<u32>(device.GetMaxThreadgroupMemoryLength());
    if (program.shared_memory_size > max_shared) {
        LOG_WARNING(Render_Metal,
                    "Compute kernel {:#016x} wants {} KiB of threadgroup memory, device allows {} "
                    "KiB - clamping",
                    key.unique_hash, program.shared_memory_size / 1024, max_shared / 1024);
        program.shared_memory_size = max_shared;
    }

    const std::vector<u32> code{Shader::Backend::SPIRV::EmitSPIRV(profile, program)};

    const BindingMap binding_map{};
    const Shader::Info& info = program.info;
    MslShader msl = RunOnLargeStack([&]() {
        return TranslateSpirvToMsl(device, code, program.stage, info, binding_map,
                                   /*rasterizes_points=*/false);
    });

    auto pipeline = std::make_unique<ComputePipeline>(device, scheduler, program.info,
                                                      program.workgroup_size, msl);
    if (!pipeline->IsValid()) {
        return nullptr;
    }
    LOG_DEBUG(Render_Metal, "Built compute pipeline {:#016x}", key.unique_hash);
    return pipeline;
} catch (const std::exception& error) {
    // Report what the decoder was looking at. "Invalid insn 0x0" means we read
    // memory holding nothing, not an unimplemented opcode.
    const GPUVAddr base = kepler_compute != nullptr ? kepler_compute->regs.code_loc.Address() : 0;
    const u32 start = kepler_compute != nullptr ? kepler_compute->launch_description.program_start
                                                : 0;
    u64 first_words = 0;
    bool mapped = false;
    u64 cpu_addr = 0;
    if (gpu_memory != nullptr) {
        first_words = gpu_memory->Read<u64>(base + start);
        // An unmapped read and a mapped-but-zero read both come back zero but
        // mean opposite things, so report `mapped` separately.
        const auto translated = gpu_memory->GpuToCpuAddress(base + start);
        mapped = translated.has_value();
        cpu_addr = mapped ? *translated : 0;
    }
    LOG_ERROR(Render_Metal,
              "Failed to build compute pipeline {:#016x} at gpu {:#x}+{:#x} (mapped={} cpu {:#x}, "
              "first words {:#018x}): {}",
              key.unique_hash, base, start, mapped, cpu_addr, first_words, error.what());
    return nullptr;
}

} // namespace Metal
