// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <ankerl/unordered_dense.h>

#include <boost/container/static_vector.hpp>

#include "video_core/renderer_vulkan/maxwell_to_vk.h"
#include "video_core/renderer_vulkan/vk_render_pass_cache.h"
#include "video_core/surface.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {
namespace {
using VideoCore::Surface::PixelFormat;
using VideoCore::Surface::SurfaceType;

        constexpr SurfaceType GetSurfaceType(PixelFormat format) {
            switch (format) {
                // Depth formats
                case PixelFormat::D16_UNORM:
                case PixelFormat::D32_FLOAT:
                case PixelFormat::X8_D24_UNORM:
                    return SurfaceType::Depth;

                    // Stencil formats
                case PixelFormat::S8_UINT:
                    return SurfaceType::Stencil;

                    // Depth+Stencil formats
                case PixelFormat::D24_UNORM_S8_UINT:
                case PixelFormat::S8_UINT_D24_UNORM:
                case PixelFormat::D32_FLOAT_S8_UINT:
                    return SurfaceType::DepthStencil;

                    // Everything else is a color texture
                default:
                    return SurfaceType::ColorTexture;
            }
        }

        VkAttachmentDescription AttachmentDescription(const Device& device, PixelFormat format,
                                                      VkSampleCountFlagBits samples,
                                                      VkAttachmentLoadOp load_op,
                                                      VkAttachmentStoreOp store_op) {
            using MaxwellToVK::SurfaceFormat;

            const SurfaceType surface_type = GetSurfaceType(format);
            const bool has_stencil = surface_type == SurfaceType::DepthStencil ||
                                     surface_type == SurfaceType::Stencil;

            return {
                .flags = {},
                .format = SurfaceFormat(device, FormatType::Optimal, true, format).format,
                .samples = samples,
                .loadOp = load_op,
                .storeOp = store_op,
                .stencilLoadOp = has_stencil ? load_op : VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = has_stencil ? store_op : VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout = VK_IMAGE_LAYOUT_GENERAL,
                .finalLayout = VK_IMAGE_LAYOUT_GENERAL,
            };
        }

        struct ResolveAspects {
            bool depth;
            bool stencil;
        };

        struct ResolveModes {
            VkResolveModeFlagBits depth;
            VkResolveModeFlagBits stencil;
        };

        constexpr ResolveAspects GetResolveAspects(PixelFormat format) {
            const SurfaceType surface_type = GetSurfaceType(format);
            return ResolveAspects{
                .depth = surface_type == SurfaceType::Depth ||
                         surface_type == SurfaceType::DepthStencil,
                .stencil = surface_type == SurfaceType::Stencil ||
                           surface_type == SurfaceType::DepthStencil,
            };
        }

