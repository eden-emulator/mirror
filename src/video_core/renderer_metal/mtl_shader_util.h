// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <bitset>
#include <functional>
#include <span>
#include <string>
#include <vector>

#include "common/common_types.h"
#include "shader_recompiler/backend/bindings.h"
#include "shader_recompiler/shader_info.h"
#include "shader_recompiler/stage.h"
#include "video_core/metal_common/metal_wrapper.h"
#include "video_core/renderer_metal/fixed_pipeline_state.h"

namespace Metal {

class Device;

/// Buffer indices used OUTSIDE the argument buffer: the argument buffer itself, the
/// vertex streams, the index buffer. Not the argument buffer's interior, SPIRV-Cross
/// assigns those slots. See mtl_argument_buffer.h for that contract.
struct BindingMap {
    /// Index 0 is the argument buffer. Guest uniform and storage buffers live
    /// inside it instead of taking slots of their own.
    static constexpr u32 ARGUMENT_BUFFER_INDEX = 0;
    /// Metal gives us 31 buffer argument slots (0..30). Hard limit, setBufferIndex
    /// past it is an assertion failure, not a warning.
    static constexpr u32 MAX_BUFFER_SLOTS = 31;
    static constexpr u32 VERTEX_BUFFER_BASE = 1;
    /// Highest slot a vertex stream may use.
    static constexpr u32 MAX_VERTEX_BUFFER_SLOT = MAX_BUFFER_SLOTS - 1;

