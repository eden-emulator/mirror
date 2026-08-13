// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "video_core/frame_gen/lossless_dll.h"
#include "video_core/renderer_vulkan/present/lsfg_shaders.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/vulkan_common/vulkan_device.h"

namespace Vulkan {

LsfgShaders::LsfgShaders(const Device& device) {
    if (!device.IsVulkanMemoryModelSupported()) {
        return;
    }

    VideoCore::FrameGen::ShaderModules code;
    if (VideoCore::FrameGen::LoadShaderModules(code) != VideoCore::FrameGen::LosslessStatus::Ok) {
        return;
    }

    for (const auto& [id, words] : code) {
        modules.emplace(id, CreateWrappedShaderModule(device, words));
    }
    valid = true;
}

VkShaderModule LsfgShaders::Get(u32 shader_id) const {
    const auto hit = modules.find(shader_id);
    return hit == modules.end() ? VK_NULL_HANDLE : *hit->second;
}

} // namespace Vulkan
