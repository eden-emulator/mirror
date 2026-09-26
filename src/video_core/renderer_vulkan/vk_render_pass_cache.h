// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <span>

#include "video_core/surface.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

class Device;

struct RenderingFormats {
    constexpr auto operator<=>(const RenderingFormats&) const noexcept = default;

    [[nodiscard]] VkPipelineRenderingCreateInfo CreateInfo() const noexcept {
        return {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .pNext = nullptr,
            .viewMask = 0,
            .colorAttachmentCount = num_colors,
            .pColorAttachmentFormats = colors.data(),
            .depthAttachmentFormat = depth,
            .stencilAttachmentFormat = stencil,
        };
    }

    std::array<VkFormat, 8> colors{};
    u32 num_colors{};
    VkFormat depth{};
    VkFormat stencil{};
};

struct RenderingAttachments {
    std::array<VkRenderingAttachmentInfo, 8> colors;
    VkRenderingAttachmentInfo depth;
    VkRenderingAttachmentInfo stencil;
    VkRect2D render_area;
    u32 num_colors;
    u32 layers;
};

struct ResolveModes {
    VkResolveModeFlagBits depth;
    VkResolveModeFlagBits stencil;
};

[[nodiscard]] bool SupportsDepthStencilResolve(const Device& device,
                                               VideoCore::Surface::PixelFormat depth_format);

[[nodiscard]] ResolveModes PickResolveModes(const Device& device,
                                            VideoCore::Surface::PixelFormat format);

[[nodiscard]] RenderingFormats MakeRenderingFormats(
    const Device& device, std::span<const VideoCore::Surface::PixelFormat> colors,
    VideoCore::Surface::PixelFormat depth);

[[nodiscard]] RenderingAttachments MakeRenderingAttachments(const RenderingFormats& formats,
                                                            std::span<const VkImageView> colors,
                                                            VkImageView depth,
                                                            const VkRect2D& render_area,
                                                            u32 layers);

void BeginRendering(vk::CommandBuffer cmdbuf, const RenderingAttachments& attachments);

} // namespace Vulkan