        ResolveModes PickResolveModes(const Device& device, PixelFormat format) {
            constexpr VkResolveModeFlagBits mode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;

            const ResolveAspects aspects = GetResolveAspects(format);
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
    } // Anonymous namespace

bool SupportsDepthStencilResolve(const Device& device, PixelFormat depth_format) {
    if (depth_format == PixelFormat::Invalid || !device.IsKhrDepthStencilResolveSupported()) {
        return false;
    }
    const ResolveAspects aspects = GetResolveAspects(depth_format);
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

RenderPassCache::RenderPassCache(const Device& device_) : device{&device_} {}

VkRenderPass RenderPassCache::Get(const RenderPassKey& key) {
    std::scoped_lock lock{mutex};
    const auto [pair, is_new] = cache.try_emplace(key);
    if (!is_new) {
        return *pair->second;
    }
    static constexpr size_t MAX_ATTACHMENTS =
        2 * std::tuple_size_v<decltype(RenderPassKey::color_formats)> + 2;
    boost::container::static_vector<VkAttachmentDescription, MAX_ATTACHMENTS> descriptions;
    std::array<VkAttachmentReference, 8> references{};
    u32 num_attachments{};
    u32 num_colors{};
    for (size_t index = 0; index < key.color_formats.size(); ++index) {
        const PixelFormat format{key.color_formats[index]};
        const bool is_valid{format != PixelFormat::Invalid};
        references[index] = VkAttachmentReference{
            .attachment = is_valid ? num_colors : VK_ATTACHMENT_UNUSED,
            .layout = VK_IMAGE_LAYOUT_GENERAL,
        };
        if (is_valid) {
            const VkAttachmentLoadOp load_op = (key.color_clear_mask & (1u << index)) != 0
                                                   ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                                   : VK_ATTACHMENT_LOAD_OP_LOAD;
            const VkAttachmentStoreOp store_op = (key.color_discard_mask & (1u << index)) != 0
                                                     ? VK_ATTACHMENT_STORE_OP_DONT_CARE
                                                     : VK_ATTACHMENT_STORE_OP_STORE;
            descriptions.push_back(
                AttachmentDescription(*device, format, key.samples, load_op, store_op));
            num_attachments = static_cast<u32>(index + 1);
            ++num_colors;
        }
    }
    const bool has_depth{key.depth_format != PixelFormat::Invalid};
    VkAttachmentReference depth_reference{};
    if (key.depth_format != PixelFormat::Invalid) {
        depth_reference = VkAttachmentReference{
            .attachment = num_colors,
            .layout = VK_IMAGE_LAYOUT_GENERAL,
        };
        const VkAttachmentLoadOp depth_load_op = key.depth_stencil_clear
                                                     ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                                     : VK_ATTACHMENT_LOAD_OP_LOAD;
        const VkAttachmentStoreOp depth_store_op = key.depth_stencil_discard
                                                       ? VK_ATTACHMENT_STORE_OP_DONT_CARE
                                                       : VK_ATTACHMENT_STORE_OP_STORE;
        descriptions.push_back(AttachmentDescription(*device, key.depth_format, key.samples,
                                                     depth_load_op, depth_store_op));
    }
    std::array<VkAttachmentReference, 8> resolve_references{};
    const bool do_resolve_color =
        key.resolve_color && key.samples != VK_SAMPLE_COUNT_1_BIT && num_colors > 0;
    if (do_resolve_color) {
        for (size_t index = 0; index < key.color_formats.size(); ++index) {
            const PixelFormat format{key.color_formats[index]};
            const bool is_valid{format != PixelFormat::Invalid};
            resolve_references[index] = VkAttachmentReference{
                .attachment = is_valid ? static_cast<u32>(descriptions.size()) : VK_ATTACHMENT_UNUSED,
                .layout = VK_IMAGE_LAYOUT_GENERAL,
            };
            if (is_valid) {
                VkAttachmentDescription resolve_desc =
                    AttachmentDescription(*device, format, VK_SAMPLE_COUNT_1_BIT,
                                          VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                          VK_ATTACHMENT_STORE_OP_STORE);
                resolve_desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                descriptions.push_back(resolve_desc);
            }
        }
    }
    const bool do_resolve_depth_stencil = key.resolve_depth_stencil && has_depth &&
                                          key.samples != VK_SAMPLE_COUNT_1_BIT &&
                                          SupportsDepthStencilResolve(*device, key.depth_format);
    VkAttachmentReference depth_resolve_reference{};
    if (do_resolve_depth_stencil) {
        depth_resolve_reference = VkAttachmentReference{
            .attachment = static_cast<u32>(descriptions.size()),
            .layout = VK_IMAGE_LAYOUT_GENERAL,
        };
        VkAttachmentDescription resolve_desc =
            AttachmentDescription(*device, key.depth_format, VK_SAMPLE_COUNT_1_BIT,
                                  VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_STORE);
        resolve_desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        descriptions.push_back(resolve_desc);
    }
    const VkSubpassDescription subpass{
        .flags = 0,
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .inputAttachmentCount = 0,
        .pInputAttachments = nullptr,
        .colorAttachmentCount = num_attachments,
        .pColorAttachments = references.data(),
        .pResolveAttachments = do_resolve_color ? resolve_references.data() : nullptr,
        .pDepthStencilAttachment = has_depth ? &depth_reference : nullptr,
        .preserveAttachmentCount = 0,
        .pPreserveAttachments = nullptr,
    };
    const VkSubpassDependency dependency{
            .srcSubpass = 0,  // Current subpass
            .dstSubpass = 0,  // Same subpass (self-dependency)
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                             VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT
    };

    if (device->IsKhrCreateRenderPass2Supported()) {
        boost::container::static_vector<VkAttachmentDescription2, MAX_ATTACHMENTS> descriptions2;
        for (const VkAttachmentDescription& description : descriptions) {
            descriptions2.push_back(VkAttachmentDescription2{
                .sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2,
                .pNext = nullptr,
                .flags = description.flags,
                .format = description.format,
                .samples = description.samples,
                .loadOp = description.loadOp,
                .storeOp = description.storeOp,
                .stencilLoadOp = description.stencilLoadOp,
                .stencilStoreOp = description.stencilStoreOp,
                .initialLayout = description.initialLayout,
                .finalLayout = description.finalLayout,
            });
        }
        const auto promote = [](const VkAttachmentReference& reference) {
            return VkAttachmentReference2{
                .sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,
                .pNext = nullptr,
                .attachment = reference.attachment,
                .layout = reference.layout,
                .aspectMask = 0,
            };
        };
        std::array<VkAttachmentReference2, 8> references2{};
        std::array<VkAttachmentReference2, 8> resolve_references2{};
        for (size_t index = 0; index < references.size(); ++index) {
            references2[index] = promote(references[index]);
            resolve_references2[index] = promote(resolve_references[index]);
        }
        const VkAttachmentReference2 depth_reference2 = promote(depth_reference);
        const VkAttachmentReference2 depth_resolve_reference2 = promote(depth_resolve_reference);
        const ResolveModes resolve_modes = PickResolveModes(*device, key.depth_format);
        const VkSubpassDescriptionDepthStencilResolve depth_stencil_resolve{
            .sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_DEPTH_STENCIL_RESOLVE,
            .pNext = nullptr,
            .depthResolveMode = resolve_modes.depth,
            .stencilResolveMode = resolve_modes.stencil,
            .pDepthStencilResolveAttachment = &depth_resolve_reference2,
        };
        const VkSubpassDescription2 subpass2{
            .sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2,
            .pNext = do_resolve_depth_stencil ? &depth_stencil_resolve : nullptr,
            .flags = 0,
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .viewMask = 0,
            .inputAttachmentCount = 0,
            .pInputAttachments = nullptr,
            .colorAttachmentCount = num_attachments,
            .pColorAttachments = references2.data(),
            .pResolveAttachments = do_resolve_color ? resolve_references2.data() : nullptr,
            .pDepthStencilAttachment = has_depth ? &depth_reference2 : nullptr,
            .preserveAttachmentCount = 0,
            .pPreserveAttachments = nullptr,
        };
        const VkSubpassDependency2 dependency2{
            .sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2,
            .pNext = nullptr,
            .srcSubpass = dependency.srcSubpass,
            .dstSubpass = dependency.dstSubpass,
            .srcStageMask = dependency.srcStageMask,
            .dstStageMask = dependency.dstStageMask,
            .srcAccessMask = dependency.srcAccessMask,
            .dstAccessMask = dependency.dstAccessMask,
            .dependencyFlags = dependency.dependencyFlags,
            .viewOffset = 0,
        };
        pair->second = device->GetLogical().CreateRenderPass2({
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2,
            .pNext = nullptr,
            .flags = 0,
            .attachmentCount = static_cast<u32>(descriptions2.size()),
            .pAttachments = descriptions2.empty() ? nullptr : descriptions2.data(),
            .subpassCount = 1,
            .pSubpasses = &subpass2,
            .dependencyCount = 1,
            .pDependencies = &dependency2,
            .correlatedViewMaskCount = 0,
            .pCorrelatedViewMasks = nullptr,
        });
        return *pair->second;
    }

    pair->second = device->GetLogical().CreateRenderPass({
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .attachmentCount = static_cast<u32>(descriptions.size()),
        .pAttachments = descriptions.empty() ? nullptr : descriptions.data(),
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &dependency,
    });
    return *pair->second;
}

} // namespace Vulkan
