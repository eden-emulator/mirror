// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>

#include "video_core/renderer_vulkan/maxwell_to_vk.h"
#include "video_core/renderer_vulkan/vk_render_pass_cache.h"
#include "video_core/surface.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {
namespace {
using VideoCore::Surface::PixelFormat;
using VideoCore::Surface::SurfaceType;

struct AttachmentAspects {
    bool depth;
    bool stencil;
};

constexpr SurfaceType GetSurfaceType(PixelFormat format) {
    switch (format) {
    case PixelFormat::D16_UNORM:
    case PixelFormat::D32_FLOAT:
    case PixelFormat::X8_D24_UNORM:
        return SurfaceType::Depth;
    case PixelFormat::S8_UINT:
        return SurfaceType::Stencil;
    case PixelFormat::D24_UNORM_S8_UINT:
    case PixelFormat::S8_UINT_D24_UNORM:
    case PixelFormat::D32_FLOAT_S8_UINT:
        return SurfaceType::DepthStencil;
    default:
        return SurfaceType::ColorTexture;
    }
}

constexpr AttachmentAspects GetAttachmentAspects(PixelFormat format) {
    const SurfaceType surface_type = GetSurfaceType(format);
    return AttachmentAspects{
        .depth = surface_type == SurfaceType::Depth || surface_type == SurfaceType::DepthStencil,
        .stencil =
            surface_type == SurfaceType::Stencil || surface_type == SurfaceType::DepthStencil,
    };
}

VkFormat AttachmentFormat(const Device& device, PixelFormat format) {
    return MaxwellToVK::SurfaceFormat(device, FormatType::Optimal, true, format).format;
}

VkRenderingAttachmentInfo MakeAttachment(VkImageView view) {
    return {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = nullptr,
        .imageView = view,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        .resolveMode = VK_RESOLVE_MODE_NONE,
        .resolveImageView = VK_NULL_HANDLE,
        .resolveImageLayout = VK_IMAGE_LAYOUT_GENERAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {},
    };
}
} // Anonymous namespace

ResolveModes PickResolveModes(const Device& device, PixelFormat format) {
    constexpr VkResolveModeFlagBits mode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;

    const AttachmentAspects aspects = GetAttachmentAspects(format);
    const bool depth_mode_supported = (device.GetDepthResolveModes() & mode) != 0;
    const bool stencil_mode_supported = (device.GetStencilResolveModes() & mode) != 0;

    ResolveModes modes{
        .depth = VK_RESOLVE_MODE_NONE,
        .stencil = VK_RESOLVE_MODE_NONE,
    };
    if (aspects.depth && depth_mode_supported) {
        modes.depth = mode;
    }
    if (aspects.stencil && stencil_mode_supported) {
        modes.stencil = mode;
    }
    if (modes.depth == modes.stencil || device.SupportsIndependentResolveNone()) {
        return modes;
    }
    if (modes.depth != VK_RESOLVE_MODE_NONE && stencil_mode_supported) {
        modes.stencil = mode;
    } else if (modes.stencil != VK_RESOLVE_MODE_NONE && depth_mode_supported) {
        modes.depth = mode;
    }
    return modes;
}

bool SupportsDepthStencilResolve(const Device& device, PixelFormat depth_format) {
    if (depth_format == PixelFormat::Invalid || !device.IsKhrDepthStencilResolveSupported()) {
        return false;
    }
    const AttachmentAspects aspects = GetAttachmentAspects(depth_format);
    if (!aspects.depth && !aspects.stencil) {
        return false;
    }
    const ResolveModes modes = PickResolveModes(device, depth_format);
    if ((aspects.depth && modes.depth == VK_RESOLVE_MODE_NONE) ||
        (aspects.stencil && modes.stencil == VK_RESOLVE_MODE_NONE)) {
        return false;
    }
    return modes.depth == modes.stencil || device.SupportsIndependentResolveNone();
}

RenderingFormats MakeRenderingFormats(const Device& device, std::span<const PixelFormat> colors,
                                      PixelFormat depth) {
    RenderingFormats formats{};
    for (size_t index = 0; index < colors.size(); ++index) {
        if (colors[index] == PixelFormat::Invalid) {
            continue;
        }
        formats.colors[index] = AttachmentFormat(device, colors[index]);
        formats.num_colors = static_cast<u32>(index + 1);
    }
    if (depth == PixelFormat::Invalid) {
        return formats;
    }
    const VkFormat depth_format = AttachmentFormat(device, depth);
    const AttachmentAspects aspects = GetAttachmentAspects(depth);
    if (aspects.depth) {
        formats.depth = depth_format;
    }
    if (aspects.stencil) {
        formats.stencil = depth_format;
    }
    return formats;
}

RenderingAttachments MakeRenderingAttachments(const RenderingFormats& formats,
                                              std::span<const VkImageView> colors,
                                              VkImageView depth, const VkRect2D& render_area,
                                              u32 layers) {
    RenderingAttachments attachments{
        .render_area = render_area,
        .num_colors = formats.num_colors,
        .layers = layers,
    };
    attachments.colors.fill(MakeAttachment(VK_NULL_HANDLE));
    std::ranges::transform(colors, attachments.colors.begin(), MakeAttachment);
    attachments.depth = MakeAttachment(VK_NULL_HANDLE);
    attachments.stencil = attachments.depth;
    if (formats.depth != VK_FORMAT_UNDEFINED) {
        attachments.depth.imageView = depth;
    }
    if (formats.stencil != VK_FORMAT_UNDEFINED) {
        attachments.stencil.imageView = depth;
    }
    return attachments;
}

void BeginRendering(vk::CommandBuffer cmdbuf, const RenderingAttachments& attachments) {
    cmdbuf.BeginRendering(VkRenderingInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .pNext = nullptr,
        .flags = 0,
        .renderArea = attachments.render_area,
        .layerCount = attachments.layers,
        .viewMask = 0,
        .colorAttachmentCount = attachments.num_colors,
        .pColorAttachments = attachments.colors.data(),
        .pDepthAttachment = &attachments.depth,
        .pStencilAttachment = &attachments.stencil,
    });
}

} // namespace Vulkan
