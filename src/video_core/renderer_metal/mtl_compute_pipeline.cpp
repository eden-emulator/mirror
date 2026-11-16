// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include "common/logging.h"
#include "video_core/metal_common/metal_device.h"
#include "video_core/renderer_metal/mtl_compute_pipeline.h"
#include "video_core/renderer_metal/mtl_scheduler.h"

namespace Metal {

ComputePipeline::ComputePipeline(const Device& device_, Scheduler& scheduler_,
                                 const Shader::Info& info_,
                                 const std::array<u32, 3>& workgroup_size,
                                 const MslShader& shader)
    : device{device_}, scheduler{scheduler_}, info{info_} {
    std::ranges::copy(info.constant_buffer_used_sizes, uniform_buffer_sizes.begin());

    ScopedAutoreleasePool pool;

    library = BuildLibrary(device, shader);
    if (!library) {
        return;
    }
    library->setLabel(ToNSString("eden compute"));

    function = GetFunction(library.get(), shader.entry_point);
    if (!function) {
        return;
    }

    NS::Error* error = nullptr;
    {
        // Metal 4 takes a library plus entry point, not an MTL::Function.
        MTL4::ComputePipelineDescriptor* descriptor =
            MTL4::ComputePipelineDescriptor::alloc()->init();
        descriptor->setComputeFunctionDescriptor(
            MakeFunctionDescriptor(library.get(), shader.entry_point));
        descriptor->setLabel(ToNSString("eden compute"));
        pipeline_state = Adopt(device.GetMTL4Compiler()->newComputePipelineState(
            descriptor, device.GetMTL4LookupOptions(), &error));
        descriptor->release();
    }
    if (!pipeline_state) {
        LOG_ERROR(Render_Metal, "Failed to create a compute pipeline state: {}",
                  DescribeError(error));
        return;
    }

    threadgroup_size =
        MTL::Size::Make(workgroup_size[0], workgroup_size[1], workgroup_size[2]);

    // Dispatching above this is a hard error, and it can be below the declared
    // size for register-heavy kernels, so ask the pipeline.
    max_total_threads = static_cast<u32>(pipeline_state->maxTotalThreadsPerThreadgroup());

    const u32 declared_total = workgroup_size[0] * workgroup_size[1] * workgroup_size[2];
    if (declared_total > max_total_threads) {
        LOG_WARNING(Render_Metal,
                    "Compute kernel declares {} threads per group but the device allows {}; "
                    "dispatches will be clamped and results may be wrong",
                    declared_total, max_total_threads);
    }
}

ComputePipeline::~ComputePipeline() = default;

} // namespace Metal
