// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/common_funcs.h"
#include "common/common_types.h"
#include "common/settings_enums.h"
#include "video_core/metal_common/metal_wrapper.h"

namespace Metal {

class Device;

// Owns the CAMetalLayer and hands out drawables. Much smaller than vk_swapchain
// because Metal has no swapchain object to create, resize or recreate.
// CAMetalLayer manages its own pool, resizing is a property assignment, and
// there is no VK_ERROR_OUT_OF_DATE_KHR to recover from. What is left is
// acquiring drawables, present mode policy, and never holding more than one
// drawable at a time.
//
// Holding one means the caller has to drain the scheduler worker before
// acquiring, since Present() runs there. Queueing the drawables instead removes
// that drain. Tried it, measured no change at all, the whole recording path is
// under a millisecond of an 18ms frame. Not worth the cross-thread ownership.
class Swapchain : Common::NonCopyable, Common::NonMoveable {
public:
    explicit Swapchain(const Device& device, Owned<CA::MetalLayer> layer);
    ~Swapchain();

    /// Resizes the drawable if @p width or @p height changed, cheap to call every
    /// frame. Returns true when the size really changed, which rebuilds the
    /// layer's drawable pool and invalidates anything keyed on the old drawables'
    /// addresses.
    bool Configure(u32 width, u32 height);

    /// Applies a vsync mode. Safe to call every frame, only acts on change.
    void SetVSyncMode(Settings::VSyncMode mode);

    /// Acquires the next drawable, or null when none is available. Null is
    /// normal, it means the layer is off screen or every drawable is still in
    /// flight. Drop the frame rather than block, blocking here stalls the GPU
    /// thread behind the compositor. The swapchain owns the returned drawable
    /// until Present() or the next AcquireDrawable().
    [[nodiscard]] CA::MetalDrawable* AcquireDrawable();

    /// The texture to render into for the drawable currently held, or null.
    [[nodiscard]] MTL::Texture* GetDrawableTexture() const;

    /// Enqueues the queue-side wait for the acquired drawable. Worker thread
    /// only, see the definition for why it cannot be done at acquire time.
    void WaitDrawableMTL4(MTL4::CommandQueue* queue, CA::MetalDrawable* drawable);

    /// Presents the acquired drawable. Metal 4 has no presentDrawable on the
    /// command buffer, it splits the two halves: the queue signals that rendering
    /// into the drawable is done, then the drawable is presented. The wait side
    /// goes in at acquire time, see AcquireDrawable.
    void PresentMTL4(MTL4::CommandQueue* queue, CA::MetalDrawable* drawable);

    /// Signals any drawable the queue is still blocked on, and says whether
    /// there was one. For the stall handler: a wait with no matching present
    /// blocks the queue silently and forever, so this is the only way out
    /// short of ending the run. Safe to call when nothing is outstanding.
    bool ReleaseBlockedDrawable(MTL4::CommandQueue* queue);

    /// Forgets any outstanding wait without signalling it, for when the queue
    /// that held it has been thrown away. Signalling on the replacement would
    /// name a wait it never made.
    void ForgetBlockedDrawable() noexcept {
        waited_drawable = nullptr;
    }

    /// Drops the swapchain's reference to the acquired drawable, once the caller
    /// has taken its own and handed the drawable to the wait and the present.
    /// Holding past that starves a three-deep pool down to two and nextDrawable()
    /// stalls waiting for one to come back, which showed up as "Drawable acquired
    /// while one was still held". Cleared here on the thread that acquired it,
    /// not from the worker in PresentMTL4, which would race the next acquire.
    void ReleaseAcquiredDrawable() noexcept {
        current_drawable = {};
    }

    [[nodiscard]] MTL::PixelFormat GetPixelFormat() const noexcept {
        return pixel_format;
    }

    [[nodiscard]] u32 GetWidth() const noexcept {
        return width;
    }

    [[nodiscard]] u32 GetHeight() const noexcept {
        return height;
    }

private:
    const Device& device;
    Owned<CA::MetalLayer> layer;

    /// Retained: nextDrawable() is autoreleased but the drawable has to survive
    /// until the command buffer that presents it is committed, which happens in
    /// a different autorelease scope.
    Owned<CA::MetalDrawable> current_drawable;

    /// The drawable the queue is currently blocked on, or null. Only touched from
    /// the worker, where both the wait and the signal run.
    ///
    /// A queue::wait on a drawable that never gets signalled blocks the queue for
    /// good with no error at all. Submissions behind it report a GPU start time
    /// of zero, having never been scheduled, and every Wait on their tick hangs.
    /// Tracking the outstanding one lets us release an orphan instead.
    CA::MetalDrawable* waited_drawable{};

    MTL::PixelFormat pixel_format{MTL::PixelFormatBGRA8Unorm};
    u32 width{};
    u32 height{};
    Settings::VSyncMode vsync_mode{Settings::VSyncMode::Fifo};
    bool vsync_configured{false};
};

} // namespace Metal
