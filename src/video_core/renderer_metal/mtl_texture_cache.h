// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <limits>
#include <array>
#include <optional>
#include <span>
#include <unordered_map>

#include "common/common_funcs.h"
#include "common/common_types.h"
#include "shader_recompiler/shader_info.h"
#include <vector>

#include "video_core/metal_common/metal_memory_allocator.h"
#include "video_core/metal_common/metal_wrapper.h"
#include "video_core/renderer_metal/mtl_blit_image.h"
#include "video_core/renderer_metal/mtl_render_pass_state.h"
#include "video_core/renderer_metal/mtl_staging_buffer_pool.h"
#include "video_core/texture_cache/texture_cache_base.h"

namespace Metal {

using VideoCommon::ImageId;
using VideoCommon::Region2D;
using VideoCommon::RenderTargets;
using VideoCore::Surface::PixelFormat;

class Device;
class Scheduler;
class Image;
class ImageView;
class Framebuffer;

/// Allocation bookkeeping the shared cache owns. No Metal state in here.
class ImageAlloc : public VideoCommon::ImageAllocBase {};
/// Backend hooks the shared TextureCache<P> calls into. Narrower than the Vulkan
/// runtime, Metal has no image layouts (so TransitionImageLayout is empty) and no
/// render pass objects (a Framebuffer is just a RenderPassState we hand to the
/// scheduler).
class TextureCacheRuntime {
public:
    explicit TextureCacheRuntime(const Device& device_, Scheduler& scheduler_,
                                 MemoryAllocator& memory_allocator_,
                                 StagingBufferPool& staging_buffer_pool_,
                                 BlitImageHelper& blit_image_helper_);

    void Finish();

    /// Vulkan batches clears in its scheduler. Metal turns them into load actions, so
    /// there is never one to flush.
    void FlushDeferredClear() {}
    void TickFrame();

    /// Throwaway render target for a colour slot whose real surface can't be
    /// bound (guest pointed two attachments at one texture). On a tiler, writing
    /// a colour slot with nothing behind it is undefined and can corrupt the
    /// other outputs in the tile. Keyed by format, size and sample count so the
    /// pass state stays identical and encoder reuse keeps working.
    [[nodiscard]] MTL::Texture* GetScratchAttachment(MTL::PixelFormat format, u32 width,
                                                     u32 height, u32 samples);

    [[nodiscard]] StagingBufferRef UploadStagingBuffer(size_t size, bool deferred = false);
    [[nodiscard]] StagingBufferRef DownloadStagingBuffer(size_t size, bool deferred = false);
    void FreeDeferredStagingBuffer(StagingBufferRef& ref);

    [[nodiscard]] u64 GetDeviceLocalMemory() const;
    [[nodiscard]] u64 GetDeviceMemoryUsage() const;
    [[nodiscard]] bool CanReportMemoryUsage() const;
    [[nodiscard]] std::optional<size_t> GetSamplerHeapBudget() const;

    void BlitImage(Framebuffer* dst_framebuffer, ImageView& dst, ImageView& src,
                   const Region2D& dst_region, const Region2D& src_region,
                   Tegra::Engines::Fermi2D::Filter filter,
                   Tegra::Engines::Fermi2D::Operation operation);

    void CopyImage(Image& dst, Image& src, std::span<const VideoCommon::ImageCopy> copies);
    void CopyImageMSAA(Image& dst, Image& src, std::span<const VideoCommon::ImageCopy> copies);

    [[nodiscard]] bool ShouldReinterpret(Image& dst, Image& src);
    void ReinterpretImage(Image& dst, Image& src, std::span<const VideoCommon::ImageCopy> copies);

    void ConvertImage(Framebuffer* dst, ImageView& dst_view, ImageView& src_view);

    [[nodiscard]] bool CanAccelerateImageUpload(Image&) const noexcept {
        return false;
    }

    [[nodiscard]] bool CanUploadMSAA() const noexcept {
        // A blit can't write into a multisampled texture, so the upload lands in
        // a single-sample staging image and BlitImageHelper::ExpandToMultisample
        // spreads it across the samples, through CopyImageMSAA.
        return true;
    }

    void AccelerateImageUpload(Image&, const StagingBufferRef&,
                               std::span<const VideoCommon::SwizzleParameters>, u32 z_start,
                               u32 z_count);

    /// Empty on purpose. Uploads get their own encoder and the Scheduler already
    /// puts a barrier in when it opens one, so later reads are ordered.
    void InsertUploadMemoryBarrier() {}

