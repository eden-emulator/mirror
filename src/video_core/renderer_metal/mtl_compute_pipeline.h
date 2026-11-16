// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>

#include "common/common_funcs.h"
#include "common/common_types.h"
#include "shader_recompiler/shader_info.h"
#include "video_core/buffer_cache/buffer_cache_base.h"
#include "video_core/metal_common/metal_wrapper.h"
#include "video_core/renderer_metal/mtl_shader_util.h"

namespace Metal {

class Device;
class Scheduler;

/// A compiled compute kernel. Metal has no pipeline/descriptor set layout
/// objects, so the pipeline state plus the shader info is the whole binding
/// story.
class ComputePipeline : Common::NonCopyable, Common::NonMoveable {
public:
    /// @param workgroup_size Declared threadgroup dimensions, from
    ///                       Shader::IR::Program (Shader::Info lacks it).
    explicit ComputePipeline(const Device& device, Scheduler& scheduler,
                             const Shader::Info& info,
                             const std::array<u32, 3>& workgroup_size, const MslShader& shader);
    ~ComputePipeline();

    [[nodiscard]] bool IsValid() const noexcept {
        return static_cast<bool>(pipeline_state);
    }

    [[nodiscard]] MTL::ComputePipelineState* Handle() const noexcept {
        return pipeline_state.get();
    }

    [[nodiscard]] const Shader::Info& GetInfo() const noexcept {
        return info;
    }

    /// Per-constant-buffer sizes, shaped for SetComputeUniformBufferState.
    [[nodiscard]] const VideoCommon::ComputeUniformBufferSizes& UniformBufferSizes()
        const noexcept {
        return uniform_buffer_sizes;
    }

    [[nodiscard]] MTL::Size GetThreadgroupSize() const noexcept {
        return threadgroup_size;
    }

    /// Largest total threadgroup this kernel can run at. Depends on register
    /// pressure, so it may be below the declared size.
    [[nodiscard]] u32 GetMaxTotalThreadsPerThreadgroup() const noexcept {
        return max_total_threads;
    }

private:
    const Device& device;
    Scheduler& scheduler;
    Shader::Info info;

    Owned<MTL::Library> library;
    Owned<MTL::Function> function;
    Owned<MTL::ComputePipelineState> pipeline_state;

    VideoCommon::ComputeUniformBufferSizes uniform_buffer_sizes{};

    MTL::Size threadgroup_size{1, 1, 1};
    u32 max_total_threads{1};
};

} // namespace Metal
