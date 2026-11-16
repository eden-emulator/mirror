// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <format>
#include <fstream>
#include <filesystem>
#include <cstdlib>
#include <array>
#include <atomic>
#include <system_error>
#include <cstdio>
#include <exception>
#include <mutex>
#include <string>
#include <unordered_map>
#include <string_view>
#include <vector>

#include <pthread.h>

#include <spirv_msl.hpp>

#include "common/cityhash.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "common/settings.h"
#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "video_core/metal_common/metal_device.h"
#include "video_core/renderer_metal/mtl_shader_util.h"

namespace Metal {
namespace {
spv::ExecutionModel ToExecutionModel(Shader::Stage stage) {
    switch (stage) {
    case Shader::Stage::VertexA:
    case Shader::Stage::VertexB:
        return spv::ExecutionModelVertex;
    case Shader::Stage::TessellationControl:
        return spv::ExecutionModelTessellationControl;
    case Shader::Stage::TessellationEval:
        return spv::ExecutionModelTessellationEvaluation;
    case Shader::Stage::Geometry:
        return spv::ExecutionModelGeometry;
    case Shader::Stage::Fragment:
        return spv::ExecutionModelFragment;
    case Shader::Stage::Compute:
        return spv::ExecutionModelGLCompute;
    }
    return spv::ExecutionModelVertex;
}

MTL::LanguageVersion ToLanguageVersion(u32 msl_version) {
    // Same major*10000 + minor*100 encoding SPIRV-Cross uses.
    const u32 major = msl_version / 10000;
    const u32 minor = (msl_version % 10000) / 100;

    if (major > 3 || (major == 3 && minor >= 1)) {
        return MTL::LanguageVersion3_1;
    }
    if (major >= 3) {
        return MTL::LanguageVersion3_0;
    }
    if (major == 2 && minor >= 4) {
        return MTL::LanguageVersion2_4;
    }
    return MTL::LanguageVersion2_4;
}

void ReportUnsupportedFeatures(Shader::Stage stage, const Shader::Info& info) {
    if (stage == Shader::Stage::Geometry) {
        LOG_WARNING(Shader_MSL, "Geometry shaders have no Metal equivalent");
    }
    if (info.stores_tess_level_outer || info.stores_tess_level_inner) {
        LOG_WARNING(Shader_MSL, "Tessellation uses a different model in Metal");
    }
    if (info.uses_typeless_image_reads || info.uses_typeless_image_writes) {
        LOG_WARNING(Shader_MSL, "Typeless image access is not expressible in MSL");
    }
}

// Decorate the position builtin `Invariant`, SPIRV-Cross turns that into MSL's
// `[[position, invariant]]`. Without it Apple's compiler can schedule position math
// per pipeline, so an EQUAL depth test between two passes kills every fragment.
void ForcePositionInvariance(spirv_cross::CompilerMSL& compiler) {
    for (const spirv_cross::VariableID id : compiler.get_active_interface_variables()) {
        // Standalone `out gl_Position`, which is how the recompiler emits it.
        if (compiler.has_decoration(id, spv::DecorationBuiltIn) &&
            compiler.get_decoration(id, spv::DecorationBuiltIn) == spv::BuiltInPosition) {
            compiler.set_decoration(id, spv::DecorationInvariant);
            continue;
        }

        const spirv_cross::SPIRType& type = compiler.get_type_from_variable(id);
        if (type.basetype != spirv_cross::SPIRType::Struct) {
            continue;
        }
        for (u32 member = 0; member < type.member_types.size(); ++member) {
            if (compiler.has_member_decoration(type.self, member, spv::DecorationBuiltIn) &&
                compiler.get_member_decoration(type.self, member, spv::DecorationBuiltIn) ==
                    spv::BuiltInPosition) {
                compiler.set_member_decoration(type.self, member, spv::DecorationInvariant);
            }
        }
    }
}

} // Anonymous namespace

MslShader TranslateSpirvToMsl(const Device& device, std::span<const u32> spirv,
                              Shader::Stage stage, const Shader::Info& info,
                              [[maybe_unused]] const BindingMap& bindings,
                              bool rasterizes_points) {
    if (spirv.empty()) {
        throw Exception("Cannot translate an empty SPIR-V module");
    }

    MslShader result{};
    result.stage = stage;
    ReportUnsupportedFeatures(stage, info);

    try {
        spirv_cross::CompilerMSL compiler{spirv.data(), spirv.size()};

        spirv_cross::CompilerMSL::Options options = compiler.get_msl_options();

        options.platform = device.IsMobile() ? spirv_cross::CompilerMSL::Options::iOS
                                             : spirv_cross::CompilerMSL::Options::macOS;
        options.msl_version = device.GetMslVersion();

        options.argument_buffers = true;
        options.argument_buffers_tier =
            device.SupportsArgumentBuffersTier2()
                ? spirv_cross::CompilerMSL::Options::ArgumentBuffersTier::Tier2
                : spirv_cross::CompilerMSL::Options::ArgumentBuffersTier::Tier1;

        // We bind every resource the recompiler recorded, so SPIRV-Cross has to lay out
        // every one too. Otherwise the dense [[id(N)]] counter shifts and we sample the
        // wrong resource.
        options.force_active_argument_buffer_resources = true;

        // Don't combine enable_decoration_binding with argument buffers, the argument
        // buffer analysis throws "nullptr" out of Variant::get.
        options.enable_decoration_binding = !options.argument_buffers;

        options.pad_fragment_output_components = true;

        // Two Metal driver bugs SPIRV-Cross works around: an explicit level() on
        // depth2d_array::sample_compare() is biased (shadow cascade acne), and
        // stores from threads that already discarded still land.
        options.sample_dref_lod_array_as_grad = true;
        options.check_discarded_frag_stores = true;

        // MSL's texture1d has no mipmaps and no LOD select. ToTextureType in
        // mtl_texture_cache.cpp maps shader 1D to 2D as well, the two MUST agree.
        options.texture_1D_as_2D = true;

        options.force_native_arrays = true;

        options.texture_buffer_native = true;

        // Apple GPUs compute cube-map gradients differently, without this we get
        // seams along cube face boundaries.
        options.agx_manual_cube_grad_fixup = true;

        options.ios_use_simdgroup_functions = true;

        // A guest vertex shader may write gl_PointSize on a triangle draw, which
        // Vulkan ignores but Metal rejects the whole pipeline state for.
        options.enable_point_size_builtin = rasterizes_points;

        // Strip NoContraction. SPIRV-Cross turns it into [[clang::optnone]]
        // spvFMul/spvFAdd helpers that cost us about a third of the frame rate. We
        // tried honouring it and measured it, fixed nothing. Don't re-test without
        // a reason.
        for (u32 id = 0; id < compiler.get_current_id_bound(); ++id) {
            compiler.unset_decoration(spirv_cross::ID(id), spv::DecorationNoContraction);
        }

        options.invariant_float_math = false;

        compiler.set_msl_options(options);

        if (ToExecutionModel(stage) != spv::ExecutionModelFragment &&
            ToExecutionModel(stage) != spv::ExecutionModelGLCompute) {
            ForcePositionInvariance(compiler);
        }

        result.source = compiler.compile();

        result.entry_point = compiler.get_cleansed_entry_point_name(
            compiler.get_entry_points_and_stages().front().name, ToExecutionModel(stage));

        if (options.argument_buffers) {
            const spirv_cross::ShaderResources resources = compiler.get_shader_resources();
            u32 highest = 0;
            bool any = false;
            const auto note = [&](const spirv_cross::Resource& resource, bool has_sampler) {
                any = true;
                highest = (std::max)(highest,
                                     compiler.get_automatic_msl_resource_binding(resource.id));
                if (has_sampler) {
                    highest = (std::max)(
                        highest,
                        compiler.get_automatic_msl_resource_binding_secondary(resource.id));
                }
            };
            for (const auto& resource : resources.uniform_buffers) {
                note(resource, false);
            }
            for (const auto& resource : resources.storage_buffers) {
                note(resource, false);
            }
            for (const auto& resource : resources.sampled_images) {
                note(resource, true);
            }
            for (const auto& resource : resources.separate_images) {
                note(resource, false);
            }
            for (const auto& resource : resources.storage_images) {
                note(resource, false);
            }
            result.argument_buffer_slots = any ? highest + 1 : 0;
        }
    } catch (const spirv_cross::CompilerError& error) {
        // A bare "nullptr" is Variant::get asked for an ID that doesn't exist.
        throw Exception(std::string{"SPIRV-Cross failed: "} + error.what());
    } catch (const std::exception& error) {
        throw Exception(std::string{"SPIR-V to MSL translation failed: "} + error.what());
    }

    if (result.source.empty()) {
        throw Exception("SPIRV-Cross produced no output");
    }

    return result;
}

namespace {
/// One member of the generated `struct main0_in`.
struct StageInMember {
    std::string type;
    std::string name;
    u32 location{};    ///< The N in [[attribute(N)]]
};

std::vector<StageInMember> ParseStageInMembers(const std::string& source) {
    std::vector<StageInMember> members;
    const size_t begin = source.find("struct main0_in");
    if (begin == std::string::npos) {
        return members;
    }
    const size_t end = source.find("};", begin);
    if (end == std::string::npos) {
        return members;
    }

    size_t cursor = begin;
    while (cursor < end) {
        const size_t line_end = std::min(source.find('\n', cursor), end);
        const std::string_view line{source.data() + cursor, line_end - cursor};
        cursor = line_end + 1;

        const size_t attribute = line.find("[[attribute(");
        if (attribute == std::string_view::npos) {
            continue;
        }
        const size_t number = attribute + std::string_view{"[[attribute("}.size();
        const size_t number_end = line.find(')', number);
        if (number_end == std::string_view::npos) {
            continue;
        }

        std::string_view head = line.substr(0, attribute);
        while (!head.empty() && (head.front() == ' ' || head.front() == '\t')) {
            head.remove_prefix(1);
        }
        while (!head.empty() && head.back() == ' ') {
            head.remove_suffix(1);
        }
        const size_t space = head.find(' ');
        if (space == std::string_view::npos) {
            continue;
        }

        StageInMember member{};
        member.type = std::string{head.substr(0, space)};
        member.name = std::string{head.substr(space + 1)};
        member.location = static_cast<u32>(
            std::strtoul(std::string{line.substr(number, number_end - number)}.c_str(), nullptr, 10));
        if (!member.name.empty()) {
            members.push_back(std::move(member));
        }
    }
    return members;
}

struct FormatLayout {
    const char* scalar{nullptr};
    u32 components{0};
    bool normalized{false};
    bool is_signed{false};
    /// For formats that are one packed word: an MSL expression taking `w` as that word.
    const char* packed_expression{nullptr};
};

FormatLayout LayoutOf(MTL::VertexFormat format) {
    using F = MTL::VertexFormat;
    switch (format) {
    case F::VertexFormatUChar:              return {"uchar", 1, false, false, nullptr};
    case F::VertexFormatUChar2:             return {"uchar", 2, false, false, nullptr};
    case F::VertexFormatUChar3:             return {"uchar", 3, false, false, nullptr};
    case F::VertexFormatUChar4:             return {"uchar", 4, false, false, nullptr};
    case F::VertexFormatUCharNormalized:    return {"uchar", 1, true, false, nullptr};
    case F::VertexFormatUChar2Normalized:   return {"uchar", 2, true, false, nullptr};
    case F::VertexFormatUChar3Normalized:   return {"uchar", 3, true, false, nullptr};
    case F::VertexFormatUChar4Normalized:   return {"uchar", 4, true, false, nullptr};
    case F::VertexFormatChar:               return {"char", 1, false, true, nullptr};
    case F::VertexFormatChar2:              return {"char", 2, false, true, nullptr};
    case F::VertexFormatChar3:              return {"char", 3, false, true, nullptr};
    case F::VertexFormatChar4:              return {"char", 4, false, true, nullptr};
    case F::VertexFormatCharNormalized:     return {"char", 1, true, true, nullptr};
    case F::VertexFormatChar2Normalized:    return {"char", 2, true, true, nullptr};
    case F::VertexFormatChar3Normalized:    return {"char", 3, true, true, nullptr};
    case F::VertexFormatChar4Normalized:    return {"char", 4, true, true, nullptr};
    case F::VertexFormatUShort:             return {"ushort", 1, false, false, nullptr};
    case F::VertexFormatUShort2:            return {"ushort", 2, false, false, nullptr};
    case F::VertexFormatUShort3:            return {"ushort", 3, false, false, nullptr};
    case F::VertexFormatUShort4:            return {"ushort", 4, false, false, nullptr};
    case F::VertexFormatUShortNormalized:   return {"ushort", 1, true, false, nullptr};
    case F::VertexFormatUShort2Normalized:  return {"ushort", 2, true, false, nullptr};
    case F::VertexFormatUShort3Normalized:  return {"ushort", 3, true, false, nullptr};
    case F::VertexFormatUShort4Normalized:  return {"ushort", 4, true, false, nullptr};
    case F::VertexFormatShort:              return {"short", 1, false, true, nullptr};
    case F::VertexFormatShort2:             return {"short", 2, false, true, nullptr};
    case F::VertexFormatShort3:             return {"short", 3, false, true, nullptr};
    case F::VertexFormatShort4:             return {"short", 4, false, true, nullptr};
    case F::VertexFormatShortNormalized:    return {"short", 1, true, true, nullptr};
    case F::VertexFormatShort2Normalized:   return {"short", 2, true, true, nullptr};
    case F::VertexFormatShort3Normalized:   return {"short", 3, true, true, nullptr};
    case F::VertexFormatShort4Normalized:   return {"short", 4, true, true, nullptr};
    case F::VertexFormatHalf:               return {"half", 1, false, true, nullptr};
    case F::VertexFormatHalf2:              return {"half", 2, false, true, nullptr};
    case F::VertexFormatHalf3:              return {"half", 3, false, true, nullptr};
    case F::VertexFormatHalf4:              return {"half", 4, false, true, nullptr};
    case F::VertexFormatFloat:              return {"float", 1, false, true, nullptr};
    case F::VertexFormatFloat2:             return {"float", 2, false, true, nullptr};
    case F::VertexFormatFloat3:             return {"float", 3, false, true, nullptr};
    case F::VertexFormatFloat4:             return {"float", 4, false, true, nullptr};
    case F::VertexFormatInt:                return {"int", 1, false, true, nullptr};
    case F::VertexFormatInt2:               return {"int", 2, false, true, nullptr};
    case F::VertexFormatInt3:               return {"int", 3, false, true, nullptr};
    case F::VertexFormatInt4:               return {"int", 4, false, true, nullptr};
    case F::VertexFormatUInt:               return {"uint", 1, false, false, nullptr};
    case F::VertexFormatUInt2:              return {"uint", 2, false, false, nullptr};
    case F::VertexFormatUInt3:              return {"uint", 3, false, false, nullptr};
    case F::VertexFormatUInt4:              return {"uint", 4, false, false, nullptr};
    // Single 32-bit words. No MSL type for them so we unpack by hand, component
    // order matches Metal's own decode.
    case F::VertexFormatUInt1010102Normalized:
        return {nullptr, 4, true, false,
                "float4(float(w & 1023u) / 1023.0, float((w >> 10) & 1023u) / 1023.0, "
                "float((w >> 20) & 1023u) / 1023.0, float((w >> 30) & 3u) / 3.0)"};
    case F::VertexFormatInt1010102Normalized:
        return {nullptr, 4, true, true,
                "float4(max(float(int(w << 22) >> 22) / 511.0, -1.0), "
                "max(float(int(w << 12) >> 22) / 511.0, -1.0), "
                "max(float(int(w << 2) >> 22) / 511.0, -1.0), "
                "max(float(int(w) >> 30), -1.0))"};
    case F::VertexFormatFloatRG11B10:
        return {nullptr, 3, false, true,
                "float4(as_type<float>((w & 2047u) << 17), "
                "as_type<float>(((w >> 11) & 2047u) << 17), "
                "as_type<float>(((w >> 22) & 1023u) << 18), 1.0)"};
    default:
        return {};
    }
}

u32 FetchedBytes(const FormatLayout& layout) {
    if (layout.packed_expression != nullptr) {
        return 4;
    }
    const std::string_view scalar{layout.scalar};
    const u32 width = (scalar == "uchar" || scalar == "char")     ? 1
                      : (scalar == "ushort" || scalar == "short" || scalar == "half") ? 2
                                                                                      : 4;
    return width * layout.components;
}

// MSL expression for one attribute's value: decode `format` at `base`, then widen to
// four components filling with (0, 0, 0, 1) the way MTLVertexDescriptor would.
std::string EmitAttributeFetch(MTL::VertexFormat format, const std::string& base,
                               const std::string& target_type, const std::string& guard) {
    const FormatLayout layout = LayoutOf(format);
    if (layout.components == 0) {
        return {};
    }

    const bool integral_target = target_type.rfind("float", 0) != 0;

    if (layout.packed_expression != nullptr) {
        // These only ever decode to floats.
        if (integral_target) {
            return {};
        }
        return std::format("({{ const uint w = *(device const uint*)({}); {}; }})", base,
                           layout.packed_expression);
    }

    // packed_* so an attribute left unaligned by the stream's stride still loads. The
    // natural vector types want alignment the guest never promises.
    const std::string load =
        layout.components == 1
            ? std::format("(*(device const {}*)({}))", layout.scalar, base)
            : std::format("(*(device const packed_{}{}*)({}))", layout.scalar,
                          layout.components, base);

    std::string value;
    if (integral_target) {
        value = std::format("{}{}({})", target_type.substr(0, target_type.size() - 1),
                            layout.components, load);
    } else if (layout.normalized) {
        const double denominator = layout.is_signed ? ((1u << (
            std::string_view{layout.scalar} == "char" ? 7 : 15)) - 1)
                                                    : ((std::string_view{layout.scalar} == "uchar"
                                                            ? 255u
                                                            : 65535u));
        value = std::format("float{}({}) / {:.1f}", layout.components, load, denominator);
        if (layout.is_signed) {
            value = std::format("max({}, float{}(-1.0))", value, layout.components);
        }
    } else {
        value = std::format("float{}({})", layout.components, load);
    }

    const std::string one = integral_target ? "1" : "1.0";
    const std::string zero = integral_target ? "0" : "0.0";
    std::string widened;
    switch (layout.components) {
    case 1:
        widened = std::format("{}({}, {}, {}, {})", target_type, value, zero, zero, one);
        break;
    case 2:
        widened = std::format("{}({}, {}, {})", target_type, value, zero, one);
        break;
    case 3:
        widened = std::format("{}({}, {})", target_type, value, one);
        break;
    default:
        widened = std::format("{}({})", target_type, value);
        break;
    }

    // Metal's stage-in bounds checked this for us, raw addressing doesn't, and an
    // unchecked read faults the GPU instead of just drawing wrong.
    return std::format("(({} + {}u <= {}) ? ({}) : {}({}, {}, {}, {}))", guard,
                       FetchedBytes(layout), "spvLimit", widened, target_type, zero, zero, zero,
                       one);
}

// Replace the capture kernel's [[stage_in]] input with an explicit fetch. Metal 4
// dropped compute stage-in, and a kernel declaring it doesn't fail, every attribute
// just reads zero. Returns false and leaves source alone when the layout can't be
// expressed, the caller then declines the capture instead of reading wrong memory.
bool InjectManualVertexFetch(std::string& source, const FixedPipelineState& state) {
    const std::vector<StageInMember> members = ParseStageInMembers(source);
    if (members.empty()) {
        // Nothing to fetch, and no stage-in parameter to remove.
        return source.find("[[stage_in]]") == std::string::npos;
    }

    std::string parameters;
    std::string body;
    std::array<bool, MAX_VERTEX_BUFFERS> wanted{};

    for (const StageInMember& member : members) {
        if (member.location >= MAX_VERTEX_ATTRIBUTES) {
            return false;
        }
        const auto& attribute = state.attributes[member.location];
        if (attribute.format == MTL::VertexFormatInvalid) {
            // Declared by the shader, not supplied by the guest.
            body += std::format("    in.{} = {}(0, 0, 0, 1);\n", member.name, member.type);
            continue;
        }
        if (attribute.buffer_index >= MAX_VERTEX_BUFFERS) {
            return false;
        }
        const auto& binding = state.bindings[attribute.buffer_index];
        const u32 slot = BindingMap::VERTEX_BUFFER_BASE + attribute.buffer_index;
        if (!binding.enabled || slot > BindingMap::MAX_VERTEX_BUFFER_SLOT ||
            IsCaptureReservedBufferSlot(slot)) {
            return false;
        }

        const std::string index =
            binding.divisor == 0
                ? "spvVertexIndex"
                : (binding.divisor == 1 ? "spvInstanceIndex"
                                        : std::format("(spvInstanceIndex / {}u)", binding.divisor));

        const std::string offset = std::format("{}u + {}u * {}", attribute.offset, binding.stride,
                                               index);
        const std::string base = std::format("spvStream{} + spvOffset", attribute.buffer_index);
        const std::string fetch =
            EmitAttributeFetch(attribute.format, base, member.type, "spvOffset");
        if (fetch.empty()) {
            return false;
        }
        wanted[attribute.buffer_index] = true;
        body += std::format("    {{\n        const uint spvOffset = {};\n"
                            "        const uint spvLimit = spvStreamLimits[{}];\n"
                            "        in.{} = {};\n    }}\n",
                            offset, attribute.buffer_index, member.name, fetch);
    }

    for (u32 buffer = 0; buffer < MAX_VERTEX_BUFFERS; ++buffer) {
        if (wanted[buffer]) {
            parameters += std::format("device const uchar* spvStream{} [[buffer({})]], ", buffer,
                                      BindingMap::VERTEX_BUFFER_BASE + buffer);
        }
    }
    parameters += std::format("device const uint* spvOrder [[buffer({})]], ",
                              CAPTURE_INDEX_BUFFER_INDEX);
    parameters += std::format("constant uint* spvStreamLimits [[buffer({})]], ",
                              CAPTURE_LIMITS_BUFFER_INDEX);

    const std::string stage_in = "main0_in in [[stage_in]], ";
    const size_t at = source.find(stage_in);
    if (at == std::string::npos) {
        return false;
    }
    source.replace(at, stage_in.size(), parameters);

    // [[grid_size]] is the STAGE-IN region size, which SPIRV-Cross bounds checks every
    // thread against. With no stage-in it reads zero and every thread returns before its
    // first store. [[threads_per_grid]] is what we pass to dispatchThreads.
    const std::string grid_size = "[[grid_size]]";
    const size_t grid_at = source.find(grid_size);
    if (grid_at == std::string::npos) {
        return false;
    }
    source.replace(grid_at, grid_size.size(), "[[threads_per_grid]]");

    const std::string guard = "    if (any(gl_GlobalInvocationID >= spvStageInputSize))\n"
                              "        return;\n";
    const size_t guard_at = source.find(guard);
    if (guard_at == std::string::npos) {
        return false;
    }
    const std::string prologue =
        "    const uint spvVertexIndex = spvOrder[gl_GlobalInvocationID.x];\n"
        "    const uint spvInstanceIndex = gl_GlobalInvocationID.y;\n"
        "    main0_in in;\n" +
        body;
    source.insert(guard_at + guard.size(), prologue);
    return true;
}

/// SPIRV-Cross takes the base vertex and instance of a captured vertex shader from
/// [[grid_origin]], which a Metal 4 compute encoder cannot set, so they always read zero.
/// Guest shaders index bone palettes with them, which skinned every mesh with the first
/// one's bones. They come from the limits buffer instead, see CAPTURE_DRAW_BASE_SLOT. The
/// vertex index is the expanded index the attributes are fetched with, not the thread id.
bool ProvideCaptureDrawParameters(std::string& source) {
    const std::string param = "uint3 spvDispatchBase [[grid_origin]]";
    const size_t param_at = source.find(param);
    if (param_at == std::string::npos) {
        return true;
    }
    size_t erase_at = param_at;
    size_t erase_len = param.size();
    if (source.compare(param_at + param.size(), 2, ", ") == 0) {
        erase_len += 2;
    } else if (param_at >= 2 && source.compare(param_at - 2, 2, ", ") == 0) {
        erase_at -= 2;
        erase_len += 2;
    }
    source.erase(erase_at, erase_len);

    const size_t body_at = source.find("\n{\n", erase_at);
    if (body_at == std::string::npos) {
        return false;
    }
    source.insert(body_at + 3,
                  std::format("    const uint3 spvDispatchBase = uint3(spvStreamLimits[{}], "
                              "spvStreamLimits[{}], 0u);\n",
                              CAPTURE_DRAW_BASE_SLOT, CAPTURE_DRAW_BASE_SLOT + 1));

    const std::string thread_index = "gl_GlobalInvocationID.x + spvDispatchBase.x;";
    for (size_t at = source.find(thread_index); at != std::string::npos;
         at = source.find(thread_index, at)) {
        source.replace(at, thread_index.size(), "spvOrder[gl_GlobalInvocationID.x];");
    }
    return true;
}

} // Anonymous namespace

MslShader TranslateVertexToCaptureKernel(const Device& device, std::span<const u32> spirv,
                                         const Shader::Info& info,
                                         [[maybe_unused]] const BindingMap& bindings,
                                         const FixedPipelineState& state) {
    if (spirv.empty()) {
        throw Exception("Cannot translate an empty SPIR-V module");
    }

    MslShader result{};
    // A compute shader, despite starting life as a vertex program.
    result.stage = Shader::Stage::Compute;

    try {
        spirv_cross::CompilerMSL compiler{spirv.data(), spirv.size()};
        spirv_cross::CompilerMSL::Options options = compiler.get_msl_options();

        options.platform = device.IsMobile() ? spirv_cross::CompilerMSL::Options::iOS
                                             : spirv_cross::CompilerMSL::Options::macOS;
        options.msl_version = device.GetMslVersion();

        // Emit the vertex program as a kernel writing its outputs to a buffer.
        options.vertex_for_tessellation = true;
        options.capture_output_to_buffer = true;
        // Explicit rather than relying on SPIRV-Cross's identical default.
        options.shader_output_buffer_index = CAPTURE_OUTPUT_BUFFER_INDEX;
        // A capture rasterizes nothing.
        options.disable_rasterization = true;

        options.argument_buffers = true;
        options.argument_buffers_tier =
            device.SupportsArgumentBuffersTier2()
                ? spirv_cross::CompilerMSL::Options::ArgumentBuffersTier::Tier2
                : spirv_cross::CompilerMSL::Options::ArgumentBuffersTier::Tier1;

        options.force_active_argument_buffer_resources = true;
        options.enable_decoration_binding = !options.argument_buffers;
        options.force_native_arrays = true;
        options.texture_buffer_native = true;
        options.agx_manual_cube_grad_fixup = true;
        options.ios_use_simdgroup_functions = true;

        compiler.set_msl_options(options);

        result.source = compiler.compile();

        if (!InjectManualVertexFetch(result.source, state)) {
            static std::atomic<u32> reported{0};
            if (reported.fetch_add(1, std::memory_order_relaxed) < 8) {
                LOG_WARNING(Shader_MSL,
                            "Vertex capture declined: the guest's attribute layout cannot be "
                            "fetched without stage-in");
            }
            // Empty source is how every caller spells "no capture kernel".
            return {};
        }
        if (!ProvideCaptureDrawParameters(result.source)) {
            return {};
        }

        // Vertex, not GLCompute, even though the MSL we get is a kernel:
        // capture_output_to_buffer only changes what SPIRV-Cross emits.
        result.entry_point = compiler.get_cleansed_entry_point_name(
            compiler.get_entry_points_and_stages().front().name, spv::ExecutionModelVertex);

        const spirv_cross::ShaderResources resources = compiler.get_shader_resources();
        u32 highest = 0;
        bool any = false;
        const auto note = [&](const spirv_cross::Resource& resource, bool has_sampler) {
            any = true;
            highest =
                (std::max)(highest, compiler.get_automatic_msl_resource_binding(resource.id));
            if (has_sampler) {
                highest = (std::max)(
                    highest, compiler.get_automatic_msl_resource_binding_secondary(resource.id));
            }
        };
        for (const auto& resource : resources.uniform_buffers) {
            note(resource, false);
        }
        for (const auto& resource : resources.storage_buffers) {
            note(resource, false);
        }
        for (const auto& resource : resources.sampled_images) {
            note(resource, true);
        }
        for (const auto& resource : resources.separate_images) {
            note(resource, false);
        }
        for (const auto& resource : resources.storage_images) {
            note(resource, false);
        }
        result.argument_buffer_slots = any ? highest + 1 : 0;
    } catch (const spirv_cross::CompilerError& error) {
        throw Exception(std::string{"SPIRV-Cross failed to build a capture kernel: "} +
                        error.what());
    } catch (const std::exception& error) {
        throw Exception(std::string{"Capture kernel translation failed: "} + error.what());
    }

    if (result.source.empty()) {
        throw Exception("SPIRV-Cross produced no capture kernel");
    }
    (void)info;
    return result;
}

namespace {
// Compiled MSL keyed by hash + length of its source (not the text itself, keeping
// every shader's source alive costs tens of MB). One guest shader serves many
// pipelines and each was calling newLibrary() on identical text, ~70s of a 150s run.
// The pipeline archive doesn't help, it's only consulted at newRenderPipelineState
// and the library has to exist by then.
struct LibraryKey {
    u64 hash;
    u64 length;