    /// Metal has no image layouts; nothing to transition.
    void TransitionImageLayout(Image&) {}

    /// Breaks the render pass when a bound texture overlaps a render target.
    /// Can't be a no-op. Inside an open pass the target is still in tile memory,
    /// so sampling it gives you whatever the texture held when the pass started.
    /// Ending the encoder stores the tiles out.
    void BarrierFeedbackLoop();

    [[nodiscard]] bool HasBrokenTextureViewFormats() const noexcept {
        // Metal texture views need the source texture created with a compatible
        // pixel format list, which we always do.
        return false;
    }

    [[nodiscard]] bool HasNativeBgr() const noexcept {
        return true;
    }

    [[nodiscard]] MTL::Buffer* GetTemporaryBuffer(size_t needed_size);

    /// Frames composited so far. Only used to tell when a cached copy of a live
    /// resource went stale, see ImageView::MakeArrayFromVolume.
    [[nodiscard]] u64 FrameIndex() const noexcept {
        return frame_index;
    }

    [[nodiscard]] bool IsFormatDitherable(PixelFormat format);
    [[nodiscard]] bool IsFormatScalable(PixelFormat format);

    const Device& device;
    Scheduler& scheduler;
    MemoryAllocator& memory_allocator;
    StagingBufferPool& staging_buffer_pool;
    BlitImageHelper& blit_image_helper;
    const Settings::ResolutionScalingInfo& resolution;

private:
    static constexpr size_t INDEXING_SLOTS = 8 * sizeof(size_t);
    std::array<Owned<MTL::Buffer>, INDEXING_SLOTS> temporary_buffers{};

    /// Scratch render targets for unbindable colour slots, keyed by format, size
    /// and sample count. See GetScratchAttachment.
    std::unordered_map<u64, Owned<MTL::Texture>> scratch_attachments;

    u64 frame_index{};
};

class Image : public VideoCommon::ImageBase {
public:
    explicit Image(TextureCacheRuntime&, const VideoCommon::ImageInfo& info, GPUVAddr gpu_addr,
                   VAddr cpu_addr);
    explicit Image(const VideoCommon::NullImageParams&);
    ~Image();

    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;
    Image(Image&&) = default;
    Image& operator=(Image&&) = default;

    void UploadMemory(MTL::Buffer* buffer, u64 offset,
                      std::span<const VideoCommon::BufferImageCopy> copies);
    void UploadMemory(const StagingBufferRef& map,
                      std::span<const VideoCommon::BufferImageCopy> copies);

    void DownloadMemory(MTL::Buffer* buffer, size_t offset,
                        std::span<const VideoCommon::BufferImageCopy> copies);
    void DownloadMemory(std::span<MTL::Buffer*> buffers, std::span<size_t> offsets,
                        std::span<const VideoCommon::BufferImageCopy> copies);
    void DownloadMemory(const StagingBufferRef& map,
                        std::span<const VideoCommon::BufferImageCopy> copies);

    /// The texture the rest of the backend should use. That's the scaled copy
    /// while this image is rescaled.
    [[nodiscard]] MTL::Texture* Handle() const noexcept {
        return IsRescaled() && scaled_texture ? scaled_texture.get() : texture.get();
    }

    [[nodiscard]] MTL::PixelFormat Format() const noexcept {
        return pixel_format;
    }

    [[nodiscard]] u32 Samples() const noexcept {
        return samples;
    }

    [[nodiscard]] bool IsValid() const noexcept {
        return static_cast<bool>(texture);
    }

    /// Whether this image is currently at the scaled resolution. Reads the
    /// shared cache's flag, a private bool here could disagree with it.
    [[nodiscard]] bool IsRescaled() const noexcept {
        return True(flags & VideoCommon::ImageFlagBits::Rescaled);
    }

    /// Move this image to (or back from) the scaled resolution, false if nothing
    /// changed. `ignore` skips the copy when the caller is about to overwrite
    /// the contents anyway. We only honour it when the destination already
    /// exists, a freshly allocated one has to be filled or the image comes back
    /// blank.
    bool ScaleUp(bool ignore = false);
    bool ScaleDown(bool ignore = false);

    /// Tick the image was allocated on, the shared cache evicts by it.
    u64 allocation_tick{};

private:
    /// Copies @p src into @p dst, resampling per level and per layer.
    void BlitScale(MTL::Texture* src, MTL::Texture* dst);

