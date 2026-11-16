// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <TargetConditionals.h>

#include "common/logging.h"
#include "video_core/metal_common/metal_device.h"
#include "video_core/renderer_metal/mtl_swapchain.h"

namespace Metal {
namespace {
/// Two is the minimum for double buffering and has the lowest latency; three
/// lets the CPU run a frame ahead. Metal caps it at 3.
NS::UInteger DrawableCountFor(Settings::VSyncMode mode) {
    switch (mode) {
    case Settings::VSyncMode::Immediate:
        return 2;
    case Settings::VSyncMode::Mailbox:
    case Settings::VSyncMode::Fifo:
    case Settings::VSyncMode::FifoRelaxed:
    default:
        return 3;
    }
}

} // Anonymous namespace

Swapchain::Swapchain(const Device& device_, Owned<CA::MetalLayer> layer_)
    : device{device_}, layer{std::move(layer_)} {
    // The presentation blit hands back linear values; writing those into the
    // layer's default BGRA8Unorm drawable stores them raw and the compositor reads
    // them back as sRGB, which is why the picture came out too dark.
    layer->setPixelFormat(MTL::PixelFormatBGRA8Unorm_sRGB);
    pixel_format = layer->pixelFormat();
    LOG_INFO(Render_Metal, "Swapchain created with drawable format {}",
             static_cast<u32>(pixel_format));
}

Swapchain::~Swapchain() {
    // A drawable that is never presented holds a slot in the layer's pool.
    current_drawable = {};
}

bool Swapchain::Configure(u32 new_width, u32 new_height) {
    if (new_width == 0 || new_height == 0) {
        return false;
    }
    if (new_width == width && new_height == height) {
        return false;
    }

    width = new_width;
    height = new_height;
    layer->setDrawableSize(CGSize{static_cast<double>(width), static_cast<double>(height)});

    LOG_DEBUG(Render_Metal, "Drawable resized to {}x{}", width, height);
    return true;
}

void Swapchain::SetVSyncMode(Settings::VSyncMode mode) {
    if (vsync_configured && mode == vsync_mode) {
        return;
    }
    vsync_mode = mode;
    vsync_configured = true;

    layer->setMaximumDrawableCount(DrawableCountFor(mode));

#if !TARGET_OS_IPHONE
    // iOS composites strictly on the display refresh, so Immediate is FIFO there.
    layer->setDisplaySyncEnabled(mode != Settings::VSyncMode::Immediate);
#else
    if (mode == Settings::VSyncMode::Immediate) {
        LOG_DEBUG(Render_Metal, "Immediate present is unavailable on iOS, using display sync");
    }
#endif
}

CA::MetalDrawable* Swapchain::AcquireDrawable() {
    if (current_drawable) {
        // Holding two at once starves the pool and deadlocks the compositor.
        LOG_WARNING(Render_Metal, "Drawable acquired while one was still held; dropping the old");
        current_drawable = {};
    }

    // nextDrawable() is autoreleased, and the drawable has to survive until the
    // command buffer presenting it is committed, a different autorelease scope.
    current_drawable = Retain(layer->nextDrawable());
    if (!current_drawable) {
        // Normal when the window is occluded or every drawable is in flight.
        return nullptr;
    }
    return current_drawable.get();
}

void Swapchain::WaitDrawableMTL4(MTL4::CommandQueue* queue, CA::MetalDrawable* drawable) {
    // The drawable comes in as an argument instead of from current_drawable
    // because this and PresentMTL4 run on the worker while the render thread is
    // already acquiring the NEXT frame's drawable; reading the member let the wait
    // and the signal name different drawables, leaving one waited on and never
    // signalled until the watchdog kills it with MTL4CommandQueueErrorTimeout.
    if (queue == nullptr || drawable == nullptr) {
        return;
    }
    // An outstanding wait means the last frame was never presented, so release the
    // orphan before piling a second wait on top.
    if (waited_drawable != nullptr && waited_drawable != drawable) {
        static std::atomic<u32> orphans{0};
        const u32 n = orphans.fetch_add(1, std::memory_order_relaxed) + 1;
        LOG_ERROR(Render_Metal,
                  "Drawable {} was waited on and never presented ({} so far); signalling it so "
                  "the queue unblocks. Everything submitted since has been stalled behind it",
                  static_cast<const void*>(waited_drawable), n);
        queue->signalDrawable(waited_drawable);
    }
    waited_drawable = drawable;
    queue->wait(drawable);
}

MTL::Texture* Swapchain::GetDrawableTexture() const {
    return current_drawable ? current_drawable->texture() : nullptr;
}

void Swapchain::PresentMTL4(MTL4::CommandQueue* queue, CA::MetalDrawable* drawable) {
    if (queue == nullptr || drawable == nullptr) {
        return;
    }
    // signalDrawable marks the contents final, then present() hands it over; the
    // classic path fuses both into presentDrawable. Pairs 1:1 with the wait above.
    queue->signalDrawable(drawable);
    if (waited_drawable == drawable) {
        waited_drawable = nullptr;
    }
    drawable->present();
}

bool Swapchain::ReleaseBlockedDrawable(MTL4::CommandQueue* queue) {
    CA::MetalDrawable* const blocked = waited_drawable;
    if (queue == nullptr || blocked == nullptr) {
        return false;
    }
    // Clear the record first, or every later frame thinks it is still blocked.
    waited_drawable = nullptr;
    LOG_ERROR(Render_Metal,
              "Releasing drawable {}, which the queue has been blocked on with no matching "
              "present. Submissions behind it never started",
              static_cast<const void*>(blocked));
    queue->signalDrawable(blocked);
    return true;
}

} // namespace Metal
