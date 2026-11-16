// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstring>
#include <algorithm>
#include <filesystem>
#include <system_error>
#include <chrono>
#include <atomic>
#include <set>
#include <mutex>
#include <string>
#include <vector>

#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "common/settings.h"
#include "core/frontend/emu_window.h"
#include "core/frontend/graphics_context.h"
#include "video_core/capture.h"
#include "video_core/framebuffer_config.h"
#include "video_core/host1x/gpu_device_memory_manager.h"
#include "video_core/metal_common/metal_surface.h"
#include "video_core/renderer_metal/maxwell_to_mtl.h"
#include "video_core/textures/decoders.h"
#include "video_core/renderer_metal/renderer_metal.h"

namespace Metal {
RendererMetal::RendererMetal(Core::Frontend::EmuWindow& emu_window,
                             Tegra::MaxwellDeviceMemoryManager& device_memory, Tegra::GPU& gpu,
                             std::unique_ptr<Core::Frontend::GraphicsContext> context_)
    : RendererBase(emu_window, std::move(context_)), m_gpu{gpu},
      m_device{Settings::values.vulkan_device.GetValue()}, m_memory_allocator{m_device},
      m_scheduler{m_device}, m_staging_pool{m_device, m_memory_allocator, m_scheduler},
      m_swapchain{m_device, CreateSurface(m_device, emu_window.GetWindowInfo())},
      m_rasterizer{gpu,           device_memory, m_device,
                   m_memory_allocator, m_staging_pool, m_scheduler},
      m_blit_screen{m_device, m_scheduler}, m_device_memory{device_memory} {
    // A drawable waited on and never presented blocks the queue with no error, and
    // the thread that could repair it is the one stuck waiting.
    m_scheduler.GetMasterSemaphore().SetStallHandler(
        [this] { static_cast<void>(m_swapchain.ReleaseBlockedDrawable(m_device.GetMTL4Queue())); });
    // A rebuilt queue never made the wait the old one was holding.
    m_device.SetQueueRebuiltHandler([this] { m_swapchain.ForgetBlockedDrawable(); });
    LOG_INFO(Render_Metal, "Metal renderer initialized");
}

RendererMetal::~RendererMetal() = default;

MTL::Texture* RendererMetal::UploadFramebuffer(const Tegra::FramebufferConfig& config) {
    const DAddr framebuffer_addr = config.address + config.offset;
    if (framebuffer_addr == 0) {
        return nullptr;
    }

    const auto pixel_format = VideoCore::Surface::PixelFormatFromGPUPixelFormat(config.pixel_format);
    const FormatInfo format_info = SurfaceFormat(m_device, pixel_format);
    if (format_info.support == FormatSupport::Unsupported) {
        LOG_DEBUG(Render_Metal, "Unsupported framebuffer format {}",
                  static_cast<u32>(config.pixel_format));
        return nullptr;
    }

    const u32 bytes_per_pixel = VideoCore::Surface::BytesPerBlock(pixel_format);

    // The guest buffer is block-linear, so its footprint is NOT stride * height *
    // bpp. Reading only the linear size truncates the source.
    constexpr u32 BLOCK_HEIGHT_LOG2 = 4;
    const u64 tiled_size = Tegra::Texture::CalculateSize(true, bytes_per_pixel, config.stride,
                                                         config.height, 1, BLOCK_HEIGHT_LOG2, 0);
    const u64 linear_size = static_cast<u64>(config.width) * config.height * bytes_per_pixel;
    if (tiled_size == 0 || linear_size == 0) {
        return nullptr;
    }

    // GetPointer, not ReadBlockUnsafe: the latter does not see the display
    // buffer's backing and returns all zeros for this address.
    const u8* const host_ptr = m_device_memory.GetPointer<u8>(framebuffer_addr);
    if (host_ptr == nullptr) {
        return nullptr;
    }

    if (!m_framebuffer_textures[0] || m_framebuffer_width != config.width ||
        m_framebuffer_height != config.height || m_framebuffer_format != format_info.format) {
        ScopedAutoreleasePool pool;

        for (Owned<MTL::Texture>& slot : m_framebuffer_textures) {
            MTL::TextureDescriptor* descriptor = MTL::TextureDescriptor::alloc()->init();
            descriptor->setTextureType(MTL::TextureType2D);
            descriptor->setPixelFormat(format_info.format);
            descriptor->setWidth(config.width);
            descriptor->setHeight(config.height);
            descriptor->setMipmapLevelCount(1);
            descriptor->setUsage(MTL::TextureUsageShaderRead);
            slot = m_memory_allocator.CreateTexture(descriptor, MemoryUsage::Upload);
            descriptor->release();

            if (!slot) {
                return nullptr;
            }
            slot->setLabel(ToNSString("eden guest framebuffer"));
        }
        m_framebuffer_slot = 0;
        m_framebuffer_width = config.width;
        m_framebuffer_height = config.height;
        m_framebuffer_format = format_info.format;
        LOG_INFO(Render_Metal, "Guest framebuffer upload target: {}x{}", config.width,
                 config.height);
    }

    m_framebuffer_staging.resize_destructive(linear_size);
    Tegra::Texture::UnswizzleTexture(
        std::span<u8>(m_framebuffer_staging.data(), linear_size),
        std::span<const u8>(host_ptr, tiled_size), bytes_per_pixel, config.width, config.height, 1,
        BLOCK_HEIGHT_LOG2, 0);

    // Rotate rather than block: replaceRegion writes host memory the GPU may still
    // be reading, and a wait between acquire and present stops presentation.
    m_framebuffer_slot = (m_framebuffer_slot + 1) % FRAMEBUFFER_SLOTS;
    MTL::Texture* const target = m_framebuffer_textures[m_framebuffer_slot].get();

    target->replaceRegion(
        MTL::Region::Make2D(0, 0, config.width, config.height), 0,
        m_framebuffer_staging.data(), static_cast<NS::UInteger>(config.width) * bytes_per_pixel);

    return target;
}

namespace {
/// Runs at the end of a frame, presented or not: the caches age by frame tick,
/// so a stalled TickFrame means nothing is ever old enough to reclaim.
struct FrameEnd {
    Tegra::GPU& gpu;
    Core::Frontend::EmuWindow& window;
    RasterizerMetal& rasterizer;
    ~FrameEnd() {
        gpu.RendererFrameEndNotify();
        window.OnFrameDisplayed();
        rasterizer.TickFrame();
    }
};

} // Anonymous namespace

void RendererMetal::Composite(std::span<const Tegra::FramebufferConfig> framebuffers) {
    // Threads with no run loop have no autorelease pool, and without one the +0
    // drawable from nextDrawable() never returns to the layer's pool.
    ScopedAutoreleasePool pool;

    if (framebuffers.empty()) {
        return;
    }

    const FrameEnd frame_end{m_gpu, render_window, m_rasterizer};

    const auto& layout = render_window.GetFramebufferLayout();
    if (m_swapchain.Configure(layout.width, layout.height)) {
        // The layer rebuilds its drawable pool on a resize, so a new drawable can
        // land on a freed address that m_resident_drawables still reads as
        // resident. Finish first, old drawables may be in flight.
        if (!m_resident_drawables.empty()) {
            m_scheduler.Finish();
            for (MTL::Texture* const stale : m_resident_drawables) {
                m_device.RemoveResidentAllocation(stale);
            }
            m_resident_drawables.clear();
            m_device.CommitResidency();
        }
    }
    m_swapchain.SetVSyncMode(Settings::values.vsync_mode.GetValue());

    // Acquire on this thread, never on the worker: nextDrawable() blocks when no
    // drawable is free, and blocking the worker stalls the submissions that would
    // release one. An un-presented drawable permanently loses its slot in the
    // layer's pool, and after three frames nextDrawable() blocks forever.
    m_scheduler.WaitWorker();

    // Retained, the swapchain's own reference goes when the next frame acquires.
    CA::MetalDrawable* const drawable = m_swapchain.AcquireDrawable();
    if (drawable == nullptr) {
        return;
    }
    drawable->retain();
    m_swapchain.ReleaseAcquiredDrawable();

    MTL::Texture* const target = drawable->texture();
    if (target == nullptr) {
        drawable->release();
        return;
    }

    // This texture comes from CAMetalLayer rather than our allocator, so nothing
    // else adds it to the residency set. The layer recycles a small pool.
    if (m_resident_drawables.insert(target).second) {
        m_device.AddResidentAllocation(target);
        m_device.CommitResidency();
    }

    // Recorded, so the queue-side wait is enqueued from the worker in the same
    // order as the signal that follows this frame's commit.
    m_scheduler.Record([this, drawable](CommandContext&) {
        m_swapchain.WaitDrawableMTL4(m_device.GetMTL4Queue(), drawable);
    });
    // From the drawable itself, not the size the swapchain last asked for: the two
    // disagree across a resize, and the present pass loads with DontCare.
    const u32 width = static_cast<u32>(target->width());
    const u32 height = static_cast<u32>(target->height());

    // Bottom layer first. Library applets (the software keyboard above all) are
    // composited by nvnflinger as their own layer on top of the game's.
    bool drew_any = false;
    for (const Tegra::FramebufferConfig& config : framebuffers) {
        u32 guest_width = 0;
        u32 guest_height = 0;
        MTL::Texture* source =
            m_rasterizer.GetFramebufferTexture(config, &guest_width, &guest_height);
        if (source == nullptr) {
            source = UploadFramebuffer(config);
        }
        if (source == nullptr || !m_blit_screen.IsValid()) {
            continue;
        }
        // The guest's crop_rect is the authority on what is on screen (it narrows
        // rather than shrinking the buffer across a docked/handheld switch) and it
        // carries the transform flags. Pass the guest extent, not the texture's.
        const auto crop = Tegra::NormalizeCrop(
            config, guest_width != 0 ? guest_width : static_cast<u32>(source->width()),
            guest_height != 0 ? guest_height : static_cast<u32>(source->height()));

        // The first layer that resolves owns the drawable, whatever its index: if
        // layer 0's lookup missed, layer 1 still has to clear rather than blend
        // onto undefined contents. layout.screen is what letterboxes.
        m_blit_screen.Draw(source, target, width, height, crop.left, crop.top, crop.right,
                           crop.bottom, config.blending, !drew_any, layout.screen);
        drew_any = true;
    }

    if (!drew_any) {
        RenderPassState pass{};
        pass.num_color = 1;
        pass.color[0].texture = target;
        pass.color[0].load_action = LoadAction::Clear;
        pass.color[0].store_action = StoreAction::Store;
        pass.color[0].clear_color = {0.0, 0.0, 0.0, 1.0};
        pass.render_area_width = width;
        pass.render_area_height = height;
        m_scheduler.RequestRenderPass(pass);
    }

    // Presented from Flush's after_commit, never from a recorded command:
    // signalling mid-encode orders the signal before the rendering it must follow.
    m_scheduler.Record([](CommandContext& context) { context.EndEncoding(); });

    m_scheduler.Flush([this, drawable]() {
        m_swapchain.PresentMTL4(m_device.GetMTL4Queue(), drawable);
        drawable->release();
    });

    // Only once asked for, this is a second full-screen render every frame.
    if (m_capture_requested.load(std::memory_order_relaxed)) {
        RenderCaptureLayer(framebuffers);
    }

    RenderScreenshot(framebuffers);
}

void RendererMetal::RenderScreenshot(std::span<const Tegra::FramebufferConfig> framebuffers) {
    if (!renderer_settings.screenshot_requested) {
        return;
    }
    const auto& layout = renderer_settings.screenshot_framebuffer_layout;
    const u32 width = layout.width;
    const u32 height = layout.height;
    // The callback's argument is invert_y, not success, so a failed capture is dropped.
    const auto fail = [this] {
        LOG_WARNING(Render_Metal, "Screenshot could not be captured");
        renderer_settings.screenshot_requested = false;
    };
    if (width == 0 || height == 0 || !m_blit_screen.IsValid()) {
        fail();
        return;
    }

    if (!m_screenshot_texture || m_screenshot_width != width || m_screenshot_height != height) {
        if (m_screenshot_texture) {
            m_scheduler.Finish();
            m_device.RemoveResidentAllocation(m_screenshot_texture.get());
            m_device.RemoveResidentAllocation(m_screenshot_readback.get());
        }
        MTL::TextureDescriptor* descriptor = MTL::TextureDescriptor::alloc()->init();
        descriptor->setTextureType(MTL::TextureType2D);
        // sRGB like the drawable, so the bytes read back are the ones that are displayed.
        descriptor->setPixelFormat(MTL::PixelFormatBGRA8Unorm_sRGB);
        descriptor->setWidth(width);
        descriptor->setHeight(height);
        descriptor->setMipmapLevelCount(1);
        descriptor->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
        m_screenshot_texture = m_memory_allocator.CreateTexture(descriptor, MemoryUsage::DeviceLocal);
        descriptor->release();
        m_screenshot_readback =
            m_memory_allocator.CreateBuffer(u64{width} * height * 4, MemoryUsage::Download);
        if (!m_screenshot_texture || !m_screenshot_readback) {
            m_screenshot_texture = {};
            m_screenshot_readback = {};
            fail();
            return;
        }
        m_device.CommitResidency();
        m_screenshot_width = width;
        m_screenshot_height = height;
    }

    bool drew_any = false;
    for (const Tegra::FramebufferConfig& config : framebuffers) {
        u32 guest_width = 0;
        u32 guest_height = 0;
        MTL::Texture* source =
            m_rasterizer.GetFramebufferTexture(config, &guest_width, &guest_height);
        if (source == nullptr) {
            source = UploadFramebuffer(config);
        }
        if (source == nullptr) {
            continue;
        }
        const auto crop = Tegra::NormalizeCrop(
            config, guest_width != 0 ? guest_width : static_cast<u32>(source->width()),
            guest_height != 0 ? guest_height : static_cast<u32>(source->height()));
        m_blit_screen.Draw(source, m_screenshot_texture.get(), width, height, crop.left, crop.top,
                           crop.right, crop.bottom, config.blending, !drew_any, layout.screen);
        drew_any = true;
    }
    if (!drew_any) {
        fail();
        return;
    }

    MTL::Texture* const src = m_screenshot_texture.get();
    MTL::Buffer* const dst = m_screenshot_readback.get();
    m_scheduler.Record([src, dst, width, height](CommandContext& context) {
        BlitEncoder encoder = context.RequestBlit();
        if (encoder == nullptr) {
            return;
        }
        encoder->copyFromTexture(src, 0, 0, MTL::Origin::Make(0, 0, 0),
                                 MTL::Size::Make(width, height, 1), dst, 0, size_t{width} * 4,
                                 size_t{width} * height * 4, MTL::BlitOptionNone);
    });
    m_scheduler.Finish();

    std::memcpy(renderer_settings.screenshot_bits, dst->contents(), size_t{width} * height * 4);
    renderer_settings.screenshot_complete_callback(false);
    renderer_settings.screenshot_requested = false;
}

void RendererMetal::RenderCaptureLayer(std::span<const Tegra::FramebufferConfig> framebuffers) {
    using namespace VideoCore::Capture;

    // Reached from the service thread as well as the GPU thread, see Composite.
    ScopedAutoreleasePool pool;

    if (!m_capture_texture) {
        MTL::TextureDescriptor* descriptor = MTL::TextureDescriptor::alloc()->init();
        descriptor->setTextureType(MTL::TextureType2D);
        descriptor->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
        descriptor->setWidth(LinearWidth);
        descriptor->setHeight(LinearHeight);
        descriptor->setMipmapLevelCount(1);
        descriptor->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
        m_capture_texture = m_memory_allocator.CreateTexture(descriptor, MemoryUsage::DeviceLocal);
        descriptor->release();
        if (!m_capture_texture) {
            return;
        }
        m_capture_texture->setLabel(ToNSString("eden applet capture"));
    }

    bool drew_any = false;
    for (const Tegra::FramebufferConfig& config : framebuffers) {
        MTL::Texture* source = m_rasterizer.GetFramebufferTexture(config);
        if (source == nullptr || !m_blit_screen.IsValid()) {
            continue;
        }
        const auto crop = Tegra::NormalizeCrop(config, static_cast<u32>(source->width()),
                                               static_cast<u32>(source->height()));
        m_blit_screen.Draw(source, m_capture_texture.get(), LinearWidth, LinearHeight, crop.left,
                           crop.top, crop.right, crop.bottom, config.blending, !drew_any);
        drew_any = true;
    }
}

std::vector<u8> RendererMetal::GetAppletCaptureBuffer() {
    using namespace VideoCore::Capture;

    ScopedAutoreleasePool pool;

    m_capture_requested.store(true, std::memory_order_relaxed);

    // The guest expects the capture block-linear, hence readback plus swizzle.
    std::vector<u8> out(TiledSize);
    if (!m_capture_texture) {
        return out;
    }

    const size_t linear_size = size_t{LinearWidth} * LinearHeight * BytesPerPixel;
    if (!m_capture_readback) {
        m_capture_readback = Adopt(
            m_device.GetDevice()->newBuffer(linear_size, MTL::ResourceStorageModeShared));
        if (m_capture_readback) {
            m_device.AddResidentAllocation(m_capture_readback.get());
            m_device.CommitResidency();
        }
        if (!m_capture_readback) {
            return out;
        }
    }

    MTL::Texture* const src = m_capture_texture.get();
    MTL::Buffer* const dst = m_capture_readback.get();
    m_scheduler.Record([src, dst](CommandContext& context) {
        BlitEncoder encoder = context.RequestBlit();
        if (encoder == nullptr) {
            return;
        }
        encoder->copyFromTexture(src, 0, 0, MTL::Origin::Make(0, 0, 0),
                                 MTL::Size::Make(LinearWidth, LinearHeight, 1), dst, 0,
                                 LinearWidth * BytesPerPixel,
                                 size_t{LinearWidth} * LinearHeight * BytesPerPixel,
                                 MTL::BlitOptionNone);
    });
    m_scheduler.Finish();

    Tegra::Texture::SwizzleTexture(
        out, std::span<const u8>(static_cast<const u8*>(dst->contents()), linear_size),
        BytesPerPixel, LinearWidth, LinearHeight, LinearDepth, BlockHeight, BlockDepth);
    return out;
}

} // namespace Metal