    TextureCacheRuntime* runtime{};
    Owned<MTL::Texture> texture;
    /// Allocated on the first ScaleUp and kept. Scaling toggles with whatever
    /// the guest is drawing, reallocating each way would churn.
    Owned<MTL::Texture> scaled_texture;
    MTL::TextureUsage texture_usage{};
    MTL::PixelFormat pixel_format{MTL::PixelFormatInvalid};
    u32 samples{1};
};

class ImageView : public VideoCommon::ImageViewBase {
public:
    explicit ImageView(TextureCacheRuntime&, const VideoCommon::ImageViewInfo&, ImageId, Image&);
    explicit ImageView(TextureCacheRuntime&, const VideoCommon::ImageViewInfo&, ImageId, Image&,
                       const Common::SlotVector<Image>&);
    explicit ImageView(TextureCacheRuntime&, const VideoCommon::ImageInfo&,
                       const VideoCommon::ImageViewInfo&, GPUVAddr);
    explicit ImageView(TextureCacheRuntime&, const VideoCommon::NullImageViewParams&);
    ~ImageView();

    ImageView(const ImageView&) = delete;
    ImageView& operator=(const ImageView&) = delete;
    ImageView(ImageView&&) = default;
    ImageView& operator=(ImageView&&) = default;

    /// View for a given shader texture type, cached one per (type, format).
    [[nodiscard]] MTL::Texture* Handle(Shader::TextureType texture_type) const noexcept;

private:
    [[nodiscard]] MTL::Texture* MakeViewForType(Shader::TextureType texture_type) const;

public:

    [[nodiscard]] MTL::Texture* RenderTarget() const noexcept {
        return render_target.get();
    }

    /// Mip level and slice the render pass attachment has to select. Zero for
    /// ordinary views, MakeView already narrowed those to the subresource.
    /// Non-zero only when RenderTarget() is a whole 3D texture, Metal can't view
    /// a volume's depth slice as a 2D attachment so the pass descriptor picks it
    /// with level + depthPlane.
    [[nodiscard]] u32 RenderTargetLevel() const noexcept {
        return rt_level;
    }
    [[nodiscard]] u32 RenderTargetSlice() const noexcept {
        return rt_slice;
    }

    /// Planes the pass can reach from RenderTargetSlice() on, that's the volume's
    /// depth at rt_level minus the base plane, 1 for non-volume views. The shared
    /// cache reports one layer for a volume, so without this a layered pass
    /// collapses every plane's write into plane 0.
    [[nodiscard]] u32 RenderTargetLayers() const noexcept {
        return rt_layers;
    }

    /// Re-records the blit that keeps a volume's 2D-array stand-in current.
    void RefreshVolumeCopy(MTL::Texture* destination) const;

    /// View for a storage image at the format the SHADER declared. Reinterpreting
    /// the bits is the whole point of a storage image, using the view's own
    /// format reads and writes through the wrong one. Typeless means no declared
    /// format, so use the view's.
    [[nodiscard]] MTL::Texture* StorageView(Shader::TextureType texture_type,
                                            Shader::ImageFormat image_format);

    [[nodiscard]] MTL::Texture* DepthView();
    [[nodiscard]] MTL::Texture* StencilView();
    [[nodiscard]] MTL::Texture* ColorView();

    /// Size in bytes of the guest range a texel-buffer view covers. Only means
    /// anything for a view built by the buffer constructor.
    [[nodiscard]] u32 BufferSize() const noexcept {
        return buffer_size;
    }

    [[nodiscard]] MTL::Texture* ImageHandle() const noexcept {
        return image_handle;
    }

    [[nodiscard]] MTL::PixelFormat Format() const noexcept {
        return pixel_format;
    }

    [[nodiscard]] u32 Samples() const noexcept {
        return samples;
    }

    [[nodiscard]] bool IsRescaled() const noexcept;

private:
    /// `swizzle` is the component mapping for a shader-visible view, pass nullptr
    /// for an attachment view. Metal can't render into a swizzled view, and the
    /// guest swizzle only says how the shader reads the texture anyway.
    [[nodiscard]] MTL::Texture* MakeView(MTL::PixelFormat format, MTL::TextureType texture_type,
                                         u32 first_level, u32 num_levels, u32 first_layer,
                                         u32 num_layers,
                                         const MTL::TextureSwizzleChannels* swizzle);

    /// Copies a 3D image's depth slices into a real 2D array texture. Metal's
    /// view classes won't let a volume be viewed as an array, so a guest that
    /// samples one that way needs a copy. Returns a +1 texture like MakeView.
    [[nodiscard]] MTL::Texture* MakeArrayFromVolume(MTL::PixelFormat format, u32 first_level,
                                                    u32 num_levels);