    bool operator==(const LibraryKey&) const noexcept = default;
};

struct LibraryKeyHash {
    size_t operator()(const LibraryKey& key) const noexcept {
        return static_cast<size_t>(key.hash ^ (key.length * 0x9E3779B97F4A7C15ull));
    }
};

std::mutex library_cache_mutex;
std::unordered_map<LibraryKey, Owned<MTL::Library>, LibraryKeyHash> library_cache;

} // Anonymous namespace

Owned<MTL::Library> BuildLibrary(const Device& device, const MslShader& shader) {
    const LibraryKey key{Common::CityHash64(shader.source.data(), shader.source.size()),
                         shader.source.size()};
    {
        std::scoped_lock lock{library_cache_mutex};
        if (const auto it = library_cache.find(key); it != library_cache.end()) {
            return it->second;
        }
    }

    ScopedAutoreleasePool pool;

    MTL::CompileOptions* options = MTL::CompileOptions::alloc()->init();
    options->setLanguageVersion(ToLanguageVersion(device.GetMslVersion()));

    // preserveInvariance keeps a depth pre-pass and the shading pass EQUAL-testing
    // against it computing bit-identical positions, without MathModeSafe's ban on FMA
    // contraction everywhere. Both math spellings are set, setFastMathEnabled is
    // deprecated in favour of mathMode and neither alone is safe across SDK versions.
    options->setPreserveInvariance(true);
    options->setFastMathEnabled(true);
    options->setMathMode(MTL::MathModeRelaxed);
    LOG_DEBUG(Render_Metal, "Shader math mode {}", static_cast<int>(options->mathMode()));

    NS::Error* error = nullptr;
    Owned<MTL::Library> library =
        Adopt(device.GetDevice()->newLibrary(ToNSString(shader.source), options, &error));

    options->release();

    if (!library) {
        LOG_ERROR(Shader_MSL, "Failed to compile MSL: {}", DescribeError(error));
        return {};
    }

    // Another worker may have compiled the same source meanwhile.
    std::scoped_lock lock{library_cache_mutex};
    return library_cache.try_emplace(key, std::move(library)).first->second;
}

namespace {

struct LargeStackTask {
    std::function<MslShader()>* translate{};
    MslShader result;
    std::exception_ptr exception;
};

void* LargeStackEntry(void* raw) {
    auto* task = static_cast<LargeStackTask*>(raw);
    try {
        task->result = (*task->translate)();
    } catch (...) {
        // An exception must not unwind out of a pthread entry point.
        task->exception = std::current_exception();
    }
    return nullptr;
}

} // Anonymous namespace

MslShader RunOnLargeStack(std::function<MslShader()> translate) {
    pthread_attr_t attributes;
    if (pthread_attr_init(&attributes) != 0) {
        LOG_WARNING(Shader_MSL, "Could not init thread attributes; translating inline");
        return translate();
    }
    pthread_attr_setstacksize(&attributes, SPIRV_CROSS_STACK_SIZE);

    LargeStackTask task{};
    task.translate = &translate;

    pthread_t thread{};
    const int created = pthread_create(&thread, &attributes, LargeStackEntry, &task);
    pthread_attr_destroy(&attributes);

    if (created != 0) {
        // Risks the overflow this exists to avoid, but failing the shader is worse.
        LOG_WARNING(Shader_MSL, "Could not spawn a large-stack thread; translating inline");
        return translate();
    }

    pthread_join(thread, nullptr);

    if (task.exception) {
        std::rethrow_exception(task.exception);
    }
    return std::move(task.result);
}

MslShader TranslateGeometryToComputeKernel(const Device& device, std::span<const u32> spirv,
                                           const Shader::Info& info,
                                           [[maybe_unused]] const BindingMap& bindings) {
    if (spirv.empty()) {
        throw Exception("Cannot translate an empty SPIR-V module");
    }

    MslShader result{};
    // The recompiler has already emitted it with ExecutionModel GLCompute.
    result.stage = Shader::Stage::Compute;
    ReportUnsupportedFeatures(Shader::Stage::Compute, info);

    try {
        spirv_cross::CompilerMSL compiler{spirv.data(), spirv.size()};
        spirv_cross::CompilerMSL::Options options = compiler.get_msl_options();

        options.platform = device.IsMobile() ? spirv_cross::CompilerMSL::Options::iOS
                                             : spirv_cross::CompilerMSL::Options::macOS;
        options.msl_version = device.GetMslVersion();

        options.argument_buffers = true;
        options.argument_buffers_tier =
            device.SupportsArgumentBuffersTier2()
                ? spirv_cross::CompilerMSL::Options::ArgumentBuffersTier::Tier2
                : spirv_cross::CompilerMSL::Options::ArgumentBuffersTier::Tier1;

        options.force_active_argument_buffer_resources = true;
        options.enable_decoration_binding = !options.argument_buffers;
        options.force_native_arrays = true;
        options.texture_buffer_native = true;
        options.agx_manual_cube_grad_fixup = true;
        options.ios_use_simdgroup_functions = true;
        options.invariant_float_math = false;

        // Discrete, so the kernel's two synthetic buffers come out as plain bindings.
        compiler.add_discrete_descriptor_set(
            Shader::Backend::SPIRV::GEOMETRY_AS_COMPUTE_DESCRIPTOR_SET);

        const auto pin = [&](u32 binding, u32 msl_buffer) {
            spirv_cross::MSLResourceBinding resource{};
            resource.stage = spv::ExecutionModelGLCompute;
            resource.desc_set = Shader::Backend::SPIRV::GEOMETRY_AS_COMPUTE_DESCRIPTOR_SET;
            resource.binding = binding;
            resource.msl_buffer = msl_buffer;
            compiler.add_msl_resource_binding(resource);
        };
        pin(0, GEOMETRY_INPUT_BUFFER_INDEX);
        pin(1, GEOMETRY_OUTPUT_BUFFER_INDEX);

        // Stripped, same as in TranslateSpirvToMsl, see the note there.
        for (u32 id = 0; id < compiler.get_current_id_bound(); ++id) {
            compiler.unset_decoration(spirv_cross::ID(id), spv::DecorationNoContraction);
        }

        compiler.set_msl_options(options);

        result.source = compiler.compile();
        result.entry_point = compiler.get_cleansed_entry_point_name(
            compiler.get_entry_points_and_stages().front().name, spv::ExecutionModelGLCompute);

        const spirv_cross::ShaderResources resources = compiler.get_shader_resources();
        u32 highest = 0;
        bool any = false;
        const auto note = [&](const spirv_cross::Resource& resource, bool has_sampler) {
            any = true;
            highest =
                (std::max)(highest, compiler.get_automatic_msl_resource_binding(resource.id));
            if (has_sampler) {
                highest = (std::max)(
                    highest, compiler.get_automatic_msl_resource_binding_secondary(resource.id));
            }
        };
        for (const auto& resource : resources.uniform_buffers) {
            note(resource, false);
        }
        for (const auto& resource : resources.storage_buffers) {
            // The discrete set is bound directly, not through an argument buffer.
            if (compiler.get_decoration(resource.id, spv::DecorationDescriptorSet) ==
                Shader::Backend::SPIRV::GEOMETRY_AS_COMPUTE_DESCRIPTOR_SET) {
                continue;
            }
            note(resource, false);
        }
        for (const auto& resource : resources.sampled_images) {
            note(resource, true);
        }
        for (const auto& resource : resources.separate_images) {
            note(resource, false);
        }
        for (const auto& resource : resources.storage_images) {
            note(resource, false);
        }
        result.argument_buffer_slots = any ? highest + 1 : 0;
    } catch (const spirv_cross::CompilerError& e) {
        throw Exception(std::format("SPIRV-Cross failed to build a geometry kernel: {}", e.what()));
    }
    return result;
}

MslShader BuildGeometryPassthroughVertexShader(u32 output_stride,
                                               const std::bitset<32>& locations) {
    std::string source;
    source += "#include <metal_stdlib>\n";
    source += "using namespace metal;\n\n";
    source += "struct gs_passthrough_out {\n";
    // `invariant` for the same reason as every other position output.
    source += "    float4 position [[position, invariant]];\n";
    for (u32 location = 0; location < 32; ++location) {
        if (locations[location]) {
            source += std::format("    float4 attr{} [[user(locn{})]];\n", location, location);
        }
    }
    source += "};\n\n";
    source += std::format(
        "vertex gs_passthrough_out gs_passthrough(uint vid [[vertex_id]],\n"
        "                                         device const float4* verts [[buffer({})]]) {{\n",
        GEOMETRY_PASSTHROUGH_BUFFER_INDEX);
    source += "    gs_passthrough_out out = {};\n";
    source += std::format("    const uint base = vid * {}u;\n", output_stride);
    source += "    out.position = verts[base];\n";
    for (u32 location = 0; location < 32; ++location) {
        if (locations[location]) {
            source += std::format("    out.attr{} = verts[base + {}u];\n", location, 1 + location);
        }
    }
    source += "    return out;\n";
    source += "}\n";

    MslShader result{};
    result.source = std::move(source);
    result.entry_point = "gs_passthrough";
    // VertexB, not Compute: this really is the render pipeline's vertex stage.
    result.stage = Shader::Stage::VertexB;
    // Binds nothing through an argument buffer, so the draw path must not write one.
    result.argument_buffer_slots = 0;
    return result;
}

Owned<MTL::Function> GetFunction(MTL::Library* library, const std::string& entry_point) {
    if (library == nullptr) {
        return {};
    }
    ScopedAutoreleasePool pool;

    Owned<MTL::Function> function = Adopt(library->newFunction(ToNSString(entry_point)));
    if (!function) {
        LOG_ERROR(Shader_MSL, "MSL library has no entry point named '{}'", entry_point);
        return function;
    }

    // A function declaring *any* [[function_constant]] can't build a pipeline state
    // until it's specialized, and Metal aborts the process instead of erroring.
    // SPIRV-Cross emits one behind our back (spvLinearTextureAlignmentOverride, for
    // emulated 2D image atomics). Re-creating with an empty value set is enough.
    NS::Dictionary* const declared = function->functionConstantsDictionary();
    if (declared == nullptr || declared->count() == 0) {
        return function;
    }

    Owned<MTL::FunctionConstantValues> values =
        Adopt(MTL::FunctionConstantValues::alloc()->init());

    // Large enough for any scalar or vector a function constant can hold.
    const std::array<u8, 16> zero{};

    NS::Enumerator<NS::String>* keys = declared->keyEnumerator<NS::String>();
    for (NS::String* key = keys->nextObject(); key != nullptr; key = keys->nextObject()) {
        auto* constant = declared->object<MTL::FunctionConstant>(key);
        if (constant == nullptr) {
            continue;
        }
        LOG_DEBUG(Shader_MSL, "'{}' declares function constant '{}' (index {}, required {})",
                  entry_point, FromNSString(constant->name()), constant->index(),
                  constant->required());
        if (!constant->required()) {
            continue;
        }
        values->setConstantValue(zero.data(), constant->type(), constant->index());
    }

    NS::Error* error = nullptr;
    Owned<MTL::Function> specialized =
        Adopt(library->newFunction(ToNSString(entry_point), values.get(), &error));
    if (!specialized) {
        // Returning the unspecialized function would abort the process.
        LOG_ERROR(Shader_MSL, "Could not specialize '{}': {}", entry_point, DescribeError(error));
        return {};
    }
    return specialized;
}

void SetColorAttachment(MTL4::RenderPipelineDescriptor* descriptor, u32 index,
                        MTL::PixelFormat format) {
    MTL4::RenderPipelineColorAttachmentDescriptor* target =
        MTL4::RenderPipelineColorAttachmentDescriptor::alloc()->init();
    target->setPixelFormat(format);
    descriptor->colorAttachments()->setObject(target, index);
    target->release();
}

MTL4::FunctionDescriptor* MakeFunctionDescriptor(MTL::Library* library,
                                                 const std::string& entry_point) {
    MTL4::LibraryFunctionDescriptor* base = MTL4::LibraryFunctionDescriptor::alloc()->init();
    base->setLibrary(library);
    base->setName(ToNSString(entry_point));
    base->autorelease();

    if (library == nullptr) {
        return base;
    }

    // Same hazard GetFunction guards against, different route: Metal 4 never takes an
    // MTL::Function. Metal doesn't ask whether required constants have values, it asks
    // whether the function was created with a constant-values object at all, and
    // spvLinearTextureAlignmentOverride is optional, so specialize whenever ANY
    // constant is declared.
    Owned<MTL::Function> probe = Adopt(library->newFunction(ToNSString(entry_point)));
    if (!probe) {
        return base;
    }
    NS::Dictionary* const declared = probe->functionConstantsDictionary();
    if (declared == nullptr || declared->count() == 0) {
        return base;
    }

    Owned<MTL::FunctionConstantValues> values =
        Adopt(MTL::FunctionConstantValues::alloc()->init());
    const std::array<u8, 16> zero{};

    NS::Enumerator<NS::String>* keys = declared->keyEnumerator<NS::String>();
    for (NS::String* key = keys->nextObject(); key != nullptr; key = keys->nextObject()) {
        auto* constant = declared->object<MTL::FunctionConstant>(key);
        if (constant == nullptr || !constant->required()) {
            continue;
        }
        values->setConstantValue(zero.data(), constant->type(), constant->index());
    }

    MTL4::SpecializedFunctionDescriptor* specialized =
        MTL4::SpecializedFunctionDescriptor::alloc()->init();
    specialized->setFunctionDescriptor(base);
    specialized->setConstantValues(values.get());
    return specialized->autorelease();
}

Owned<MTL::ComputePipelineState> BuildComputePipeline(const Device& device, MTL::Library* library,
                                                      const std::string& entry_point,
                                                      const char* label) {
    if (library == nullptr) {
        return {};
    }
    ScopedAutoreleasePool pool;

    MTL4::ComputePipelineDescriptor* descriptor =
        MTL4::ComputePipelineDescriptor::alloc()->init();
    descriptor->setComputeFunctionDescriptor(MakeFunctionDescriptor(library, entry_point));
    if (label != nullptr) {
        descriptor->setLabel(ToNSString(label));
    }

    NS::Error* error = nullptr;
    Owned<MTL::ComputePipelineState> pipeline = Adopt(device.GetMTL4Compiler()->
        newComputePipelineState(descriptor, device.GetMTL4LookupOptions(), &error));
    descriptor->release();

    if (!pipeline) {
        LOG_ERROR(Render_Metal, "Failed to build the '{}' compute pipeline: {}",
                  label != nullptr ? label : entry_point.c_str(), DescribeError(error));
    }
    return pipeline;
}

} // namespace Metal