    /// Unused, nothing reads these.
    u32 uniform_buffer_base{1};
    u32 storage_buffer_base{8};
    u32 texture_base{0};
    u32 image_base{64};
    u32 sampler_base{0};
};

struct MslShader {
    std::string source;
    /// Entry point name SPIRV-Cross emitted. Not always "main", MSL reserves some
    /// identifiers and SPIRV-Cross renames on collision.
    std::string entry_point;
    Shader::Stage stage{};
    /// Number of `[[id(N)]]` slots SPIRV-Cross emitted in this stage's argument
    /// buffer struct. We write those in append order and Metal checks nothing, so
    /// comparing counts is the only guard we have. Catches a missing or extra
    /// resource, not a permutation.
    u32 argument_buffer_slots{};
};

/// Translates a SPIR-V module to MSL. Throws Exception when SPIRV-Cross can't
/// translate it at all.
[[nodiscard]] MslShader TranslateSpirvToMsl(const Device& device,
                                            std::span<const u32> spirv,
                                            Shader::Stage stage,
                                            const Shader::Info& info,
                                            const BindingMap& bindings,
                                            bool rasterizes_points);

/// Translates a vertex program into a compute kernel writing its outputs to a
/// buffer, the only way Metal does transform feedback. Result is a Compute shader
/// even though the guest program is a vertex one. The captured layout is
/// SPIRV-Cross's own stage-out struct, not the guest's transform feedback layout, so
/// a gather step still has to move each varying to the buffer, offset and stride the
/// guest asked for.
[[nodiscard]] MslShader TranslateVertexToCaptureKernel(const Device& device,
                                                       std::span<const u32> spirv,
                                                       const Shader::Info& info,
                                                       const BindingMap& bindings,
                                                       const FixedPipelineState& state);

/// Buffer slot SPIRV-Cross writes a capture kernel's outputs to. Matches
/// CompilerMSL::Options::shader_output_buffer_index, which we set explicitly instead
/// of trusting the default to stay this value.
constexpr u32 CAPTURE_OUTPUT_BUFFER_INDEX = 28;

/// Buffer slot the capture dispatch's flattened index buffer takes. The injected
/// manual fetch reads it directly as `spvOrder`.
constexpr u32 CAPTURE_INDEX_BUFFER_INDEX = 29;

/// Buffer slot holding each vertex stream's size in bytes. Our hand-written
/// attribute fetch (Metal 4 has no stage-in for kernels) indexes raw memory by a
/// guest-supplied index and nothing else bounds it. An unchecked read past a stream
/// can fault the GPU and kill the queue.
constexpr u32 CAPTURE_LIMITS_BUFFER_INDEX = 30;

/// Index into the capture limits buffer of the draw's base vertex, followed by its base
/// instance. Past the per-stream limits.
constexpr u32 CAPTURE_DRAW_BASE_SLOT = BindingMap::MAX_BUFFER_SLOTS;

/// Whether slot is one of the three the capture dispatch keeps for itself. All three
/// sit inside the range vertex streams bind from, so we name them one by one.
/// Treating everything above the output slot as reserved would drop the
/// constant-attribute binding, which uses the last legal slot on purpose.
[[nodiscard]] constexpr bool IsCaptureReservedBufferSlot(u32 slot) noexcept {
    return slot == CAPTURE_OUTPUT_BUFFER_INDEX || slot == CAPTURE_INDEX_BUFFER_INDEX ||
           slot == CAPTURE_LIMITS_BUFFER_INDEX;
}

/// Where the emitted-vertex buffer binds for the generated passthrough vertex
/// shader. Slot 0 is the argument buffer everywhere else, but a passthrough isn't a
/// guest shader and binds nothing through one.
constexpr u32 GEOMETRY_PASSTHROUGH_BUFFER_INDEX = 0;

/// Vertex shader a geometry-emulated draw rasterises with. The capture and geometry
/// kernels already produced the expanded triangles, this just hands them to the
/// *unmodified* guest fragment shader, which links because SPIRV-Cross names fragment
/// inputs by location (`[[user(locnN)]]`). output_stride is float4s per emitted
/// vertex, position is element 0 and generic location L is element 1 + L. locations
/// is what the fragment program loads, not all 32, since 32 float4s blow Metal's
/// per-stage varying budget on their own.
[[nodiscard]] MslShader BuildGeometryPassthroughVertexShader(u32 output_stride,
                                                             const std::bitset<32>& locations);

/// Slots for the two buffers a lowered geometry kernel uses. The recompiler puts them
/// in their own descriptor set, declared discrete so SPIRV-Cross gives them plain
/// buffer bindings instead of folding them into the guest's argument buffer. These
/// pin where those bindings land.
constexpr u32 GEOMETRY_INPUT_BUFFER_INDEX = 26;
constexpr u32 GEOMETRY_OUTPUT_BUFFER_INDEX = 27;

/// Translates a geometry program into an MSL kernel. Metal has no geometry stage and
/// SPIRV-Cross can't translate one, so the SPIR-V emitter already lowered it to a
/// GLCompute module and by here it's an ordinary compute shader. One thread per input
/// primitive, reading captured vertices from GEOMETRY_INPUT_BUFFER_INDEX and writing
/// expanded triangles to GEOMETRY_OUTPUT_BUFFER_INDEX.
[[nodiscard]] MslShader TranslateGeometryToComputeKernel(const Device& device,
                                                         std::span<const u32> spirv,
                                                         const Shader::Info& info,
                                                         const BindingMap& bindings);

/// Compiles MSL source into a Metal library. Returns null and logs on failure.
[[nodiscard]] Owned<MTL::Library> BuildLibrary(const Device& device, const MslShader& shader);

/// Looks up the entry point in the library and returns the function object.
[[nodiscard]] Owned<MTL::Function> GetFunction(MTL::Library* library,
                                                const std::string& entry_point);

/// Declares one colour attachment on a Metal 4 render pipeline descriptor. MTL4
/// attachments are set as objects, not mutated in place, so this wraps the
/// alloc/set/release for callers that only have a format.
void SetColorAttachment(MTL4::RenderPipelineDescriptor* descriptor, u32 index,
                        MTL::PixelFormat format);

/// Names entry_point in library for a Metal 4 pipeline descriptor. Returns an
/// autoreleased descriptor, wrapped in a specialization whenever the function
/// declares ANY function constants. Metal 4 never sees the MTL::Function GetFunction
/// specializes, so we have to do it here too.
[[nodiscard]] MTL4::FunctionDescriptor* MakeFunctionDescriptor(MTL::Library* library,
                                                               const std::string& entry_point);

/// Builds a compute pipeline through MTL4::Compiler for helper kernels with no other
/// pipeline state to configure. Metal 4 has no newComputePipelineState(function)
/// overload so even those need a descriptor. Passes the device's lookup options, a
/// kernel compiled on a previous run is an archive lookup instead of a compile.
/// Returns null and logs on failure.
[[nodiscard]] Owned<MTL::ComputePipelineState> BuildComputePipeline(const Device& device,
                                                                    MTL::Library* library,
                                                                    const std::string& entry_point,
                                                                    const char* label);

/// Stack for SPIR-V translation. SPIRV-Cross recurses deep over the control flow
/// graph and overflows the 512 KiB an Apple secondary thread gets by default, and
/// translation always runs on a worker.
constexpr size_t SPIRV_CROSS_STACK_SIZE = 8 * 1024 * 1024;

/// Runs translate on a thread with a SPIRV_CROSS_STACK_SIZE stack.
/// Common::StatefulThreadWorker can't set a stack size and std::thread doesn't expose
/// one, so we use pthread directly. Exceptions are propagated.
[[nodiscard]] MslShader RunOnLargeStack(std::function<MslShader()> translate);

} // namespace Metal