    TextureCacheRuntime* runtime{};
    /// The image slots, so a view can tell whether the image behind it is
    /// rescaled right now. Only the render-target constructor gets these, it's
    /// null everywhere else where nobody asks.
    const Common::SlotVector<Image>* slot_images{};
    u32 buffer_size{};
    MTL::Texture* image_handle{};
    Owned<MTL::Texture> render_target;
    u32 rt_level{};
    u32 rt_slice{};
    u32 rt_layers{1};
    /// Set for a view served by a 2D-array copy of a volume, so we can re-record
    /// the copy if the guest wrote the volume since. See MakeArrayFromVolume.
    Owned<MTL::Texture> volume_copy;
    MTL::PixelFormat volume_copy_format{MTL::PixelFormatInvalid};
    u32 volume_copy_first_level{};
    u32 volume_copy_num_levels{};
    mutable u64 volume_copy_frame{~0ull};
    /// One view per shader texture type, made on demand. The shader decides how
    /// it samples and doesn't have to agree with the guest's declared view type.
    mutable std::array<Owned<MTL::Texture>, Shader::NUM_TEXTURE_TYPES> views;
    /// Types Handle() already tried to build, successfully or not. A view that
    /// can't be made never will be, the image doesn't change shape, so without
    /// this every draw retries it and re-logs the failure.
    mutable u32 view_attempted{};
    /// What a lazily created view has to be built with, see Handle().
    MTL::PixelFormat sampled_format{MTL::PixelFormatInvalid};
    MTL::TextureSwizzleChannels view_swizzle{};
    bool view_swizzle_identity{true};
    Owned<MTL::Texture> depth_view;
    /// Storage views keyed by declared image format, see StorageView.
    std::unordered_map<u32, Owned<MTL::Texture>> storage_views;
    Owned<MTL::Texture> stencil_view;
    MTL::PixelFormat pixel_format{MTL::PixelFormatInvalid};
    u32 samples{1};
};

class Sampler {
public:
    explicit Sampler(TextureCacheRuntime&, const Tegra::Texture::TSCEntry&);

    [[nodiscard]] MTL::SamplerState* Handle() const noexcept {
        return sampler.get();
    }

    /// Same sampler at the anisotropy the GUEST asked for, for views where the
    /// user's forced setting changes the result instead of improving it (shadow
    /// maps, single- and dual-channel data textures).
    [[nodiscard]] MTL::SamplerState* HandleWithDefaultAnisotropy() const noexcept {
        return sampler_default_anisotropy ? sampler_default_anisotropy.get() : sampler.get();
    }

    /// Whether the user's setting raised this sampler above the guest's value.
    [[nodiscard]] bool HasAddedAnisotropy() const noexcept {
        return static_cast<bool>(sampler_default_anisotropy);
    }

    /// Same sampler with point filtering. Linear filtering an integer texture is
    /// undefined and Metal won't reject it, so we have to swap it at bind time
    /// once we know the format.
    [[nodiscard]] MTL::SamplerState* HandleWithNearestFilter() const noexcept {
        return sampler_nearest ? sampler_nearest.get() : sampler.get();
    }

    /// Same sampler without depth comparison, for the equally undefined case of
    /// a comparison sampler on a non-depth format.
    [[nodiscard]] MTL::SamplerState* HandleWithoutDepthComparison() const noexcept {
        return sampler_no_compare ? sampler_no_compare.get() : sampler.get();
    }

    [[nodiscard]] bool HasLinearFiltering() const noexcept {
        return has_linear_filtering;
    }

    [[nodiscard]] bool HasDepthComparison() const noexcept {
        return has_depth_comparison;
    }

private:
    Owned<MTL::SamplerState> sampler;
    Owned<MTL::SamplerState> sampler_default_anisotropy;
    Owned<MTL::SamplerState> sampler_nearest;
    Owned<MTL::SamplerState> sampler_no_compare;
    bool has_linear_filtering{};
    bool has_depth_comparison{};
};

/// A set of render targets. Just a RenderPassState value, the scheduler builds
/// the descriptor when the encoder opens and load/store actions are picked
/// per-pass instead of baked in.
class Framebuffer {
public:
    explicit Framebuffer(TextureCacheRuntime& runtime,
                         std::span<ImageView*, NUM_RT> color_buffers, ImageView* depth_buffer,
                         const RenderTargets& key);
    explicit Framebuffer(TextureCacheRuntime& runtime, ImageView* color_buffer,
                         ImageView* depth_buffer, u32 width, u32 height, bool is_rescaled);
    ~Framebuffer();

