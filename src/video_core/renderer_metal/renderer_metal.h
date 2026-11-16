// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <unordered_set>
#include <atomic>
#include <array>
#include <memory>

#include "common/scratch_buffer.h"
#include <string>

#include "video_core/metal_common/metal_device.h"
#include "video_core/metal_common/metal_memory_allocator.h"
#include "video_core/metal_common/metal_wrapper.h"
#include "video_core/renderer_base.h"
#include "video_core/renderer_metal/mtl_blit_screen.h"
#include "video_core/renderer_metal/mtl_rasterizer.h"
#include "video_core/renderer_metal/mtl_scheduler.h"
#include "video_core/renderer_metal/mtl_staging_buffer_pool.h"
#include "video_core/renderer_metal/mtl_swapchain.h"

namespace Core::Frontend {
class EmuWindow;
class GraphicsContext;
} // namespace Core::Frontend

namespace Metal {
class RendererMetal final : public VideoCore::RendererBase {
public:
    explicit RendererMetal(Core::Frontend::EmuWindow& emu_window,
                           Tegra::MaxwellDeviceMemoryManager& device_memory, Tegra::GPU& gpu,
                           std::unique_ptr<Core::Frontend::GraphicsContext> context);
    ~RendererMetal() override;

    void Composite(std::span<const Tegra::FramebufferConfig> framebuffers) override;

    std::vector<u8> GetAppletCaptureBuffer() override;

    /// Drawable textures already added to the device residency set. CAMetalLayer
    /// recycles a small pool, so this stops growing almost immediately.
    std::unordered_set<MTL::Texture*> m_resident_drawables;

    void RenderCaptureLayer(std::span<const Tegra::FramebufferConfig> framebuffers);

    /// Renders the frame at the requested screenshot layout and hands it back as BGRA8.
    void RenderScreenshot(std::span<const Tegra::FramebufferConfig> framebuffers);

    VideoCore::RasterizerInterface* ReadRasterizer() override {
        return &m_rasterizer;
    }

    [[nodiscard]] std::string GetDeviceVendor() const override {
        return m_device.GetVendorName();
    }

private:
    /// Uploads the guest framebuffer straight from device memory. Fallback for
    /// when the guest's final image is not a render target the texture cache
    /// knows about, which is the common case for display-controller compositing.
    [[nodiscard]] MTL::Texture* UploadFramebuffer(const Tegra::FramebufferConfig& config);

    Tegra::GPU& m_gpu;

    Device m_device;
    MemoryAllocator m_memory_allocator;
    Scheduler m_scheduler;
    StagingBufferPool m_staging_pool;
    Swapchain m_swapchain;

    RasterizerMetal m_rasterizer;
    BlitScreen m_blit_screen;

    Tegra::MaxwellDeviceMemoryManager& m_device_memory;

    /// Reused across frames, only reallocated when the layout changes.
    /// Guest-side bytes, read out of device memory before unswizzling.
    Common::ScratchBuffer<u8> m_framebuffer_source;
    /// Linear bytes handed to Metal.
    Common::ScratchBuffer<u8> m_framebuffer_staging;

    /// The composited frame at capture resolution, kept so the applet capture
    /// service has something to read once it has asked at least once. The frame
    /// itself is gone by the time the service gets there.
    Owned<MTL::Texture> m_capture_texture;
    Owned<MTL::Buffer> m_capture_readback;
    /// Latched on the first request for the applet capture buffer, so we do not
    /// render the capture layer every frame. It goes through the full
    /// blit-screen path here, filters and all. The first capture after boot
    /// comes back empty, every one after it is current.
    std::atomic<bool> m_capture_requested{false};

    Owned<MTL::Texture> m_screenshot_texture;
    Owned<MTL::Buffer> m_screenshot_readback;
    u32 m_screenshot_width{};
    u32 m_screenshot_height{};

    /// Upload targets for the non-accelerated present path, used in rotation:
    /// replaceRegion writes from the CPU while the previous frame's present
    /// blit may still be sampling, so one texture cannot be reused every frame.
    static constexpr size_t FRAMEBUFFER_SLOTS = 3;
    std::array<Owned<MTL::Texture>, FRAMEBUFFER_SLOTS> m_framebuffer_textures;
    size_t m_framebuffer_slot{};
    u32 m_framebuffer_width{};
    u32 m_framebuffer_height{};
    MTL::PixelFormat m_framebuffer_format{MTL::PixelFormatInvalid};
};

} // namespace Metal
