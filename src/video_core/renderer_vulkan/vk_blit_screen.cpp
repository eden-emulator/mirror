// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 Torzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <vulkan/vulkan_core.h>
#include "common/settings.h"
#include "video_core/framebuffer_config.h"
#include "video_core/present.h"
#include "video_core/renderer_vulkan/present/filters.h"
#include "video_core/renderer_vulkan/present/layer.h"
#include "video_core/renderer_vulkan/vk_blit_screen.h"
#include "video_core/renderer_vulkan/vk_present_manager.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"

namespace Vulkan {

BlitScreen::BlitScreen(Tegra::MaxwellDeviceMemoryManager& device_memory_, const Device& device_, MemoryAllocator& memory_allocator_, PresentManager& present_manager_, Scheduler& scheduler_, const PresentFilters& filters_)
    : device_memory{device_memory_}
    , memory_allocator{memory_allocator_}
    , present_manager{present_manager_}
    , scheduler{scheduler_}
    , filters{filters_}
    , image_count{1}
    , image_index{0}
    , swapchain_view_format{VK_FORMAT_B8G8R8A8_UNORM}
{}

BlitScreen::~BlitScreen() = default;

void BlitScreen::WaitIdle(const Device& device) {
    scheduler.Finish();
    present_manager.WaitPresent();
    device.GetLogical().WaitIdle();
}

void BlitScreen::SetWindowAdaptPass(const Device& device) {
    layers.clear();
    scaling_filter = filters.get_scaling_filter();

    switch (scaling_filter) {
    case Settings::ScalingFilter::NearestNeighbor:
        window_adapt = MakeNearestNeighbor(device, swapchain_view_format);
        break;
    case Settings::ScalingFilter::Bicubic:
        window_adapt = MakeBicubic(device, swapchain_view_format, VK_CUBIC_FILTER_WEIGHTS_CATMULL_ROM_QCOM);
        break;
    case Settings::ScalingFilter::ZeroTangent:
        window_adapt = MakeBicubic(device, swapchain_view_format, VK_CUBIC_FILTER_WEIGHTS_ZERO_TANGENT_CARDINAL_QCOM);
        break;
    case Settings::ScalingFilter::BSpline:
        window_adapt = MakeBicubic(device, swapchain_view_format, VK_CUBIC_FILTER_WEIGHTS_B_SPLINE_QCOM);
        break;
    case Settings::ScalingFilter::Mitchell:
        window_adapt = MakeBicubic(device, swapchain_view_format, VK_CUBIC_FILTER_WEIGHTS_MITCHELL_NETRAVALI_QCOM);
        break;
    case Settings::ScalingFilter::Spline1:
        window_adapt = MakeSpline1(device, swapchain_view_format);
        break;
    case Settings::ScalingFilter::Gaussian:
        window_adapt = MakeGaussian(device, swapchain_view_format);
        break;
    case Settings::ScalingFilter::Lanczos:
        window_adapt = MakeLanczos(device, swapchain_view_format);
        break;
    case Settings::ScalingFilter::ScaleForce:
        window_adapt = MakeScaleForce(device, swapchain_view_format);
        break;
    case Settings::ScalingFilter::Area:
        window_adapt = MakeArea(device, swapchain_view_format);
        break;
    case Settings::ScalingFilter::Mmpx:
        window_adapt = MakeMmpx(device, swapchain_view_format);
        break;
    case Settings::ScalingFilter::Fsr:
    case Settings::ScalingFilter::Sgsr:
    case Settings::ScalingFilter::SgsrEdge:
    case Settings::ScalingFilter::Bilinear:
    default:
        window_adapt = MakeBilinear(device, swapchain_view_format);
        break;
    }
}

void BlitScreen::PrepareFrame(const Device& device, Frame* frame,
                              const Layout::FramebufferLayout& layout) {
    if (!window_adapt) {
        return;
    }

    if (frame->width == layout.width && frame->height == layout.height) {
        return;
    }

    WaitIdle(device);
    present_manager.RecreateFrame(frame, layout.width, layout.height, swapchain_view_format);
}

FrameGenSource BlitScreen::GenerationSource() const {
    if (layers.empty()) {
        return {};
    }
    return FrameGenSource{
        .image = layers.front().GenerationSource(),
        .extent = layers.front().GenerationExtent(),
    };
}

bool BlitScreen::IsGenerationFree(size_t generation) const {
    return !layers.empty() && layers.front().IsGenerationFree(generation);
}

void BlitScreen::DrawGenerated(const Device& device, Frame* frame,
                               const Layout::FramebufferLayout& layout, size_t generation,
                               VkImage image, VkImageView view) {
    window_adapt->DrawGenerated(device, scheduler, layers.front(), generation, image, view, layout,
                                frame);
}

void BlitScreen::DrawToFrame(const Device& device, RasterizerVulkan& rasterizer, Frame* frame,
                             std::span<const Tegra::FramebufferConfig> framebuffers,
                             const Layout::FramebufferLayout& layout,
                             size_t current_swapchain_image_count,
                             VkFormat current_swapchain_view_format) {
    bool resource_update_required = false;
    bool presentation_recreate_required = false;

    if (!window_adapt || scaling_filter != filters.get_scaling_filter()) {
        resource_update_required = true;
    }

    if (image_count != current_swapchain_image_count) {
        resource_update_required = true;
        image_count = current_swapchain_image_count;
    }

    if (swapchain_view_format != current_swapchain_view_format ||
        layout.width != frame->width || layout.height != frame->height) {
        resource_update_required = true;
        presentation_recreate_required = true;
        swapchain_view_format = current_swapchain_view_format;
    }

    if (resource_update_required) {
        WaitIdle(device);
        SetWindowAdaptPass(device);

        if (presentation_recreate_required) {
            present_manager.RecreateFrame(frame, layout.width, layout.height,
                                          swapchain_view_format);
        }

        image_index = 0;
    }

    const VkExtent2D window_size{
        .width = layout.screen.GetWidth(),
        .height = layout.screen.GetHeight(),
    };

    const size_t generations = Settings::FrameGenMaxGenerations();
    if (layers.size() != framebuffers.size() || generation_count != generations) {
        layers.clear();
        generation_count = generations;
        for (size_t i = 0; i < framebuffers.size(); ++i) {
            layers.emplace_back(device, memory_allocator, scheduler, device_memory, image_count,
                                generation_count, window_size,
                                window_adapt->GetDescriptorSetLayout(), filters);
        }
    }

    window_adapt->Draw(device, rasterizer, scheduler, image_index, layers, framebuffers, layout, frame);

    if (++image_index >= image_count) {
        image_index = 0;
    }
}

} // namespace Vulkan