    Framebuffer(const Framebuffer&) = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;
    Framebuffer(Framebuffer&&) = default;
    Framebuffer& operator=(Framebuffer&&) = default;

    /// Bounds the render area to the smallest attachment. See the definition for
    /// why we can't hand Metal a larger one.
    void ClampRenderArea(u32 width, u32 height);

    /// TEMPORARY diagnostic: colour attachment 0's image id.
    u32 diag_image_id{0xFFFFFFFFu};

    /// Smallest attachment extent, filled in by CreateFramebuffer. Stays at the
    /// maximum with no attachments, so an attachmentless pass keeps the size it
    /// asked for.
    u32 attachment_width{std::numeric_limits<u32>::max()};
    u32 attachment_height{std::numeric_limits<u32>::max()};

    void CreateFramebuffer(TextureCacheRuntime& runtime,
                           std::span<ImageView*, NUM_RT> color_buffers, ImageView* depth_buffer,
                           bool is_rescaled = false);

    /// Pass description to hand to Scheduler::RequestRenderPass. Callers can
    /// override load and store actions on the returned copy.
    [[nodiscard]] const RenderPassState& GetRenderPassState() const noexcept {
        return pass_state;
    }

    [[nodiscard]] u32 RenderAreaWidth() const noexcept {
        return pass_state.render_area_width;
    }

    [[nodiscard]] u32 RenderAreaHeight() const noexcept {
        return pass_state.render_area_height;
    }

    [[nodiscard]] u32 Samples() const noexcept {
        return pass_state.sample_count;
    }

    [[nodiscard]] u32 NumColorBuffers() const noexcept {
        return pass_state.num_color;
    }

    [[nodiscard]] bool HasDepthStencil() const noexcept {
        return pass_state.depth_stencil.texture != nullptr;
    }

    [[nodiscard]] bool IsRescaled() const noexcept {
        return is_rescaled;
    }

    /// Base images behind this pass's attachments, for feedback detection. The
    /// attachments in `pass_state` are views, so comparing those pointers against
    /// a sampled view of the same image finds nothing. Null entries are unused
    /// slots, the depth attachment's image comes last.
    [[nodiscard]] std::span<MTL::Texture* const> AttachmentImages() const noexcept {
        return std::span<MTL::Texture* const>(attachment_images.data(), num_attachment_images);
    }

    /// Image subresources the attachments render into, parallel to AttachmentImages().
    [[nodiscard]] std::span<const VideoCommon::SubresourceRange> AttachmentRanges() const noexcept {
        return std::span<const VideoCommon::SubresourceRange>(attachment_ranges.data(),
                                                              num_attachment_images);
    }

    /// Scheduler attachment bit of each entry, parallel to AttachmentImages(). See
    /// Scheduler::MarkPassDrawn.
    [[nodiscard]] std::span<const u32> AttachmentBits() const noexcept {
        return std::span<const u32>(attachment_bits.data(), num_attachment_images);
    }

private:
    RenderPassState pass_state{};
    bool is_rescaled{};
    std::array<MTL::Texture*, NUM_RT + 1> attachment_images{};
    std::array<VideoCommon::SubresourceRange, NUM_RT + 1> attachment_ranges{};
    std::array<u32, NUM_RT + 1> attachment_bits{};
    u32 num_attachment_images{};
};

struct TextureCacheParams {
    static constexpr bool ENABLE_VALIDATION = true;
    static constexpr bool FRAMEBUFFER_BLITS = false;
    static constexpr bool HAS_EMULATED_COPIES = false;
    static constexpr bool HAS_DEVICE_MEMORY_INFO = true;
    static constexpr bool IMPLEMENTS_ASYNC_DOWNLOADS = true;
    static constexpr bool HAS_MSAA_DOWNLOADS = false;
    static constexpr bool PREEMPTIVE_DOWNLOAD_ON_MODIFICATION = true;

    using Runtime = Metal::TextureCacheRuntime;
    using Image = Metal::Image;
    using ImageAlloc = Metal::ImageAlloc;
    using ImageView = Metal::ImageView;
    using Sampler = Metal::Sampler;
    using Framebuffer = Metal::Framebuffer;
    using AsyncBuffer = Metal::StagingBufferRef;
    using BufferType = MTL::Buffer*;
};

using TextureCache = VideoCommon::TextureCache<TextureCacheParams>;

} // namespace Metal
