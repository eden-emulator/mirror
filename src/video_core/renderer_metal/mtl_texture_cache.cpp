// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <ranges>
#include <set>
#include <string>

#include <boost/container/small_vector.hpp>

#include "common/alignment.h"
#include "common/logging.h"
#include "common/scope_exit.h"
#include "common/settings.h"
#include "video_core/metal_common/metal_device.h"
#include "video_core/renderer_metal/maxwell_to_mtl.h"
#include "video_core/renderer_metal/mtl_scheduler.h"
#include "video_core/renderer_metal/mtl_texture_cache.h"
#include "video_core/texture_cache/decode_bc.h"
#include "video_core/texture_cache/expand_format.h"
#include "video_core/texture_cache/formatter.h"
#include "video_core/texture_cache/samples_helper.h"
#include "video_core/texture_cache/util.h"

namespace Metal {

namespace {

// Round a copy extent up to the format's compressed block size. Metal addresses
// compressed textures in pixels but copies whole blocks, so a sub-block extent
// is illegal, and the last mips of a block-compressed chain always are. Without
// the validation layer Metal just takes the copy and gives an undefined result.
// It doesn't overread, a mip's storage is always whole blocks.
[[nodiscard]] MTL::Size AlignCopySizeToBlock(MTL::Size size, MTL::PixelFormat format) {
    u32 block_width = 1;
    u32 block_height = 1;
    switch (format) {
    case MTL::PixelFormatBC1_RGBA:
    case MTL::PixelFormatBC1_RGBA_sRGB:
    case MTL::PixelFormatBC2_RGBA:
    case MTL::PixelFormatBC2_RGBA_sRGB:
    case MTL::PixelFormatBC3_RGBA:
    case MTL::PixelFormatBC3_RGBA_sRGB:
    case MTL::PixelFormatBC4_RUnorm:
    case MTL::PixelFormatBC4_RSnorm:
    case MTL::PixelFormatBC5_RGUnorm:
    case MTL::PixelFormatBC5_RGSnorm:
    case MTL::PixelFormatBC6H_RGBFloat:
    case MTL::PixelFormatBC6H_RGBUfloat:
    case MTL::PixelFormatBC7_RGBAUnorm:
    case MTL::PixelFormatBC7_RGBAUnorm_sRGB:
    case MTL::PixelFormatEAC_R11Unorm:
    case MTL::PixelFormatEAC_R11Snorm:
    case MTL::PixelFormatEAC_RG11Unorm:
    case MTL::PixelFormatEAC_RG11Snorm:
    case MTL::PixelFormatEAC_RGBA8:
    case MTL::PixelFormatEAC_RGBA8_sRGB:
    case MTL::PixelFormatETC2_RGB8:
    case MTL::PixelFormatETC2_RGB8_sRGB:
    case MTL::PixelFormatETC2_RGB8A1:
    case MTL::PixelFormatETC2_RGB8A1_sRGB:
    case MTL::PixelFormatASTC_4x4_LDR:
    case MTL::PixelFormatASTC_4x4_sRGB:
    case MTL::PixelFormatASTC_4x4_HDR:
        block_width = 4;
        block_height = 4;
        break;
    case MTL::PixelFormatASTC_5x4_LDR:
    case MTL::PixelFormatASTC_5x4_sRGB:
    case MTL::PixelFormatASTC_5x4_HDR:
        block_width = 5;
        block_height = 4;
        break;
    case MTL::PixelFormatASTC_5x5_LDR:
    case MTL::PixelFormatASTC_5x5_sRGB:
    case MTL::PixelFormatASTC_5x5_HDR:
        block_width = 5;
        block_height = 5;
        break;
    case MTL::PixelFormatASTC_6x5_LDR:
    case MTL::PixelFormatASTC_6x5_sRGB:
    case MTL::PixelFormatASTC_6x5_HDR:
        block_width = 6;
        block_height = 5;
        break;
    case MTL::PixelFormatASTC_6x6_LDR:
    case MTL::PixelFormatASTC_6x6_sRGB:
    case MTL::PixelFormatASTC_6x6_HDR:
        block_width = 6;
        block_height = 6;
        break;
    case MTL::PixelFormatASTC_8x5_LDR:
    case MTL::PixelFormatASTC_8x5_sRGB:
    case MTL::PixelFormatASTC_8x5_HDR:
        block_width = 8;
        block_height = 5;
        break;
    case MTL::PixelFormatASTC_8x6_LDR:
    case MTL::PixelFormatASTC_8x6_sRGB:
    case MTL::PixelFormatASTC_8x6_HDR:
        block_width = 8;
        block_height = 6;
        break;
    case MTL::PixelFormatASTC_8x8_LDR:
    case MTL::PixelFormatASTC_8x8_sRGB:
    case MTL::PixelFormatASTC_8x8_HDR:
        block_width = 8;
        block_height = 8;
        break;
    case MTL::PixelFormatASTC_10x5_LDR:
    case MTL::PixelFormatASTC_10x5_sRGB:
    case MTL::PixelFormatASTC_10x5_HDR:
        block_width = 10;
        block_height = 5;
        break;
    case MTL::PixelFormatASTC_10x6_LDR:
    case MTL::PixelFormatASTC_10x6_sRGB:
    case MTL::PixelFormatASTC_10x6_HDR:
        block_width = 10;
        block_height = 6;
        break;
    case MTL::PixelFormatASTC_10x8_LDR:
    case MTL::PixelFormatASTC_10x8_sRGB:
    case MTL::PixelFormatASTC_10x8_HDR:
        block_width = 10;
        block_height = 8;
        break;
    case MTL::PixelFormatASTC_10x10_LDR:
    case MTL::PixelFormatASTC_10x10_sRGB:
    case MTL::PixelFormatASTC_10x10_HDR:
        block_width = 10;
        block_height = 10;
        break;
    case MTL::PixelFormatASTC_12x10_LDR:
    case MTL::PixelFormatASTC_12x10_sRGB:
    case MTL::PixelFormatASTC_12x10_HDR:
        block_width = 12;
        block_height = 10;
        break;
    case MTL::PixelFormatASTC_12x12_LDR:
    case MTL::PixelFormatASTC_12x12_sRGB:
    case MTL::PixelFormatASTC_12x12_HDR:
        block_width = 12;
        block_height = 12;
        break;
    default:
        return size;
    }

    return MTL::Size::Make(Common::AlignUp<NS::UInteger>(size.width, block_width),
                           Common::AlignUp<NS::UInteger>(size.height, block_height), size.depth);
}

// Whether Metal's validation layer is running. It aborts on things we do on
// purpose, so a couple of places relax what they do to keep the tool usable.
// Each one logs when it does.
[[nodiscard]] bool ValidationLayerActive() {
    static const bool active = [] {
        const char* const debug_layer = std::getenv("MTL_DEBUG_LAYER");
        const char* const shader_validation = std::getenv("MTL_SHADER_VALIDATION");
        const auto set = [](const char* value) { return value != nullptr && value[0] == '1'; };
        return set(debug_layer) || set(shader_validation);
    }();
    return active;
}

} // Anonymous namespace


using VideoCommon::ImageInfo;
using VideoCommon::SubresourceRange;
using VideoCore::Surface::IsPixelFormatASTC;
using VideoCore::Surface::SurfaceType;

namespace {

MTL::TextureType ToTextureType(const ImageInfo& info) {
    switch (info.type) {
    // Allocated as 2D with height 1 to match texture_1D_as_2D in the shader
    // translation. A texture1d can't carry mips, and a 1D view over a 2D base
    // isn't a legal Metal view class.
    case VideoCommon::ImageType::e1D:
        return info.resources.layers > 1 ? MTL::TextureType2DArray : MTL::TextureType2D;
    case VideoCommon::ImageType::e2D:
        if (info.num_samples > 1) {
            return info.resources.layers > 1 ? MTL::TextureType2DMultisampleArray
                                             : MTL::TextureType2DMultisample;
        }
        return info.resources.layers > 1 ? MTL::TextureType2DArray : MTL::TextureType2D;
    case VideoCommon::ImageType::e3D:
        return MTL::TextureType3D;
    case VideoCommon::ImageType::Linear:
        return MTL::TextureType2D;
    case VideoCommon::ImageType::Buffer:
        return MTL::TextureTypeTextureBuffer;
    }
    return MTL::TextureType2D;
}

MTL::TextureType ToTextureType(Shader::TextureType type) {
    switch (type) {
    // 1D is declared as 2D in the generated MSL (texture_1D_as_2D), MSL's
    // texture1d has no mipmaps and no LOD selection. The shader's declared type
    // and the view we hand it have to agree.
    case Shader::TextureType::Color1D:
        return MTL::TextureType2D;
    case Shader::TextureType::ColorArray1D:
        return MTL::TextureType2DArray;
    case Shader::TextureType::Color2D:
    case Shader::TextureType::Color2DRect:
        return MTL::TextureType2D;
    case Shader::TextureType::ColorArray2D:
        return MTL::TextureType2DArray;
    case Shader::TextureType::Color3D:
        return MTL::TextureType3D;
    case Shader::TextureType::ColorCube:
        return MTL::TextureTypeCube;
    case Shader::TextureType::ColorArrayCube:
        return MTL::TextureTypeCubeArray;
    case Shader::TextureType::Buffer:
        return MTL::TextureTypeTextureBuffer;
    }
    return MTL::TextureType2D;
}

MTL::TextureSwizzle ToSwizzle(Tegra::Texture::SwizzleSource source) {
    using Tegra::Texture::SwizzleSource;
    switch (source) {
    case SwizzleSource::Zero:
        return MTL::TextureSwizzleZero;
    case SwizzleSource::R:
        return MTL::TextureSwizzleRed;
    case SwizzleSource::G:
        return MTL::TextureSwizzleGreen;
    case SwizzleSource::B:
        return MTL::TextureSwizzleBlue;
    case SwizzleSource::A:
        return MTL::TextureSwizzleAlpha;
    case SwizzleSource::OneInt:
    case SwizzleSource::OneFloat:
        return MTL::TextureSwizzleOne;
    }
    return MTL::TextureSwizzleZero;
}

// Component mapping for a shader-visible view. That's the guest's own swizzle
// from the texture header plus the undo for whatever channels the format
// substitution moved around.
MTL::TextureSwizzleChannels MakeSwizzle(const VideoCommon::ImageViewInfo& info,
                                        SwizzleFix fix) {
    using Tegra::Texture::SwizzleSource;
    std::array<SwizzleSource, 4> sources{SwizzleSource::R, SwizzleSource::G, SwizzleSource::B,
                                         SwizzleSource::A};
    // A render target's view has no guest swizzle to honour. ImageViewInfo puts
    // the sentinel 0xff in all four channels, which isn't a SwizzleSource and
    // would map to Zero, so the target would sample as (0,0,0,0).
    if (!info.IsRenderTarget()) {
        sources = info.Swizzle();

        // A depth/stencil image has one component, but Maxwell names depth R or
        // G depending on the format, so a guest reading the depth of a packed
        // format asks for GREEN, which a single-component host image doesn't
        // have. Fold it onto red like Vulkan's ConvertGreenRed does.
        if (VideoCore::Surface::GetFormatType(info.format) !=
            VideoCore::Surface::SurfaceType::ColorTexture) {
            bool folded = false;
            for (SwizzleSource& source : sources) {
                if (source == SwizzleSource::G) {
                    source = SwizzleSource::R;
                    folded = true;
                }
            }
            if (folded) {
                static std::atomic<u32> reported{0};
                if (reported++ < 8) {
                    LOG_WARNING(Render_Metal,
                                "Depth/stencil view asked for green; folding onto red (fmt {})",
                                info.format);
                }
            }
        }
    }
    // Undo whatever the format substitution moved. A red/blue flip isn't enough,
    // A5B5G5R1 against Metal's A1BGR5 swaps A with R and G with B, and A4B4G4R4
    // against ABGR4 is a full reversal.
    switch (fix) {
    case SwizzleFix::None:
        break;
    case SwizzleFix::SwapRedBlue:
        for (SwizzleSource& source : sources) {
            if (source == SwizzleSource::R) {
                source = SwizzleSource::B;
            } else if (source == SwizzleSource::B) {
                source = SwizzleSource::R;
            }
        }
        break;
    case SwizzleFix::SwapGreenRed:
        for (SwizzleSource& source : sources) {
            if (source == SwizzleSource::R) {
                source = SwizzleSource::G;
            } else if (source == SwizzleSource::G) {
                source = SwizzleSource::R;
            }
        }
        break;
    case SwizzleFix::SwapSpecial:
        for (SwizzleSource& source : sources) {
            switch (source) {
            case SwizzleSource::A:
                source = SwizzleSource::R;
                break;
            case SwizzleSource::R:
                source = SwizzleSource::A;
                break;
            case SwizzleSource::G:
                source = SwizzleSource::B;
                break;
            case SwizzleSource::B:
                source = SwizzleSource::G;
                break;
            default:
                break;
            }
        }
        break;
    case SwizzleFix::Reverse:
        std::ranges::reverse(sources);
        break;
    }
    return MTL::TextureSwizzleChannels{ToSwizzle(sources[0]), ToSwizzle(sources[1]),
                                       ToSwizzle(sources[2]), ToSwizzle(sources[3])};
}

// The Metal format a shader-visible view should sample through. An image and a
// view over it don't have to agree, the shared cache reuses one image for any
// view in the same view class, so a surface rendered as RGBA can later be
// sampled through a BGRA header and the image's format would swap red and blue.
// Reinterpreting only works between formats of the same texel size, anything
// else falls back to the image's format.
[[nodiscard]] MTL::PixelFormat ViewSampleFormat(const FormatInfo& view_format,
                                                PixelFormat view_pixel_format,
                                                PixelFormat image_pixel_format,
                                                MTL::PixelFormat image_format) {
    using VideoCore::Surface::BytesPerBlock;
    using VideoCore::Surface::DefaultBlockHeight;
    using VideoCore::Surface::DefaultBlockWidth;

    if (view_pixel_format == image_pixel_format ||
        view_format.support == FormatSupport::Unsupported) {
        return image_format;
    }
    // An image whose texels got widened on upload isn't the format the guest
    // named, so the size test below would compare the wrong thing and the view
    // would read every texel at the wrong stride.
    if (VideoCommon::IsPixelFormatExpanded(image_pixel_format) ||
        VideoCommon::IsPixelFormatExpanded(view_pixel_format)) {
        return image_format;
    }
    if (BytesPerBlock(view_pixel_format) != BytesPerBlock(image_pixel_format) ||
        DefaultBlockWidth(view_pixel_format) != DefaultBlockWidth(image_pixel_format) ||
        DefaultBlockHeight(view_pixel_format) != DefaultBlockHeight(image_pixel_format)) {
        return image_format;
    }
    static std::atomic<u32> reported{0};
    if (reported++ < 16) {
        LOG_INFO(Render_Metal, "Sampling image format {} through view format {}",
                 image_pixel_format, view_pixel_format);
    }
    return view_format.format;
}

// Guest pixel format for a shader's declared storage-image format.
[[nodiscard]] PixelFormat StorageFormatOf(Shader::ImageFormat format) {
    switch (format) {
    case Shader::ImageFormat::R8_UINT:
        return PixelFormat::R8_UINT;
    case Shader::ImageFormat::R8_SINT:
        return PixelFormat::R8_SINT;
    case Shader::ImageFormat::R16_UINT:
        return PixelFormat::R16_UINT;
    case Shader::ImageFormat::R16_SINT:
        return PixelFormat::R16_SINT;
    case Shader::ImageFormat::R32_UINT:
        return PixelFormat::R32_UINT;
    case Shader::ImageFormat::R32G32_UINT:
        return PixelFormat::R32G32_UINT;
    case Shader::ImageFormat::R32G32B32A32_UINT:
        return PixelFormat::R32G32B32A32_UINT;
    case Shader::ImageFormat::Typeless:
        break;
    }
    return PixelFormat::Invalid;
}

[[nodiscard]] bool IsIdentitySwizzle(const MTL::TextureSwizzleChannels& swizzle) {
    return swizzle.red == MTL::TextureSwizzleRed && swizzle.green == MTL::TextureSwizzleGreen &&
           swizzle.blue == MTL::TextureSwizzleBlue && swizzle.alpha == MTL::TextureSwizzleAlpha;
}

// Metal's texture view classes. A view can only reinterpret a texture as another
// type in the same class, zero means it can't be viewed at all.
constexpr int ViewClass(MTL::TextureType type) {
    switch (type) {
    case MTL::TextureType1D:
    case MTL::TextureType1DArray:
        return 1;
    case MTL::TextureType2D:
    case MTL::TextureType2DArray:
    case MTL::TextureTypeCube:
    case MTL::TextureTypeCubeArray:
        return 2;
    case MTL::TextureType2DMultisample:
    case MTL::TextureType2DMultisampleArray:
        return 3;
    case MTL::TextureType3D:
        return 4;
    case MTL::TextureTypeTextureBuffer:
        return 5;
    default:
        return 0;
    }
}

// The guest's border colour rounded to the three Metal gives us (transparent
// black, opaque black, opaque white). Thresholds are the same as the Vulkan
// fallback for when VK_EXT_custom_border_color isn't there.
MTL::SamplerBorderColor BorderColorFor(const std::array<f32, 4>& color) {
    if (color == std::array<f32, 4>{0.0f, 0.0f, 0.0f, 0.0f}) {
        return MTL::SamplerBorderColorTransparentBlack;
    }
    if (color == std::array<f32, 4>{0.0f, 0.0f, 0.0f, 1.0f}) {
        return MTL::SamplerBorderColorOpaqueBlack;
    }
    if (color == std::array<f32, 4>{1.0f, 1.0f, 1.0f, 1.0f}) {
        return MTL::SamplerBorderColorOpaqueWhite;
    }
    if (color[0] + color[1] + color[2] > 1.35f) {
        return MTL::SamplerBorderColorOpaqueWhite;
    }
    return color[3] > 0.5f ? MTL::SamplerBorderColorOpaqueBlack
                           : MTL::SamplerBorderColorTransparentBlack;
}

// Everything a guest image might get used for. Metal wants usage declared up
// front and getting it wrong is a validation failure, not a slow path, so we're
// permissive here. It only costs us some lossless-compression opportunities.
MTL::TextureUsage UsageFor(const ImageInfo& info, const FormatInfo& format_info) {
    MTL::TextureUsage usage = MTL::TextureUsageShaderRead;

    if (format_info.attachable) {
        usage |= MTL::TextureUsageRenderTarget;
    }
    if (format_info.storage) {
        usage |= MTL::TextureUsageShaderWrite;
    }
    // Image atomics compile to native texture atomics, which Metal allows on 32-bit
    // integer textures only.
    using VideoCore::Surface::PixelFormat;
    if (info.format == PixelFormat::R32_UINT || info.format == PixelFormat::R32_SINT) {
        usage |= MTL::TextureUsageShaderAtomic;
    }
    // Views with a different pixel format need this declared at creation time.
    usage |= MTL::TextureUsagePixelFormatView;

    return usage;
}

// Whether a format carries depth and stencil in one texture. A blit treats the
// two aspects as separate planes and rejects a buffer copy that doesn't pick
// one, MTLBlitOptionNone asserts in validation and is UB without it.
bool IsCombinedDepthStencil(MTL::PixelFormat format) {
    switch (format) {
    case MTL::PixelFormatDepth32Float_Stencil8:
    case MTL::PixelFormatDepth24Unorm_Stencil8:
    case MTL::PixelFormatX32_Stencil8:
    case MTL::PixelFormatX24_Stencil8:
        return true;
    default:
        return false;
    }
}

MTL::Origin ToOrigin(const VideoCommon::Offset3D& offset) {
    return MTL::Origin::Make(static_cast<NS::UInteger>(offset.x),
                             static_cast<NS::UInteger>(offset.y),
                             static_cast<NS::UInteger>(offset.z));
}

MTL::Size ToSize(const VideoCommon::Extent3D& extent) {
    return MTL::Size::Make(static_cast<NS::UInteger>(extent.width),
                           static_cast<NS::UInteger>(extent.height),
                           static_cast<NS::UInteger>(extent.depth));
}

} // Anonymous namespace

// ---------------------------------------------------------------------------
// TextureCacheRuntime
// ---------------------------------------------------------------------------

TextureCacheRuntime::TextureCacheRuntime(const Device& device_, Scheduler& scheduler_,
                                         MemoryAllocator& memory_allocator_,
                                         StagingBufferPool& staging_buffer_pool_,
                                         BlitImageHelper& blit_image_helper_)
    : device{device_}, scheduler{scheduler_}, memory_allocator{memory_allocator_},
      staging_buffer_pool{staging_buffer_pool_}, blit_image_helper{blit_image_helper_},
      resolution{Settings::values.resolution_info} {}

void TextureCacheRuntime::Finish() {
    scheduler.Finish();
}

void TextureCacheRuntime::TickFrame() {
    ++frame_index;
}

StagingBufferRef TextureCacheRuntime::UploadStagingBuffer(size_t size, bool deferred) {
    return staging_buffer_pool.Request(size, MemoryUsage::Upload, deferred);
}

StagingBufferRef TextureCacheRuntime::DownloadStagingBuffer(size_t size, bool deferred) {
    return staging_buffer_pool.Request(size, MemoryUsage::Download, deferred);
}

void TextureCacheRuntime::FreeDeferredStagingBuffer(StagingBufferRef& ref) {
    staging_buffer_pool.FreeDeferred(ref);
}

u64 TextureCacheRuntime::GetDeviceLocalMemory() const {
    return device.GetRecommendedWorkingSetSize();
}

u64 TextureCacheRuntime::GetDeviceMemoryUsage() const {
    return device.GetDevice()->currentAllocatedSize();
}

bool TextureCacheRuntime::CanReportMemoryUsage() const {
    // False on purpose. Say true and the shared cache swaps its own image
    // accounting for the device-wide currentAllocatedSize(), which its collector
    // can't bring down by evicting images, so it evicts every frame.
    return false;
}

std::optional<size_t> TextureCacheRuntime::GetSamplerHeapBudget() const {
    // Metal imposes no global sampler-object limit worth surfacing here.
    return std::nullopt;
}

MTL::Buffer* TextureCacheRuntime::GetTemporaryBuffer(size_t needed_size) {
    const size_t log2 = static_cast<size_t>(std::bit_width(needed_size - 1));
    if (log2 >= INDEXING_SLOTS) {
        return nullptr;
    }
    if (temporary_buffers[log2]) {
        return temporary_buffers[log2].get();
    }

    const size_t size = size_t{1} << log2;
    temporary_buffers[log2] = memory_allocator.CreateBuffer(size, MemoryUsage::DeviceLocal);
    if (temporary_buffers[log2]) {
        temporary_buffers[log2]->setLabel(ToNSString("eden temp texture buffer"));
    }
    return temporary_buffers[log2].get();
}

bool TextureCacheRuntime::IsFormatDitherable(PixelFormat format) {
    switch (format) {
    case PixelFormat::A8B8G8R8_UNORM:
    case PixelFormat::B8G8R8A8_UNORM:
    case PixelFormat::A8B8G8R8_SRGB:
    case PixelFormat::B8G8R8A8_SRGB:
        return true;
    default:
        return false;
    }
}

bool TextureCacheRuntime::IsFormatScalable(PixelFormat format) {
    // Colour is limited to the four formats Vulkan allows, not everything
    // attachable. Rescaling round-trips through a bilinear BlitColor and
    // interpolating between texels is undefined for integer, packed-float and
    // single-channel targets.
    //
    // Depth has to be allowed too and doesn't go through that filter, BlitScale
    // sends it through the depth-writing Convert path. Refusing depth while
    // taking colour leaves a pass with Nx colour and 1x depth, which then
    // collapses to the smallest attachment.
    const SurfaceType type = VideoCore::Surface::GetFormatType(format);
    const bool is_depth = type == SurfaceType::Depth || type == SurfaceType::DepthStencil;
    if (!is_depth) {
        switch (format) {
        case PixelFormat::B8G8R8A8_UNORM:
        case PixelFormat::A8B8G8R8_UNORM:
        case PixelFormat::R16G16B16A16_FLOAT:
        case PixelFormat::R32G32B32A32_FLOAT:
            break;
        default:
            return false;
        }
    } else if (type == SurfaceType::DepthStencil) {
        // Known cost. Metal has no stencil export so the Convert path writes
        // depth only, and a rescaled packed image keeps the destination's old
        // stencil. Still better than clamping every scaled pass to 1x depth.
        static std::atomic<bool> reported{false};
        if (!reported.exchange(true)) {
            LOG_WARNING(Render_Metal,
                        "Rescaling a packed depth/stencil image; stencil contents are not "
                        "carried across the rescale");
        }
    }

    const FormatInfo info = SurfaceFormat(device, format);
    return info.attachable && info.support != FormatSupport::Unsupported;
}

MTL::Texture* TextureCacheRuntime::GetScratchAttachment(MTL::PixelFormat format, u32 width,
                                                        u32 height, u32 samples) {
    const u64 key = (static_cast<u64>(format) << 40) ^ (static_cast<u64>(samples) << 56) ^
                    (static_cast<u64>(width) << 20) ^ height;
    const auto it = scratch_attachments.find(key);
    if (it != scratch_attachments.end()) {
        return it->second.get();
    }

    ScopedAutoreleasePool pool;
    MTL::TextureDescriptor* descriptor = MTL::TextureDescriptor::alloc()->init();
    descriptor->setTextureType(samples > 1 ? MTL::TextureType2DMultisample : MTL::TextureType2D);
    descriptor->setPixelFormat(format);
    descriptor->setWidth(width);
    descriptor->setHeight(height);
    descriptor->setSampleCount(samples);
    descriptor->setMipmapLevelCount(1);
    descriptor->setUsage(MTL::TextureUsageRenderTarget);
    Owned<MTL::Texture> texture =
        memory_allocator.CreateTexture(descriptor, MemoryUsage::Transient);
    descriptor->release();
    if (!texture) {
        return nullptr;
    }
    texture->setLabel(ToNSString("eden scratch attachment"));
    return scratch_attachments.emplace(key, std::move(texture)).first->second.get();
}

void TextureCacheRuntime::BarrierFeedbackLoop() {
    scheduler.RequestFeedbackBarrier();
}

void TextureCacheRuntime::CopyImage(Image& dst, Image& src,
                                    std::span<const VideoCommon::ImageCopy> copies) {
    if (!dst.IsValid() || !src.IsValid()) {
        return;
    }

    // Formats of different texel size can't be copied texture-to-texture.
    // ShouldReinterpret answers this, but the shared cache only asks when the
    // surface TYPES differ, so two colour formats of different widths land here
    // and copyFromTexture between them is undefined.
    if (VideoCore::Surface::BytesPerBlock(dst.info.format) !=
        VideoCore::Surface::BytesPerBlock(src.info.format)) {
        static std::atomic<u32> reported{0};
        if (reported++ == 0) {
            LOG_DEBUG(Render_Metal, "Copying {} to {} through a reinterpret; texel sizes differ",
                      static_cast<u32>(src.info.format), static_cast<u32>(dst.info.format));
        }
        ReinterpretImage(dst, src, copies);
        return;
    }

    boost::container::small_vector<VideoCommon::ImageCopy, 8> owned(copies.begin(), copies.end());
    MTL::Texture* dst_texture = dst.Handle();
    MTL::Texture* src_texture = src.Handle();

    // Ends the pass on the recording side too. RequestBlit closes the render
    // encoder on the worker, but without this the recording thread's mirror
    // still thinks a pass is open and every later check of that is wrong.
    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([dst_texture, src_texture,
                      copies = std::move(owned)](CommandContext& context) {
        BlitEncoder encoder = context.RequestBlit();
        if (encoder == nullptr) {
            return;
        }
        // An array's slices live in num_layers, a 3D image's in extent.depth,
        // and the two are never both set. The shared cache fills in whichever
        // the image uses and leaves the other at 1. Mix them up and you either
        // drop every layer past the first or issue an illegal copy.
        const bool is_volume = src_texture->textureType() == MTL::TextureType3D ||
                               dst_texture->textureType() == MTL::TextureType3D;
        for (const VideoCommon::ImageCopy& copy : copies) {
            if (is_volume) {
                encoder->copyFromTexture(
                    src_texture, copy.src_subresource.base_layer,
                    copy.src_subresource.base_level, ToOrigin(copy.src_offset),
                    AlignCopySizeToBlock(ToSize(copy.extent), src_texture->pixelFormat()),
                    dst_texture, copy.dst_subresource.base_layer,
                    copy.dst_subresource.base_level, ToOrigin(copy.dst_offset));
                continue;
            }
            const MTL::Size slice_size = AlignCopySizeToBlock(
                MTL::Size::Make(copy.extent.width, copy.extent.height, 1),
                src_texture->pixelFormat());
            const u32 num_layers =
                (std::max)(1u, static_cast<u32>(copy.src_subresource.num_layers));
            for (u32 layer = 0; layer < num_layers; ++layer) {
                encoder->copyFromTexture(
                    src_texture, copy.src_subresource.base_layer + layer,
                    copy.src_subresource.base_level, ToOrigin(copy.src_offset), slice_size,
                    dst_texture, copy.dst_subresource.base_layer + layer,
                    copy.dst_subresource.base_level, ToOrigin(copy.dst_offset));
            }
        }
    });
}

void TextureCacheRuntime::CopyImageMSAA(Image& dst, Image& src,
                                        std::span<const VideoCommon::ImageCopy> copies) {
    MTL::Texture* const src_texture = src.Handle();
    MTL::Texture* const dst_texture = dst.Handle();
    if (src_texture == nullptr || dst_texture == nullptr) {
        return;
    }

    // A blit encoder can't change sample count. Resolving many samples to one is
    // a render pass store action, so we open a pass that loads and resolves and
    // draw nothing. The other direction has no fixed-function form.
    if (src.Samples() > 1 && dst.Samples() == 1) {
        // A store-action resolve always covers the WHOLE attachment, so we can
        // only write straight into the destination if the guest asked for the
        // whole image. Anything else needs a scratch target and a region copy.
        const bool whole_image =
            copies.empty() ||
            (copies.size() == 1 && copies[0].src_offset.x == 0 && copies[0].src_offset.y == 0 &&
             copies[0].dst_offset.x == 0 && copies[0].dst_offset.y == 0 &&
             copies[0].src_subresource.base_level == 0 &&
             copies[0].dst_subresource.base_level == 0 &&
             copies[0].extent.width == static_cast<u32>(src_texture->width()) &&
             copies[0].extent.height == static_cast<u32>(src_texture->height()));

        RenderPassState pass{};
        pass.num_color = 1;
        pass.color[0].texture = src_texture;
        pass.color[0].load_action = LoadAction::Load;
        pass.color[0].store_action = StoreAction::Resolve;
        pass.render_area_width = static_cast<u32>(src_texture->width());
        pass.render_area_height = static_cast<u32>(src_texture->height());
        pass.sample_count = src.Samples();

        Owned<MTL::Texture> scratch;
        if (whole_image) {
            pass.color[0].resolve_texture = dst_texture;
        } else {
            ScopedAutoreleasePool pool;
            MTL::TextureDescriptor* descriptor = MTL::TextureDescriptor::alloc()->init();
            descriptor->setTextureType(MTL::TextureType2D);
            descriptor->setPixelFormat(src_texture->pixelFormat());
            descriptor->setWidth(src_texture->width());
            descriptor->setHeight(src_texture->height());
            descriptor->setMipmapLevelCount(1);
            descriptor->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
            scratch = memory_allocator.CreateTexture(descriptor, MemoryUsage::DeviceLocal);
            descriptor->release();
            if (!scratch) {
                // Don't fall back to the whole-image resolve, it would clobber
                // the regions the guest didn't name.
                LOG_WARNING(Render_Metal, "No scratch target to resolve {} MSAA region(s)",
                            copies.size());
                return;
            }
            scratch->setLabel(ToNSString("eden MSAA region resolve"));
            pass.color[0].resolve_texture = scratch.get();
        }

        scheduler.RequestRenderPass(pass);
        // Closed right away, the resolve is the store action and not a draw.
        scheduler.RequestOutsideRenderPassOperationContext();

        static std::atomic<u32> resolved{0};
        if (resolved++ == 0) {
            LOG_INFO(Render_Metal, "Resolving multisampled images through a store action");
        }

        if (whole_image) {
            return;
        }

        // Now move only what was asked for. The resolved copy is single-sample
        // and single-layer, so every region reads slice 0 level 0 at the source
        // offset the guest named.
        MTL::Texture* const keep = scratch.get();
        boost::container::small_vector<VideoCommon::ImageCopy, 8> owned(copies.begin(),
                                                                       copies.end());
        // Named as an allocation too so the sweep drops it from the residency
        // set as well as releasing it. Those entries are wired, they can't be
        // compressed or evicted, so a stale one costs real physical memory.
        // KeepAlive holds it until the GPU is past this tick, which covers the
        // command recorded below.
        scheduler.KeepAlive(keep, keep);
        scheduler.Record([dst_texture, keep, copies = std::move(owned)](CommandContext& context) {
            BlitEncoder encoder = context.RequestBlit();
            if (encoder == nullptr) {
                return;
            }
            for (const VideoCommon::ImageCopy& copy : copies) {
                const MTL::Size size = AlignCopySizeToBlock(
                    MTL::Size::Make(copy.extent.width, copy.extent.height, 1),
                    keep->pixelFormat());
                encoder->copyFromTexture(keep, 0, 0, ToOrigin(copy.src_offset), size, dst_texture,
                                         copy.dst_subresource.base_layer,
                                         copy.dst_subresource.base_level,
                                         ToOrigin(copy.dst_offset));
            }
        });
        return;
    }

    if (src.Samples() == dst.Samples()) {
        // Same sample count after all, an ordinary copy does it.
        CopyImage(dst, src, copies);
        return;
    }

    if (src.Samples() == 1 && dst.Samples() > 1) {
        // A blit can't write into a multisampled texture, so the expand is a
        // draw whose per-sample invocations all read the same source texel.
        // Only a whole-image request can use the discard-and-draw-once form,
        // regions have to be expanded one by one or the rest gets discarded.
        const bool whole_image =
            copies.empty() ||
            (copies.size() == 1 && copies[0].src_offset.x == 0 && copies[0].src_offset.y == 0 &&
             copies[0].dst_offset.x == 0 && copies[0].dst_offset.y == 0 &&
             copies[0].extent.width == static_cast<u32>(dst_texture->width()) &&
             copies[0].extent.height == static_cast<u32>(dst_texture->height()));

        if (whole_image) {
            blit_image_helper.ExpandToMultisample(dst_texture, src_texture);
        } else {
            boost::container::small_vector<BlitImageHelper::ExpandRegion, 8> regions;
            regions.reserve(copies.size());
            for (const VideoCommon::ImageCopy& copy : copies) {
                regions.push_back(BlitImageHelper::ExpandRegion{
                    static_cast<u32>(copy.src_offset.x), static_cast<u32>(copy.src_offset.y),
                    static_cast<u32>(copy.dst_offset.x), static_cast<u32>(copy.dst_offset.y),
                    copy.extent.width, copy.extent.height});
            }
            blit_image_helper.ExpandToMultisample(dst_texture, src_texture, regions);
        }
        static std::atomic<u32> expanded{0};
        if (expanded++ == 0) {
            LOG_INFO(Render_Metal, "Expanding single-sample images into multisampled ones");
        }
        return;
    }

    static std::atomic<u32> reported{0};
    if (reported++ < 4) {
        LOG_WARNING(Render_Metal, "Cannot spread {} sample(s) across {}", src.Samples(),
                    dst.Samples());
    }
}

bool TextureCacheRuntime::ShouldReinterpret(Image& dst, Image& src) {
    // Metal texture views can reinterpret between formats of the same bit width
    // if the texture was created with PixelFormatView usage, which ours are.
    // Anything crossing bit widths needs a real conversion.
    if (VideoCore::Surface::BytesPerBlock(dst.info.format) !=
        VideoCore::Surface::BytesPerBlock(src.info.format)) {
        return true;
    }

    // Depth-format copies stay on the blit path on purpose, we tried sending
    // them through ReinterpretImage and reverted it.
    return false;
}

void TextureCacheRuntime::ReinterpretImage(Image& dst, Image& src,
                                           std::span<const VideoCommon::ImageCopy> copies) {
    // Reinterpreting across bit widths needs no shader, we push the bytes out
    // through a buffer and read them back with the destination's layout. The
    // source rect scales by the ratio of the two formats' bytes per texel.
    MTL::Texture* const src_texture = src.Handle();
    MTL::Texture* const dst_texture = dst.Handle();
    if (src_texture == nullptr || dst_texture == nullptr || copies.empty()) {
        return;
    }

    const u32 src_block_width = VideoCore::Surface::DefaultBlockWidth(src.info.format);
    const u32 dst_block_width = VideoCore::Surface::DefaultBlockWidth(dst.info.format);
    const u32 dst_block_height = VideoCore::Surface::DefaultBlockHeight(dst.info.format);
    const u32 src_bytes_per_block = VideoCore::Surface::BytesPerBlock(src.info.format);
    const u32 dst_bytes_per_block = VideoCore::Surface::BytesPerBlock(dst.info.format);
    const u32 bpp_in = src_bytes_per_block / (std::max)(src_block_width, 1u);
    const u32 bpp_out = dst_bytes_per_block / (std::max)(dst_block_width, 1u);
    if (bpp_in == 0 || bpp_out == 0) {
        return;
    }

    // One staging region per copy, laid out back to back in the destination's
    // own block layout.
    struct Step {
        VideoCommon::ImageCopy copy;
        u64 offset;
        u32 bytes_per_row;
        u32 bytes_per_image;
        u32 src_x;
        u32 src_width;
    };
    boost::container::small_vector<Step, 8> steps;
    u64 total = 0;
    for (const VideoCommon::ImageCopy& copy : copies) {
        const u32 blocks_across =
            (copy.extent.width + dst_block_width - 1) / dst_block_width;
        const u32 blocks_down = (copy.extent.height + dst_block_height - 1) / dst_block_height;
        const u32 bytes_per_row = blocks_across * dst_bytes_per_block;
        const u32 bytes_per_image = bytes_per_row * blocks_down;
        steps.push_back(Step{
            .copy = copy,
            .offset = total,
            .bytes_per_row = bytes_per_row,
            .bytes_per_image = bytes_per_image,
            .src_x = (bpp_out * static_cast<u32>(copy.src_offset.x)) / bpp_in,
            .src_width = (bpp_out * copy.extent.width) / bpp_in,
        });
        total += static_cast<u64>(bytes_per_image) *
                 static_cast<u64>((std::max)(static_cast<s32>(copy.extent.depth) *
                                                 copy.src_subresource.num_layers,
                                             1));
    }
    if (total == 0) {
        return;
    }

    const StagingBufferRef ref = staging_buffer_pool.Request(total, MemoryUsage::DeviceLocal);
    if (ref.buffer == nullptr) {
        LOG_WARNING(Render_Metal, "No staging space to reinterpret a {} byte image", total);
        return;
    }

    MTL::Buffer* const buffer = ref.buffer;
    const u64 base = ref.offset;
    // See Image::UploadMemory, a copy touching a combined depth/stencil texture
    // has to name the aspect it means, in both directions.
    const MTL::BlitOption src_options = IsCombinedDepthStencil(src_texture->pixelFormat())
                                            ? MTL::BlitOptionDepthFromDepthStencil
                                            : MTL::BlitOptionNone;
    const MTL::BlitOption dst_options = IsCombinedDepthStencil(dst_texture->pixelFormat())
                                            ? MTL::BlitOptionDepthFromDepthStencil
                                            : MTL::BlitOptionNone;
    // Ends the pass on the recording side too. RequestBlit closes the render
    // encoder on the worker, but without this the recording thread's mirror
    // still thinks a pass is open and every later check of that is wrong.
    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([src_texture, dst_texture, buffer, base, src_options, dst_options,
                      steps = std::move(steps)](CommandContext& context) {
        BlitEncoder encoder = context.RequestBlit();
        if (encoder == nullptr) {
            return;
        }
        // Both halves share one encoder. Every texture-to-buffer copy is
        // recorded before any buffer-to-texture one, with a barrier in between
        // so the reads see the writes.
        for (const Step& step : steps) {
            const auto& copy = step.copy;
            for (s32 layer = 0; layer < copy.src_subresource.num_layers; ++layer) {
                const u64 offset = base + step.offset +
                                   static_cast<u64>(layer) * step.bytes_per_image;
                encoder->copyFromTexture(
                    src_texture, copy.src_subresource.base_layer + layer,
                    copy.src_subresource.base_level,
                    MTL::Origin::Make(step.src_x, copy.src_offset.y, copy.src_offset.z),
                    MTL::Size::Make(step.src_width, copy.extent.height, copy.extent.depth),
                    buffer, offset, step.bytes_per_row, step.bytes_per_image, src_options);
            }
        }
        // The staging buffer is written above and read below. Without this the
        // read-back can see the texture's pre-copy contents.
        encoder->barrierAfterCopies();
        for (const Step& step : steps) {
            const auto& copy = step.copy;
            for (s32 layer = 0; layer < copy.dst_subresource.num_layers; ++layer) {
                const u64 offset = base + step.offset +
                                   static_cast<u64>(layer) * step.bytes_per_image;
                encoder->copyFromBuffer(
                    buffer, offset, step.bytes_per_row, step.bytes_per_image,
                    MTL::Size::Make(copy.extent.width, copy.extent.height, copy.extent.depth),
                    dst_texture, copy.dst_subresource.base_layer + layer,
                    copy.dst_subresource.base_level, ToOrigin(copy.dst_offset), dst_options);
            }
        }
    });
}

void TextureCacheRuntime::ConvertImage(Framebuffer* dst, ImageView& dst_view,
                                       ImageView& src_view) {
    if (dst == nullptr) {
        return;
    }
    const RenderPassState& pass = dst->GetRenderPassState();
    MTL::Texture* const src = src_view.Handle(Shader::TextureType::Color2D);
    if (src == nullptr) {
        return;
    }

    const auto dst_type = VideoCore::Surface::GetFormatType(dst_view.format);
    const auto src_type = VideoCore::Surface::GetFormatType(src_view.format);
    const bool dst_is_color = dst_type == SurfaceType::ColorTexture;
    const bool src_is_color = src_type == SurfaceType::ColorTexture;
    if (dst_is_color == src_is_color) {
        // Same aspect, an ordinary copy already covers it.
        return;
    }

    const VideoCommon::Region2D region{
        .start = {0, 0},
        .end = {static_cast<s32>(pass.render_area_width),
                static_cast<s32>(pass.render_area_height)},
    };
    blit_image_helper.Convert(pass, src,
                              dst_is_color ? BlitImageHelper::ConvertKind::DepthToColor
                                           : BlitImageHelper::ConvertKind::ColorToDepth,
                              region, region, false);

    if (!dst_is_color && dst_view.format == PixelFormat::D24_UNORM_S8_UINT) {
        // We can't write the stencil half of a packed destination, Metal has no
        // stencil export from a fragment shader. Depth gets converted, stencil
        // keeps whatever it held.
        static std::atomic<u32> reported{0};
        if (reported++ < 4) {
            LOG_WARNING(Render_Metal,
                        "Converted depth into a packed depth-stencil target; stencil is kept");
        }
    }
}

namespace {

// One level of one layer as a 2D texture. Returns +1 like newTextureView does.
[[nodiscard]] Owned<MTL::Texture> SliceView(MTL::Texture* texture, MTL::PixelFormat format,
                                            u32 level, u32 layer) {
    return Adopt(texture->newTextureView(format, MTL::TextureType2D,
                                         NS::Range::Make(level, 1), NS::Range::Make(layer, 1)));
}

} // Anonymous namespace

void Image::BlitScale(MTL::Texture* src, MTL::Texture* dst) {
    if (src == nullptr || dst == nullptr) {
        return;
    }
    const bool is_color =
        VideoCore::Surface::GetFormatType(info.format) == SurfaceType::ColorTexture;
    const u32 levels = static_cast<u32>(std::min(src->mipmapLevelCount(), dst->mipmapLevelCount()));
    // A cube reports an arrayLength of one while holding six slices, so taking
    // arrayLength at face value copies one face and drops five.
    const auto slices_of = [](MTL::Texture* texture) {
        const MTL::TextureType type = texture->textureType();
        const bool is_cube =
            type == MTL::TextureTypeCube || type == MTL::TextureTypeCubeArray;
        return static_cast<u32>(texture->arrayLength()) * (is_cube ? 6u : 1u);
    };
    const u32 layers = std::min(slices_of(src), slices_of(dst));

    for (u32 level = 0; level < levels; ++level) {
        // From the textures instead of info, that way the two agree at every
        // level without us repeating the mip maths.
        const s32 src_w = static_cast<s32>(std::max<u64>(1, src->width() >> level));
        const s32 src_h = static_cast<s32>(std::max<u64>(1, src->height() >> level));
        const s32 dst_w = static_cast<s32>(std::max<u64>(1, dst->width() >> level));
        const s32 dst_h = static_cast<s32>(std::max<u64>(1, dst->height() >> level));
        const Region2D src_region{.start = {0, 0}, .end = {src_w, src_h}};
        const Region2D dst_region{.start = {0, 0}, .end = {dst_w, dst_h}};

        for (u32 layer = 0; layer < layers; ++layer) {
            Owned<MTL::Texture> src_view = SliceView(src, pixel_format, level, layer);
            Owned<MTL::Texture> dst_view = SliceView(dst, pixel_format, level, layer);
            if (!src_view || !dst_view) {
                continue;
            }
            // Sampled and rendered into respectively, so both have to be
            // registered. A view carries its own gpuResourceID and the parent's
            // residency doesn't cover it. KeepAlive below names them as
            // allocations too, so they leave the set when the submission retires.
            runtime->device.AddResidentAllocation(src_view.get());
            runtime->device.AddResidentAllocation(dst_view.get());
            if (is_color) {
                runtime->blit_image_helper.BlitColor(dst_view.get(), src_view.get(), dst_region,
                                                     src_region,
                                                     Tegra::Engines::Fermi2D::Filter::Bilinear);
            } else {
                // Depth can't be a colour attachment so it goes through the
                // depth-writing conversion instead. Stencil rides along
                // untouched, Metal has no stencil export, so a rescaled packed
                // image keeps the stencil the destination already had.
                RenderPassState pass{};
                pass.depth_stencil.texture = dst_view.get();
                pass.depth_stencil.has_stencil =
                    VideoCore::Surface::GetFormatType(info.format) ==
                    SurfaceType::DepthStencil;
                pass.render_area_width = static_cast<u32>(dst_w);
                pass.render_area_height = static_cast<u32>(dst_h);
                pass.num_layers = 1;
                runtime->blit_image_helper.Convert(pass, src_view.get(),
                                                   BlitImageHelper::ConvertKind::DepthToDepth,
                                                   dst_region, src_region, true);
            }
            // The blits above are recorded, not executed, and a Metal 4 command
            // buffer takes no strong reference to what it touches, so these
            // views have to outlive the submission that reads them.
            runtime->scheduler.KeepAlive(src_view.get(), src_view.get());
            runtime->scheduler.KeepAlive(dst_view.get(), dst_view.get());
        }
    }
}

bool Image::ScaleUp(bool ignore) {
    if (runtime == nullptr || !runtime->resolution.active) {
        return false;
    }
    if (IsRescaled() || !texture) {
        return false;
    }
    if (info.type != VideoCommon::ImageType::e2D) {
        // A 3D image can't be sliced into the 2D views the blits need, Metal
        // won't view across texture classes, and render targets are 2D.
        return false;
    }
    if (samples > 1) {
        // The copy is a draw through a plain sampler, it can't resolve or spread
        // samples. Refusing keeps the whole MSAA framebuffer at 1x.
        return false;
    }
    if (!runtime->IsFormatScalable(info.format)) {
        // The copy is a draw, so the destination has to be renderable.
        return false;
    }

    if (!scaled_texture) {
        // Unexpand the sample count first, then rescale. The other way round
        // scales the sample expansion too.
        const auto [scale_samples_x, scale_samples_y] =
            VideoCommon::SamplesLog2(info.num_samples);
        const u32 base_width = info.size.width >> scale_samples_x;
        const u32 base_height = info.size.height >> scale_samples_y;
        const u32 width = runtime->resolution.ScaleUp(base_width);
        const u32 height = info.type == VideoCommon::ImageType::e2D
                               ? runtime->resolution.ScaleUp(base_height)
                               : base_height;

        ScopedAutoreleasePool pool;
        MTL::TextureDescriptor* descriptor = MTL::TextureDescriptor::alloc()->init();
        descriptor->setTextureType(ToTextureType(info));
        descriptor->setPixelFormat(pixel_format);
        descriptor->setWidth(width);
        descriptor->setHeight(height);
        descriptor->setDepth(info.type == VideoCommon::ImageType::e3D ? info.size.depth : 1);
        descriptor->setMipmapLevelCount(info.resources.levels);
        descriptor->setArrayLength(
            info.type == VideoCommon::ImageType::e3D ? 1 : info.resources.layers);
        descriptor->setSampleCount(samples);
        descriptor->setUsage(texture_usage);
        scaled_texture = runtime->memory_allocator.CreateTexture(descriptor, MemoryUsage::DeviceLocal);
        descriptor->release();

        if (!scaled_texture) {
            LOG_WARNING(Render_Metal, "Could not allocate a {}x{} scaled image", width, height);
            return false;
        }
        // Nothing to skip on a texture that has never held anything.
        ignore = false;
    }

    // Only set once the destination exists. Handle() switches on this flag, so
    // raising it before there's something to switch to hands out a null texture.
    flags |= VideoCommon::ImageFlagBits::Rescaled;
    has_scaled = true;
    if (!ignore) {
        BlitScale(texture.get(), scaled_texture.get());
    }
    return true;
}

bool Image::ScaleDown(bool ignore) {
    if (runtime == nullptr || !IsRescaled()) {
        return false;
    }
    flags &= ~VideoCommon::ImageFlagBits::Rescaled;
    if (!ignore && texture && scaled_texture) {
        BlitScale(scaled_texture.get(), texture.get());
    }
    return true;
}

void TextureCacheRuntime::BlitImage(Framebuffer* dst_framebuffer, ImageView& dst, ImageView& src,
                                    const Region2D& dst_region, const Region2D& src_region,
                                    Tegra::Engines::Fermi2D::Filter filter,
                                    Tegra::Engines::Fermi2D::Operation operation) {
    if (operation != Tegra::Engines::Fermi2D::Operation::SrcCopy &&
        operation != Tegra::Engines::Fermi2D::Operation::SrcCopyAnd) {
        // Blend and ROP operations need the destination read back into the
        // shader, which we don't do, the source is written straight over. Not a
        // bail-out on purpose, Vulkan does the same unblended copy and dropping
        // it loses the whole surface.
        static std::atomic<u32> reported{0};
        if (reported++ < 8) {
            LOG_WARNING(Render_Metal, "Blit operation {} is unblended; Metal copies the source",
                        static_cast<u32>(operation));
        }
    }

    // A depth destination can't be a colour attachment, so it takes the
    // depth-writing path instead of BlitColor. Stencil rides along untouched,
    // Metal has no stencil export.
    const auto surface_type = VideoCore::Surface::GetFormatType(dst.format);
    if (surface_type != SurfaceType::ColorTexture) {
        if (dst_framebuffer == nullptr) {
            return;
        }
        MTL::Texture* const depth_source = src.Handle(Shader::TextureType::Color2D);
        if (depth_source == nullptr) {
            return;
        }
        blit_image_helper.Convert(dst_framebuffer->GetRenderPassState(), depth_source,
                                  BlitImageHelper::ConvertKind::DepthToDepth, dst_region,
                                  src_region,
                                  filter == Tegra::Engines::Fermi2D::Filter::Bilinear);
        return;
    }

    MTL::Texture* const source = src.RenderTarget();
    MTL::Texture* const destination = dst.RenderTarget();
    if (source == nullptr || destination == nullptr) {
        return;
    }

    // BlitColor's shaders declare texture2d<float> and its pipelines target a
    // single-sample attachment, so a multisampled texture on either side is a
    // type the shader can't sample at all.
    Owned<MTL::Texture> resolved_source;
    MTL::Texture* blit_source = source;
    if (src.Samples() > 1) {
        if (dst.Samples() > 1) {
            // Both multisampled. A resolve-then-expand round trip loses the
            // per-sample detail the destination exists to hold, so it's no
            // better than leaving the destination alone.
            static std::atomic<u32> reported{0};
            if (reported++ < 4) {
                LOG_WARNING(Render_Metal, "Skipping a {}x to {}x multisampled blit",
                            src.Samples(), dst.Samples());
            }
            return;
        }
        // Resolve to a single-sample copy and blit from that, same store-action
        // resolve CopyImageMSAA and the MSAA download path use.
        ScopedAutoreleasePool pool;
        MTL::TextureDescriptor* descriptor = MTL::TextureDescriptor::alloc()->init();
        descriptor->setTextureType(MTL::TextureType2D);
        descriptor->setPixelFormat(source->pixelFormat());
        descriptor->setWidth(source->width());
        descriptor->setHeight(source->height());
        descriptor->setMipmapLevelCount(1);
        descriptor->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
        resolved_source = memory_allocator.CreateTexture(descriptor, MemoryUsage::DeviceLocal);
        descriptor->release();
        if (!resolved_source) {
            LOG_WARNING(Render_Metal, "No resolve target for a {}x multisampled blit",
                        src.Samples());
            return;
        }
        resolved_source->setLabel(ToNSString("eden blit resolve"));

        RenderPassState pass{};
        pass.num_color = 1;
        pass.color[0].texture = source;
        pass.color[0].resolve_texture = resolved_source.get();
        pass.color[0].load_action = LoadAction::Load;
        pass.color[0].store_action = StoreAction::Resolve;
        pass.render_area_width = static_cast<u32>(source->width());
        pass.render_area_height = static_cast<u32>(source->height());
        pass.sample_count = src.Samples();
        scheduler.RequestRenderPass(pass);
        scheduler.RequestOutsideRenderPassOperationContext();

        blit_source = resolved_source.get();
        // Named as an allocation too so the sweep drops it from the residency
        // set. Releasing it without that left the set pointing at freed memory,
        // and residency entries are wired, they can't be compressed or evicted,
        // so every stale one steals real physical memory.
        scheduler.KeepAlive(resolved_source.get(), resolved_source.get());
    } else if (dst.Samples() > 1) {
        // Single-sample source into a multisampled destination. That's what
        // ExpandToMultisample is for, but it writes the whole attachment from a
        // matching-size source and can't place a scaled region, which is what a
        // Fermi2D blit asks for. Refusing leaves the destination intact.
        static std::atomic<u32> reported{0};
        if (reported++ < 4) {
            LOG_WARNING(Render_Metal, "Skipping a blit into a {}x multisampled target",
                        dst.Samples());
        }
        return;
    }
    if (source->textureType() == MTL::TextureType3D ||
        destination->textureType() == MTL::TextureType3D) {
        // A volume render target can't go through the blit helper, its pipelines
        // sample texture2d.
        LOG_WARNING(Render_Metal, "Skipping 2D blit involving a 3D render target");
        return;
    }

    blit_image_helper.BlitColor(destination, blit_source, dst_region, src_region, filter);
}

void TextureCacheRuntime::AccelerateImageUpload(
    Image&, const StagingBufferRef&, std::span<const VideoCommon::SwizzleParameters>, u32, u32) {
    LOG_WARNING(Render_Metal, "Accelerated image upload is not implemented");
}

// ---------------------------------------------------------------------------
// Image
// ---------------------------------------------------------------------------

Image::Image(TextureCacheRuntime& runtime_, const ImageInfo& info_, GPUVAddr gpu_addr_,
             VAddr cpu_addr_)
    : VideoCommon::ImageBase(info_, gpu_addr_, cpu_addr_), runtime{&runtime_} {
    ScopedAutoreleasePool pool;

    const FormatInfo format_info = SurfaceFormat(runtime->device, info.format);
    if (format_info.support == FormatSupport::Unsupported) {
        LOG_ERROR(Render_Metal, "Unsupported image format {}", info.format);
        return;
    }

    // A compressed family this device doesn't have gets decoded on the CPU into
    // whatever format SurfaceFormat substituted. Without Converted the shared
    // cache uploads the still-compressed guest bytes into an RGBA8 texture.
    if (!IsCompressedFamilyNative(runtime->device, info.format)) {
        flags |= VideoCommon::ImageFlagBits::Converted;
        flags |= VideoCommon::ImageFlagBits::CostlyLoad;
    }
    // Same routing for a format that's just the wrong shape and not compressed.
    // Converted without CostlyLoad, the expansion is a linear pass and marking
    // it costly would skew eviction against cheap-to-rebuild images.
    if (VideoCommon::IsPixelFormatExpanded(info.format)) {
        flags |= VideoCommon::ImageFlagBits::Converted;
    }
    pixel_format = format_info.format;
    // Clamped, an unsupported count makes newTexture fail outright and leaves
    // the image and every view over it null, so a whole surface goes missing.
    samples = static_cast<u32>(info.num_samples);
    const u32 max_samples = runtime->device.GetMaxSampleCount();
    if (samples > max_samples) {
        LOG_WARNING(Render_Metal, "Clamping {}x MSAA to the device maximum of {}x", samples,
                    max_samples);
        samples = max_samples;
    }

    // The guest's extent counts samples as texels, Maxwell lays a multisampled
    // surface out spatially, so info.size is already sample-expanded. Making the
    // host texture at that size AND giving it a sample count allocates the
    // expansion twice and puts the rendered image in the top-left quadrant.
    const auto [samples_x, samples_y] = VideoCommon::SamplesLog2(info.num_samples);

    MTL::TextureDescriptor* descriptor = MTL::TextureDescriptor::alloc()->init();
    descriptor->setTextureType(ToTextureType(info));
    descriptor->setPixelFormat(pixel_format);
    descriptor->setWidth(info.size.width >> samples_x);
    descriptor->setHeight(info.size.height >> samples_y);
    descriptor->setDepth(info.type == VideoCommon::ImageType::e3D ? info.size.depth : 1);
    // Metal requires a multisampled texture to have exactly one mip level.
    descriptor->setMipmapLevelCount(samples > 1 ? 1 : info.resources.levels);
    descriptor->setArrayLength(info.type == VideoCommon::ImageType::e3D
                                   ? 1
                                   : info.resources.layers);
    descriptor->setSampleCount(samples);
    texture_usage = UsageFor(info, format_info);
    descriptor->setUsage(texture_usage);

    texture = runtime->memory_allocator.CreateTexture(descriptor, MemoryUsage::DeviceLocal);
    descriptor->release();

    if (!texture) {
        LOG_ERROR(Render_Metal, "Failed to create a {}x{} image", info.size.width,
                  info.size.height);
        return;
    }
    texture->setLabel(ToNSString("eden image"));

}

Image::Image(const VideoCommon::NullImageParams& params) : VideoCommon::ImageBase{params} {}

Image::~Image() {
    // Hand the textures to the scheduler instead of freeing them here. A Metal 4
    // command buffer holds no strong reference to what it touches, so an image
    // dropped while a recorded draw still samples it is a use-after-free. Passed
    // as allocations too so the sweep drops them from the residency set.
    if (runtime != nullptr) {
        runtime->scheduler.KeepAlive(texture.get(), texture.get());
        runtime->scheduler.KeepAlive(scaled_texture.get(), scaled_texture.get());
    }
}

namespace {

// Block metrics of the data that's actually in the staging buffer. Do NOT take
// these from info.format for a Converted image, the shared cache already
// rewrote the staging span into the host format while info.format still
// describes the guest's. Metal wants bytesPerRow and bytesPerImage in bytes, so
// getting it wrong shears every converted texture and misplaces every array
// layer past the first. The converted layout is 1x1 texels of
// ConvertedBytesPerBlock, the same oracle that sized the staging buffer, except
// for ASTC recompressed to BC which gives real 4x4 blocks.
struct StagingLayout {
    u32 bytes_per_block;
    u32 block_width;
    u32 block_height;
};

[[nodiscard]] StagingLayout StagingLayoutOf(const VideoCommon::ImageInfo& info,
                                            VideoCommon::ImageFlagBits flags) {
    using VideoCore::Surface::BytesPerBlock;
    using VideoCore::Surface::DefaultBlockHeight;
    using VideoCore::Surface::DefaultBlockWidth;

    if (False(flags & VideoCommon::ImageFlagBits::Converted)) {
        return {BytesPerBlock(info.format), DefaultBlockWidth(info.format),
                DefaultBlockHeight(info.format)};
    }

    const auto recompression = Settings::values.astc_recompression.GetValue();
    if (VideoCore::Surface::IsPixelFormatASTC(info.format) &&
        recompression != Settings::AstcRecompression::Uncompressed) {
        // Recompressed instead of decoded, so real BC blocks come out.
        return {recompression == Settings::AstcRecompression::Bc1 ? 8u : 16u, 4u, 4u};
    }
    return {VideoCommon::ConvertedBytesPerBlock(info.format), 1u, 1u};
}

} // Anonymous namespace

void Image::UploadMemory(MTL::Buffer* buffer, u64 offset,
                         std::span<const VideoCommon::BufferImageCopy> copies) {
    if (!texture || buffer == nullptr) {
        return;
    }
    // Uploads and downloads work on the UNSCALED texture, so a rescaled image
    // has to come down first and go back up after. Handle() returns
    // scaled_texture while the Rescaled flag is set.
    const bool was_rescaled = True(flags & VideoCommon::ImageFlagBits::Rescaled);
    if (was_rescaled) {
        ScaleDown(true);
    }
    SCOPE_EXIT {
        if (was_rescaled) {
            ScaleUp();
        }
    };
    // A blit can't write a multisampled destination, so the upload lands in a
    // single-sample staging texture and a draw spreads it across the samples.
    // Allocated per upload, MSAA images are rarely re-uploaded.
    Owned<MTL::Texture> staging;
    if (samples > 1) {
        // Single-layer only. The staging texture is a plain 2D one but the copy
        // loop addresses base_layer + layer, and ExpandToMultisample draws over
        // the whole attachment with no slice of its own.
        if (info.resources.layers > 1) {
            static std::atomic<u32> reported{0};
            if (reported++ < 4) {
                LOG_WARNING(Render_Metal,
                            "Skipping the upload of a {}x MSAA image with {} layers; only slice 0 "
                            "could be written and the rest would be out of range",
                            samples, info.resources.layers);
            }
            return;
        }
        ScopedAutoreleasePool pool;
        MTL::TextureDescriptor* descriptor = MTL::TextureDescriptor::alloc()->init();
        descriptor->setTextureType(MTL::TextureType2D);
        descriptor->setPixelFormat(pixel_format);
        descriptor->setWidth(info.size.width);
        descriptor->setHeight(info.size.height);
        descriptor->setMipmapLevelCount(1);
        descriptor->setUsage(MTL::TextureUsageShaderRead);
        staging = runtime->memory_allocator.CreateTexture(descriptor, MemoryUsage::DeviceLocal);
        descriptor->release();
        if (!staging) {
            LOG_WARNING(Render_Metal, "No staging texture for a {}x MSAA upload", samples);
            return;
        }
        staging->setLabel(ToNSString("eden MSAA upload staging"));
    }

    boost::container::small_vector<VideoCommon::BufferImageCopy, 8> owned(copies.begin(),
                                                                          copies.end());
    MTL::Texture* dst = staging ? staging.get() : texture.get();
    const auto [bytes_per_block, block_width, block_height] = StagingLayoutOf(info, flags);

    // The staging span is fully written by now. Publishing the whole region is
    // coarse but correct, and a no-op on unified memory.
    MemoryAllocator::FlushRange(buffer, offset, buffer->length() - offset);

    // A combined depth/stencil texture holds two interleaved aspects and Metal
    // refuses a copy that doesn't name one. The guest hands over the depth
    // aspect, and the two options are mutually exclusive so one copy can't carry
    // both.
    const MTL::BlitOption blit_options =
        IsCombinedDepthStencil(dst->pixelFormat()) ? MTL::BlitOptionDepthFromDepthStencil
                                                   : MTL::BlitOptionNone;

    // A guest D24S8 gets promoted to D32S8 on Apple GPUs, which have no
    // Depth24Unorm_Stencil8, and the two layouts differ. The staging span holds
    // a 24-bit unorm depth packed with an 8-bit stencil, while
    // BlitOptionDepthFromDepthStencil on a D32S8 texture wants 32-bit floats.
    // Both are 4 bytes so Metal takes the copy and reads the packed integer as
    // an IEEE float. We de-interleave into separate planes and blit each aspect
    // on its own, the two options are mutually exclusive.
    //
    // On the CPU on purpose, these are one-off depth surface inits and not
    // per-frame traffic.
    //
    // Depth is bits 8..31 and stencil 0..7 for BOTH guest orderings, because
    // maxwell_to_vk maps D24_UNORM_S8_UINT and S8_UINT_D24_UNORM to the same
    // Vulkan format. Reading them the same way keeps the backends agreeing.
    const bool promoted_d24s8 =
        dst->pixelFormat() == MTL::PixelFormatDepth32Float_Stencil8 &&
        (info.format == PixelFormat::D24_UNORM_S8_UINT ||
         info.format == PixelFormat::S8_UINT_D24_UNORM) &&
        bytes_per_block == 4 && buffer->contents() != nullptr;

    if (promoted_d24s8) {
        // One scratch allocation for every region, laid out as a depth plane
        // then a stencil plane per layer. 16-byte aligned offsets suit both
        // aspects' texel sizes.
        struct PlaneSpan {
            u64 depth_offset;
            u64 stencil_offset;
            u32 row_texels;
            u32 image_texels;
        };
        boost::container::small_vector<PlaneSpan, 8> spans;
        u64 scratch_bytes = 0;
        const auto align16 = [](u64 value) { return (value + 15u) & ~u64{15}; };
        for (const VideoCommon::BufferImageCopy& copy : owned) {
            const u32 row_texels =
                copy.buffer_row_length != 0 ? copy.buffer_row_length : copy.image_extent.width;
            const u32 image_texels =
                copy.buffer_image_height != 0 ? copy.buffer_image_height : copy.image_extent.height;
            const u64 texels = u64{row_texels} * image_texels;
            for (s32 layer = 0; layer < copy.image_subresource.num_layers; ++layer) {
                const u64 depth_offset = align16(scratch_bytes);
                const u64 stencil_offset = align16(depth_offset + texels * 4);
                scratch_bytes = stencil_offset + texels;
                spans.push_back(PlaneSpan{depth_offset, stencil_offset, row_texels, image_texels});
            }
        }

        const StagingBufferRef scratch =
            runtime->staging_buffer_pool.Request(static_cast<size_t>(scratch_bytes),
                                                 MemoryUsage::Upload);
        if (scratch.buffer == nullptr || scratch.mapped_span.empty()) {
            LOG_WARNING(Render_Metal, "No staging space to split a {} byte depth/stencil upload",
                        scratch_bytes);
            return;
        }

        const u8* const src_base = static_cast<const u8*>(buffer->contents()) + offset;
        u8* const dst_base = scratch.mapped_span.data();
        size_t span_index = 0;
        for (const VideoCommon::BufferImageCopy& copy : owned) {
            const PlaneSpan& first = spans[span_index];
            const u64 src_bytes_per_image = u64{first.row_texels} * 4u * first.image_texels;
            for (s32 layer = 0; layer < copy.image_subresource.num_layers; ++layer) {
                const PlaneSpan& span = spans[span_index++];
                const u8* src = src_base + copy.buffer_offset + layer * src_bytes_per_image;
                u8* depth_plane = dst_base + span.depth_offset;
                u8* stencil_plane = dst_base + span.stencil_offset;
                for (u32 y = 0; y < span.image_texels; ++y) {
                    for (u32 x = 0; x < span.row_texels; ++x) {
                        u32 packed;
                        std::memcpy(&packed, src + (u64{y} * span.row_texels + x) * 4,
                                    sizeof(packed));
                        // 0xFFFFFF is the largest 24-bit value, dividing by it
                        // maps the guest's full range onto 0..1.
                        const float depth = static_cast<float>(packed >> 8) / 16777215.0f;
                        std::memcpy(depth_plane + (u64{y} * span.row_texels + x) * 4, &depth,
                                    sizeof(depth));
                        stencil_plane[u64{y} * span.row_texels + x] =
                            static_cast<u8>(packed & 0xFFu);
                    }
                }
            }
        }
        MemoryAllocator::FlushRange(scratch.buffer, scratch.offset,
                                    static_cast<u64>(scratch_bytes));

        runtime->scheduler.RequestOutsideRenderPassOperationContext();
        runtime->scheduler.Record([dst, scratch, spans = std::move(spans),
                                   copies = std::move(owned)](CommandContext& context) {
            BlitEncoder encoder = context.RequestBlit();
            if (encoder == nullptr) {
                return;
            }
            size_t span_index = 0;
            for (const VideoCommon::BufferImageCopy& copy : copies) {
                for (s32 layer = 0; layer < copy.image_subresource.num_layers; ++layer) {
                    const PlaneSpan& span = spans[span_index++];
                    const MTL::Size extent =
                        MTL::Size::Make(copy.image_extent.width, copy.image_extent.height,
                                        copy.image_extent.depth);
                    const u64 texels = u64{span.row_texels} * span.image_texels;
                    // Two copies, because the aspect options are mutually
                    // exclusive - there is no "both" to ask for.
                    encoder->copyFromBuffer(scratch.buffer, scratch.offset + span.depth_offset,
                                            span.row_texels * 4u, texels * 4u, extent, dst,
                                            copy.image_subresource.base_layer + layer,
                                            copy.image_subresource.base_level,
                                            ToOrigin(copy.image_offset),
                                            MTL::BlitOptionDepthFromDepthStencil);
                    encoder->copyFromBuffer(scratch.buffer, scratch.offset + span.stencil_offset,
                                            span.row_texels, texels, extent, dst,
                                            copy.image_subresource.base_layer + layer,
                                            copy.image_subresource.base_level,
                                            ToOrigin(copy.image_offset),
                                            MTL::BlitOptionStencilFromDepthStencil);
                }
            }
        });

        if (staging) {
            runtime->blit_image_helper.ExpandToMultisample(texture.get(), staging.get());
            // Named as an allocation too so the sweep drops it from the
            // residency set, those entries are wired and can't be evicted.
            runtime->scheduler.KeepAlive(staging.get(), staging.get());
        }
        return;
    }

    runtime->scheduler.RequestOutsideRenderPassOperationContext();
    runtime->scheduler.Record([dst, buffer, offset, bytes_per_block, block_width, block_height,
                               blit_options, copies = std::move(owned)](CommandContext& context) {
        BlitEncoder encoder = context.RequestBlit();
        if (encoder == nullptr) {
            return;
        }
        for (const VideoCommon::BufferImageCopy& copy : copies) {
            // Metal wants bytes, not texels, and whole blocks for compressed
            // formats. The row length MUST come from buffer_row_length, which
            // describes the source buffer. Taking the stride from image_extent
            // shears the upload into diagonal bands.
            const u32 row_texels =
                copy.buffer_row_length != 0 ? copy.buffer_row_length : copy.image_extent.width;
            const u32 image_texels =
                copy.buffer_image_height != 0 ? copy.buffer_image_height : copy.image_extent.height;
            const u32 blocks_across = (row_texels + block_width - 1) / block_width;
            const u32 blocks_down = (image_texels + block_height - 1) / block_height;
            const NS::UInteger bytes_per_row = blocks_across * bytes_per_block;
            const NS::UInteger bytes_per_image = bytes_per_row * blocks_down;

            for (s32 layer = 0; layer < copy.image_subresource.num_layers; ++layer) {
                encoder->copyFromBuffer(
                    buffer, offset + copy.buffer_offset + layer * bytes_per_image, bytes_per_row,
                    bytes_per_image,
                    MTL::Size::Make(copy.image_extent.width, copy.image_extent.height,
                                    copy.image_extent.depth),
                    dst, copy.image_subresource.base_layer + layer,
                    copy.image_subresource.base_level, ToOrigin(copy.image_offset),
                    blit_options);
            }
        }
    });

    if (staging) {
        // Now spread it across every sample. The staging texture goes to a
        // recorded command so it outlives the expand instead of dying here.
        runtime->blit_image_helper.ExpandToMultisample(texture.get(), staging.get());
        // Named as an allocation too so the sweep drops it from the residency
        // set, those entries are wired and can't be evicted.
        runtime->scheduler.KeepAlive(staging.get(), staging.get());
    }
}

void Image::UploadMemory(const StagingBufferRef& map,
                         std::span<const VideoCommon::BufferImageCopy> copies) {
    UploadMemory(map.buffer, map.offset, copies);
}

void Image::DownloadMemory(MTL::Buffer* buffer, size_t offset,
                           std::span<const VideoCommon::BufferImageCopy> copies) {
    if (!texture || buffer == nullptr) {
        return;
    }
    // Uploads and downloads work on the UNSCALED texture, so a rescaled image
    // has to come down first and go back up after. Handle() returns
    // scaled_texture while the Rescaled flag is set.
    const bool was_rescaled = True(flags & VideoCommon::ImageFlagBits::Rescaled);
    if (was_rescaled) {
        ScaleDown(true);
    }
    SCOPE_EXIT {
        if (was_rescaled) {
            ScaleUp();
        }
    };

    boost::container::small_vector<VideoCommon::BufferImageCopy, 8> owned(copies.begin(),
                                                                          copies.end());
    MTL::Texture* src = texture.get();

    // A blit encoder can't read a multisampled texture, so resolve first through
    // a pass whose store action is Resolve and blit from that. It's lossy, the
    // guest gets the sample average, but it's the only readback Metal gives us.
    Owned<MTL::Texture> resolved;
    if (info.num_samples > 1) {
        const bool is_color = VideoCore::Surface::GetFormatType(info.format) ==
                              VideoCore::Surface::SurfaceType::ColorTexture;
        // A resolve store action carries colour only, RenderPassState has no
        // resolve texture for the depth attachment. Refusing gives back stale
        // contents, which beats an undefined blit.
        if (!is_color || info.resources.layers > 1) {
            static std::atomic<u32> reported{0};
            if (reported++ < 4) {
                LOG_WARNING(Render_Metal,
                            "Cannot read back a {}x MSAA image with {} layer(s), format {}: no "
                            "resolve path for it",
                            info.num_samples, info.resources.layers,
                            static_cast<u32>(info.format));
            }
            return;
        }

        ScopedAutoreleasePool pool;
        MTL::TextureDescriptor* descriptor = MTL::TextureDescriptor::alloc()->init();
        descriptor->setTextureType(MTL::TextureType2D);
        descriptor->setPixelFormat(texture->pixelFormat());
        descriptor->setWidth(texture->width());
        descriptor->setHeight(texture->height());
        descriptor->setMipmapLevelCount(1);
        descriptor->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
        resolved = runtime->memory_allocator.CreateTexture(descriptor, MemoryUsage::DeviceLocal);
        descriptor->release();
        if (!resolved) {
            LOG_WARNING(Render_Metal, "No resolve target for a {}x MSAA download",
                        info.num_samples);
            return;
        }
        resolved->setLabel(ToNSString("eden MSAA download resolve"));

        RenderPassState pass{};
        pass.num_color = 1;
        pass.color[0].texture = texture.get();
        pass.color[0].resolve_texture = resolved.get();
        pass.color[0].load_action = LoadAction::Load;
        pass.color[0].store_action = StoreAction::Resolve;
        pass.render_area_width = static_cast<u32>(texture->width());
        pass.render_area_height = static_cast<u32>(texture->height());
        pass.sample_count = info.num_samples;
        runtime->scheduler.RequestRenderPass(pass);
        // No draw, the resolve is the store action so ending the pass is the
        // whole operation.
        runtime->scheduler.RequestOutsideRenderPassOperationContext();

        src = resolved.get();
    }

    const auto [bytes_per_block, block_width, block_height] = StagingLayoutOf(info, flags);

    // See Image::UploadMemory, reading out of a combined depth/stencil texture
    // has to name the aspect too.
    const MTL::BlitOption blit_options =
        IsCombinedDepthStencil(src->pixelFormat()) ? MTL::BlitOptionDepthFromDepthStencil
                                                   : MTL::BlitOptionNone;

    // The reverse of the split in UploadMemory. Metal hands back a float depth
    // plane and a byte stencil plane, one aspect per copy, while the guest wants
    // a packed 32-bit word per texel. The re-interleave is a kernel and not a
    // CPU pass because the planes don't exist until the blits have run and
    // DownloadMemory has no completion hook.
    const bool promoted_d24s8 =
        src->pixelFormat() == MTL::PixelFormatDepth32Float_Stencil8 &&
        (info.format == PixelFormat::D24_UNORM_S8_UINT ||
         info.format == PixelFormat::S8_UINT_D24_UNORM) &&
        bytes_per_block == 4;

    if (promoted_d24s8) {
        struct PlaneSpan {
            u64 depth_offset;
            u64 stencil_offset;
            u32 row_texels;
            u32 image_texels;
        };
        boost::container::small_vector<PlaneSpan, 8> spans;
        u64 scratch_bytes = 0;
        const auto align16 = [](u64 value) { return (value + 15u) & ~u64{15}; };
        for (const VideoCommon::BufferImageCopy& copy : owned) {
            const u32 row_texels =
                copy.buffer_row_length != 0 ? copy.buffer_row_length : copy.image_extent.width;
            const u32 image_texels =
                copy.buffer_image_height != 0 ? copy.buffer_image_height : copy.image_extent.height;
            const u64 texels = u64{row_texels} * image_texels;
            for (s32 layer = 0; layer < copy.image_subresource.num_layers; ++layer) {
                const u64 depth_offset = align16(scratch_bytes);
                const u64 stencil_offset = align16(depth_offset + texels * 4);
                scratch_bytes = stencil_offset + texels;
                spans.push_back(PlaneSpan{depth_offset, stencil_offset, row_texels, image_texels});
            }
        }

        // DeviceLocal, the GPU writes these planes and reads them back in the
        // kernel. Nothing on the CPU ever looks at them.
        const StagingBufferRef scratch = runtime->staging_buffer_pool.Request(
            static_cast<size_t>(scratch_bytes), MemoryUsage::DeviceLocal);
        if (scratch.buffer == nullptr) {
            LOG_WARNING(Render_Metal, "No staging space to split a {} byte depth/stencil download",
                        scratch_bytes);
            return;
        }

        runtime->scheduler.RequestOutsideRenderPassOperationContext();
        {
            auto span_copy = spans;
            runtime->scheduler.Record([src, scratch, spans = std::move(span_copy),
                                       copies = owned](CommandContext& context) {
                BlitEncoder encoder = context.RequestBlit();
                if (encoder == nullptr) {
                    return;
                }
                size_t span_index = 0;
                for (const VideoCommon::BufferImageCopy& copy : copies) {
                    for (s32 layer = 0; layer < copy.image_subresource.num_layers; ++layer) {
                        const PlaneSpan& span = spans[span_index++];
                        const MTL::Size extent =
                            MTL::Size::Make(copy.image_extent.width, copy.image_extent.height,
                                            copy.image_extent.depth);
                        const u64 texels = u64{span.row_texels} * span.image_texels;
                        encoder->copyFromTexture(
                            src, copy.image_subresource.base_layer + layer,
                            copy.image_subresource.base_level, ToOrigin(copy.image_offset), extent,
                            scratch.buffer, scratch.offset + span.depth_offset,
                            span.row_texels * 4u, texels * 4u,
                            MTL::BlitOptionDepthFromDepthStencil);
                        encoder->copyFromTexture(
                            src, copy.image_subresource.base_layer + layer,
                            copy.image_subresource.base_level, ToOrigin(copy.image_offset), extent,
                            scratch.buffer, scratch.offset + span.stencil_offset, span.row_texels,
                            texels, MTL::BlitOptionStencilFromDepthStencil);
                    }
                }
            });
        }

        // Then fold the two planes back together, one dispatch per region. The
        // encoder switch from blit to compute is what orders this after them.
        size_t span_index = 0;
        for (const VideoCommon::BufferImageCopy& copy : owned) {
            for (s32 layer = 0; layer < copy.image_subresource.num_layers; ++layer) {
                const PlaneSpan& span = spans[span_index++];
                runtime->blit_image_helper.PackDepthStencil(
                    buffer,
                    offset + copy.buffer_offset +
                        static_cast<u64>(layer) * span.row_texels * 4u * span.image_texels,
                    span.row_texels, scratch.buffer, scratch.offset + span.depth_offset,
                    scratch.offset + span.stencil_offset, span.row_texels,
                    copy.image_extent.width, copy.image_extent.height);
            }
        }
        return;
    }

    // Ends the pass on the recording side too. RequestBlit closes the render
    // encoder on the worker, but without this the recording thread's mirror
    // still thinks a pass is open and every later check of that is wrong.
    runtime->scheduler.RequestOutsideRenderPassOperationContext();
    runtime->scheduler.Record([src, buffer, offset, bytes_per_block, block_width, block_height,
                               blit_options, copies = std::move(owned)](CommandContext& context) {
        BlitEncoder encoder = context.RequestBlit();
        if (encoder == nullptr) {
            return;
        }
        for (const VideoCommon::BufferImageCopy& copy : copies) {
            // Same as UploadMemory, the buffer's layout comes from
            // buffer_row_length/buffer_image_height and not from the region.
            const u32 row_texels =
                copy.buffer_row_length != 0 ? copy.buffer_row_length : copy.image_extent.width;
            const u32 image_texels =
                copy.buffer_image_height != 0 ? copy.buffer_image_height : copy.image_extent.height;
            const u32 blocks_across = (row_texels + block_width - 1) / block_width;
            const u32 blocks_down = (image_texels + block_height - 1) / block_height;
            const NS::UInteger bytes_per_row = blocks_across * bytes_per_block;
            const NS::UInteger bytes_per_image = bytes_per_row * blocks_down;

            for (s32 layer = 0; layer < copy.image_subresource.num_layers; ++layer) {
                encoder->copyFromTexture(
                    src, copy.image_subresource.base_layer + layer,
                    copy.image_subresource.base_level, ToOrigin(copy.image_offset),
                    MTL::Size::Make(copy.image_extent.width, copy.image_extent.height,
                                    copy.image_extent.depth),
                    buffer, offset + copy.buffer_offset + layer * bytes_per_image, bytes_per_row,
                    bytes_per_image, blit_options);
            }
        }
    });
}

void Image::DownloadMemory(std::span<MTL::Buffer*> buffers, std::span<size_t> offsets,
                           std::span<const VideoCommon::BufferImageCopy> copies) {
    for (size_t i = 0; i < buffers.size(); ++i) {
        DownloadMemory(buffers[i], offsets[i], copies);
    }
}

void Image::DownloadMemory(const StagingBufferRef& map,
                           std::span<const VideoCommon::BufferImageCopy> copies) {
    DownloadMemory(map.buffer, map.offset, copies);
}

// ---------------------------------------------------------------------------
// ImageView
// ---------------------------------------------------------------------------

ImageView::ImageView(TextureCacheRuntime& runtime_, const VideoCommon::ImageViewInfo& info_,
                     ImageId image_id_, Image& image)
    : VideoCommon::ImageViewBase{info_, image.info, image_id_, image.gpu_addr}, runtime{&runtime_},
      image_handle{image.Handle()}, pixel_format{image.Format()}, samples{image.Samples()} {
    if (image_handle == nullptr) {
        return;
    }

    // The format every view of this image uses. The attachment view and the
    // sampled views MUST agree or the guest renders through one interpretation
    // and samples back through another.
    const FormatInfo view_format = SurfaceFormat(runtime->device, info_.format);
    const MTL::PixelFormat sample_format =
        ViewSampleFormat(view_format, info_.format, image.info.format, pixel_format);

    // Views are NOT added to the device residency set on purpose. A view owns no
    // memory, it aliases the parent's pages, and the parent went in when
    // MemoryAllocator created it. Validation has never asked for one.
    //
    // newTextureView returns a +1 object so adopt it, don't retain. The
    // attachment view keeps the image's texture type. The shader-visible view
    // uses the type the guest declared and is the only one carrying the swizzle,
    // Metal refuses to render into a swizzled view.
    if (image_handle->textureType() == MTL::TextureType3D) {
        // newTextureView can't narrow a 3D texture to one depth plane, so the
        // attachment is the whole volume and we pick the subresource at
        // pass-build time with the attachment's level + depthPlane.
        render_target = Adopt(image_handle->newTextureView(sample_format));
        // This skips MakeView so it needs the same registration, an attachment
        // is addressed by its own gpuResourceID like any other view.
        if (render_target && runtime != nullptr) {
            runtime->device.AddResidentAllocation(render_target.get());
        }
        rt_level = range.base.level;
        rt_slice = range.base.layer;
        // Every plane from the base to the end of the mip, Metal validates the
        // array length against exactly this remainder.
        const u32 mip_depth =
            std::max<u32>(1, static_cast<u32>(image_handle->depth()) >> rt_level);
        rt_layers = mip_depth > rt_slice ? mip_depth - rt_slice : 1;
    } else {
        render_target = Adopt(MakeView(sample_format, image_handle->textureType(),
                                       range.base.level, range.extent.levels, range.base.layer,
                                       range.extent.layers, nullptr));
    }

    const MTL::TextureSwizzleChannels swizzle = MakeSwizzle(info_, view_format.swizzle_fix);
    const bool identity = IsIdentitySwizzle(swizzle);

    // Which ASPECT of a packed depth/stencil image the guest wants to sample.
    // Maxwell names the components positionally and the order depends on the
    // format, so a swizzle mentioning R means depth for D24S8/D32S8 and stencil
    // for S8_D24. Metal spells the stencil aspect as the X<n>_Stencil8 companion
    // format, and the attachment view keeps the depth format.
    MTL::PixelFormat sampled_format_value = sample_format;
    if (pixel_format == MTL::PixelFormatDepth32Float_Stencil8 ||
        pixel_format == MTL::PixelFormatDepth24Unorm_Stencil8) {
        const auto sources = info_.Swizzle();
        const bool any_red = std::ranges::any_of(
            sources, [](Tegra::Texture::SwizzleSource c) {
                return c == Tegra::Texture::SwizzleSource::R;
            });
        const bool red_is_stencil = info_.format == PixelFormat::S8_UINT_D24_UNORM;
        const bool wants_stencil = any_red == red_is_stencil;
        if (wants_stencil && !info_.IsRenderTarget()) {
            sampled_format_value = pixel_format == MTL::PixelFormatDepth32Float_Stencil8
                                 ? MTL::PixelFormatX32_Stencil8
                                 : MTL::PixelFormatX24_Stencil8;
        }
    }

    // We build both the single-layer and the array view of each class, the
    // shader decides how it samples and doesn't have to match the guest's
    // declared view type. Kept so Handle() can build others on demand.
    sampled_format = sampled_format_value;
    view_swizzle = swizzle;
    view_swizzle_identity = identity;

    const auto create = [&](Shader::TextureType shader_type, u32 num_layers) {
        views[static_cast<size_t>(shader_type)] =
            Adopt(MakeView(sampled_format_value, ToTextureType(shader_type), range.base.level,
                           range.extent.levels, range.base.layer, num_layers,
                           identity ? nullptr : &swizzle));
    };

    switch (type) {
    case VideoCommon::ImageViewType::e1D:
    case VideoCommon::ImageViewType::e1DArray:
        create(Shader::TextureType::Color1D, 1);
        create(Shader::TextureType::ColorArray1D, range.extent.layers);
        break;
    case VideoCommon::ImageViewType::e2D:
    case VideoCommon::ImageViewType::e2DArray:
    case VideoCommon::ImageViewType::Rect:
        create(Shader::TextureType::Color2D, 1);
        create(Shader::TextureType::ColorArray2D, range.extent.layers);
        break;
    case VideoCommon::ImageViewType::e3D:
        create(Shader::TextureType::Color3D, range.extent.layers);
        break;
    case VideoCommon::ImageViewType::Cube:
    case VideoCommon::ImageViewType::CubeArray:
        create(Shader::TextureType::ColorCube, 6);
        create(Shader::TextureType::ColorArrayCube, range.extent.layers);
        break;
    case VideoCommon::ImageViewType::Buffer:
        create(Shader::TextureType::Buffer, range.extent.layers);
        break;
    }
}

ImageView::ImageView(TextureCacheRuntime& runtime_, const VideoCommon::ImageViewInfo& info_,
                     ImageId image_id_, Image& image, const Common::SlotVector<Image>& slot_images_)
    : ImageView{runtime_, info_, image_id_, image} {
    slot_images = &slot_images_;
}

ImageView::ImageView(TextureCacheRuntime& runtime_, const VideoCommon::ImageInfo& info_,
                     const VideoCommon::ImageViewInfo& view_info, GPUVAddr gpu_addr_)
    : VideoCommon::ImageViewBase{info_, view_info, gpu_addr_}, runtime{&runtime_},
      buffer_size{VideoCommon::CalculateGuestSizeInBytes(info_)} {}

ImageView::ImageView(TextureCacheRuntime& runtime_,
                     const VideoCommon::NullImageViewParams& params)
    : VideoCommon::ImageViewBase{params}, runtime{&runtime_} {}

ImageView::~ImageView() {
    // Every view MakeView/MakeArrayFromVolume built went through
    // AddResidentAllocation. A view is its own MTL::Allocation with its own
    // gpuResourceID, the parent being resident doesn't cover it. Releasing it
    // here without telling the device leaks a residency entry per view, and the
    // set is rebuilt from those entries, so wired memory climbs for as long as
    // the game runs. On an 8 GiB M1 that ends as a GPU-side stall with every
    // in-flight buffer discarded along with it.
    //
    // Deferred like Image::~Image and for the same reason, a Metal 4 command
    // buffer holds no strong reference to what it touches, so a view dropped
    // while a recorded draw still samples it is a use-after-free. Passing the
    // texture as both object and allocation makes the sweep drop the residency
    // entry when the tick retires.
    if (runtime == nullptr) {
        return;
    }
    const auto retire = [this](const Owned<MTL::Texture>& view) {
        if (view) {
            runtime->scheduler.KeepAlive(view.get(), view.get());
        }
    };
    retire(render_target);
    retire(volume_copy);
    retire(depth_view);
    retire(stencil_view);
    for (const Owned<MTL::Texture>& view : views) {
        retire(view);
    }
    for (const auto& [format, view] : storage_views) {
        retire(view);
    }
}

MTL::Texture* ImageView::MakeView(MTL::PixelFormat format, MTL::TextureType texture_type,
                                  u32 first_level, u32 num_levels, u32 first_layer,
                                  u32 num_layers,
                                  const MTL::TextureSwizzleChannels* swizzle) {
    if (image_handle == nullptr) {
        return nullptr;
    }

    // Metal ABORTS the process inside newTextureView if the level or slice range
    // steps outside the base texture. No nil return, no validation message. The
    // guest can legitimately describe a wider view, so we clamp every range
    // below instead of trusting it.
    const u32 total_levels = static_cast<u32>(image_handle->mipmapLevelCount());
    const MTL::TextureType base_type = image_handle->textureType();
    const bool base_is_cube = base_type == MTL::TextureTypeCube ||
                              base_type == MTL::TextureTypeCubeArray;
    // Has to be decided BEFORE the slice range check below. A volume keeps its
    // slices in depth and reports arrayLength 1, so the guest's slice index
    // always looks out of range and we'd drop the view. Widening the check isn't
    // an option, a 3D image viewed as 3D still needs a slice range of {0, 1} or
    // Metal aborts.
    if (base_type == MTL::TextureType3D &&
        (texture_type == MTL::TextureType2DArray || texture_type == MTL::TextureType2D)) {
        if (first_level >= total_levels) {
            return nullptr;
        }
        return MakeArrayFromVolume(format, first_level, num_levels);
    }

    const u32 total_slices =
        static_cast<u32>(image_handle->arrayLength()) * (base_is_cube ? 6u : 1u);

    if (first_level >= total_levels || first_layer >= total_slices) {
        LOG_WARNING(Render_Metal,
                    "Texture view starts outside its image (level {}/{}, slice {}/{}) - base "
                    "{}x{}x{} type={} fmt={} gpu_addr=0x{:x}",
                    first_level, total_levels, first_layer, total_slices,
                    static_cast<u32>(image_handle->width()),
                    static_cast<u32>(image_handle->height()),
                    static_cast<u32>(image_handle->depth()), static_cast<u32>(base_type),
                    static_cast<u32>(image_handle->pixelFormat()), gpu_addr);
        return nullptr;
    }
    num_levels = (std::min)(num_levels, total_levels - first_level);
    num_layers = (std::min)(num_layers, total_slices - first_layer);

    // A cube view needs six faces per cube, anything less can't be expressed and
    // aborts the same way.
    const bool view_is_cube = texture_type == MTL::TextureTypeCube ||
                              texture_type == MTL::TextureTypeCubeArray;
    if (view_is_cube && num_layers < 6) {
        static std::atomic<u32> reported{0};
        if (reported++ < 8) {
            LOG_WARNING(Render_Metal,
                        "Cube view needs 6 slices, image has {} - base {}x{}x{} type={} "
                        "levels={} fmt={} gpu_addr=0x{:x}",
                        num_layers, static_cast<u32>(image_handle->width()),
                        static_cast<u32>(image_handle->height()),
                        static_cast<u32>(image_handle->depth()), static_cast<u32>(base_type),
                        static_cast<u32>(image_handle->mipmapLevelCount()),
                        static_cast<u32>(image_handle->pixelFormat()), gpu_addr);
        }
        return nullptr;
    }

    // A view's type has to be in the same view class as the base texture, and an
    // incompatible pair aborts instead of returning nil, so check it first.
    if (ViewClass(base_type) != ViewClass(texture_type)) {
        // A 3D image sampled as a 2D array is common enough that we serve it by
        // copying the depth slices instead of dropping the view. The copy is a
        // snapshot, fine for the LUTs this hits but not for a volume the guest
        // keeps writing.
        if (base_type == MTL::TextureType3D && (texture_type == MTL::TextureType2DArray ||
                                                texture_type == MTL::TextureType2D)) {
            return MakeArrayFromVolume(format, first_level, num_levels);
        }
        static std::atomic<u32> reported{0};
        if (reported++ < 8) {
            LOG_WARNING(Render_Metal,
                        "Incompatible texture view type {} over base {} - {}x{}x{} levels={} "
                        "layers={} fmt={} gpu_addr=0x{:x}",
                        static_cast<u32>(texture_type), static_cast<u32>(base_type),
                        static_cast<u32>(image_handle->width()),
                        static_cast<u32>(image_handle->height()),
                        static_cast<u32>(image_handle->depth()),
                        static_cast<u32>(image_handle->mipmapLevelCount()),
                        static_cast<u32>(image_handle->arrayLength()),
                        static_cast<u32>(image_handle->pixelFormat()), gpu_addr);
        }
        return nullptr;
    }
    if (num_levels == 0 || num_layers == 0) {
        return nullptr;
    }

    if (swizzle != nullptr) {
        MTL::Texture* const view = image_handle->newTextureView(
            format, texture_type, NS::Range::Make(first_level, num_levels),
            NS::Range::Make(first_layer, num_layers), *swizzle);
        if (view != nullptr) {
            // A view is its own MTL::Allocation with its own gpuResourceID and
            // that ID is what the shader addresses, the parent being resident
            // doesn't cover it. Metal 4 makes nothing implicitly resident, so an
            // unregistered view is a GPU page fault that discards the whole
            // submission and everything drawn in it.
            if (runtime != nullptr) {
                runtime->device.AddResidentAllocation(view);
            }
            return view;
        }
        // Not every format takes a component mapping, depth especially. Falling
        // back to identity samples the wrong channels, but returning null would
        // make Handle() hand the shader a different texture entirely.
        static std::atomic<u32> reported{0};
        if (reported++ < 4) {
            LOG_WARNING(Render_Metal, "Swizzled view rejected for format {}; using identity",
                        static_cast<u32>(format));
        }
    }
    MTL::Texture* const view =
        image_handle->newTextureView(format, texture_type, NS::Range::Make(first_level, num_levels),
                                     NS::Range::Make(first_layer, num_layers));
    if (view != nullptr && runtime != nullptr) {
        // See the swizzled path above, the view has to be in the residency set
        // and not just its parent.
        runtime->device.AddResidentAllocation(view);
    }
    return view;
}

MTL::Texture* ImageView::MakeArrayFromVolume(MTL::PixelFormat format, u32 first_level,
                                             u32 num_levels) {
    ScopedAutoreleasePool pool;

    const u32 width = static_cast<u32>(image_handle->width());
    const u32 height = static_cast<u32>(image_handle->height());
    const u32 depth = static_cast<u32>(image_handle->depth());
    if (width == 0 || height == 0 || depth == 0) {
        return nullptr;
    }
    num_levels = (std::min)(num_levels,
                            static_cast<u32>(image_handle->mipmapLevelCount()) - first_level);

    MTL::TextureDescriptor* descriptor = MTL::TextureDescriptor::alloc()->init();
    descriptor->setTextureType(MTL::TextureType2DArray);
    descriptor->setPixelFormat(format);
    descriptor->setWidth(width);
    descriptor->setHeight(height);
    descriptor->setMipmapLevelCount(num_levels);
    descriptor->setArrayLength(depth);
    descriptor->setUsage(MTL::TextureUsageShaderRead);
    // Made here instead of through MemoryAllocator::CreateTexture because
    // MakeView has to return a +1 object and Owned is a shared pointer with no
    // release(). The storage mode is what the allocator would have picked. The
    // hazard mode does nothing, Metal 4 treats every resource as untracked and
    // the allocator sets none.
    descriptor->setStorageMode(runtime->memory_allocator.StorageModeFor(MemoryUsage::DeviceLocal));
    descriptor->setHazardTrackingMode(MTL::HazardTrackingModeTracked);

    MTL::Texture* const copy = runtime->device.GetDevice()->newTexture(descriptor);
    if (copy != nullptr) {
        runtime->device.AddResidentAllocation(copy);
        runtime->device.CommitResidency();
    }
    descriptor->release();
    if (copy == nullptr) {
        LOG_WARNING(Render_Metal, "Could not allocate a {}x{}x{} array copy of a 3D image", width,
                    height, depth);
        return nullptr;
    }
    copy->setLabel(ToNSString("eden 3D-as-2D-array"));

    // Kept so we can refresh the copy. A volume the guest re-renders every frame
    // would otherwise be sampled as a frame-0 snapshot forever.
    volume_copy_format = format;
    volume_copy_first_level = first_level;
    volume_copy_num_levels = num_levels;
    volume_copy_frame = runtime->FrameIndex();

    MTL::Texture* const source = image_handle;
    // Ends the pass on the recording side too. RequestBlit closes the render
    // encoder on the worker, but without this the recording thread's mirror
    // still thinks a pass is open and every later check of that is wrong.
    runtime->scheduler.RequestOutsideRenderPassOperationContext();
    runtime->scheduler.Record([source, copy, width, height, depth, first_level,
                               num_levels](CommandContext& context) {
        BlitEncoder encoder = context.RequestBlit();
        if (encoder == nullptr) {
            return;
        }
        for (u32 level = 0; level < num_levels; ++level) {
            const u32 shift = first_level + level;
            const u32 level_width = (std::max)(width >> shift, 1u);
            const u32 level_height = (std::max)(height >> shift, 1u);
            const u32 level_depth = (std::max)(depth >> shift, 1u);
            for (u32 slice = 0; slice < level_depth; ++slice) {
                // A 3D texture has one slice and addresses depth through the
                // origin, the destination does it through the slice index.
                encoder->copyFromTexture(
                    source, 0, shift, MTL::Origin::Make(0, 0, slice),
                    AlignCopySizeToBlock(MTL::Size::Make(level_width, level_height, 1),
                                         source->pixelFormat()),
                    copy, slice, level, MTL::Origin::Make(0, 0, 0));
            }
        }
    });

    static std::atomic<u32> made{0};
    if (made++ == 0) {
        LOG_INFO(Render_Metal, "Copying 3D images into 2D array textures for sampling");
    }
    return copy;
}

void ImageView::RefreshVolumeCopy(MTL::Texture* destination) const {
    if (destination == nullptr || image_handle == nullptr ||
        volume_copy_num_levels == 0) {
        return;
    }
    const u32 width = static_cast<u32>(image_handle->width());
    const u32 height = static_cast<u32>(image_handle->height());
    const u32 depth = static_cast<u32>(image_handle->depth());
    MTL::Texture* const source = image_handle;
    const u32 first_level = volume_copy_first_level;
    const u32 num_levels = volume_copy_num_levels;
    // Ends the pass on the recording side too. RequestBlit closes the render
    // encoder on the worker, but without this the recording thread's mirror
    // still thinks a pass is open and every later check of that is wrong.
    runtime->scheduler.RequestOutsideRenderPassOperationContext();
    runtime->scheduler.Record([source, destination, width, height, depth, first_level,
                               num_levels](CommandContext& context) {
        BlitEncoder encoder = context.RequestBlit();
        if (encoder == nullptr) {
            return;
        }
        for (u32 level = 0; level < num_levels; ++level) {
            const u32 shift = first_level + level;
            const u32 level_width = (std::max)(width >> shift, 1u);
            const u32 level_height = (std::max)(height >> shift, 1u);
            const u32 level_depth = (std::max)(depth >> shift, 1u);
            for (u32 slice = 0; slice < level_depth; ++slice) {
                encoder->copyFromTexture(
                    source, 0, shift, MTL::Origin::Make(0, 0, slice),
                    AlignCopySizeToBlock(MTL::Size::Make(level_width, level_height, 1),
                                         source->pixelFormat()),
                    destination, slice, level, MTL::Origin::Make(0, 0, 0));
            }
        }
    });
}

MTL::Texture* ImageView::MakeViewForType(Shader::TextureType texture_type) const {
    // Layer count follows the requested type, same as the constructor's switch.
    // A single-layer type takes one, an array type takes the view's whole range,
    // and a cube needs its six faces.
    u32 num_layers = 1;
    switch (texture_type) {
    case Shader::TextureType::ColorArray1D:
    case Shader::TextureType::ColorArray2D:
    case Shader::TextureType::Color3D:
    case Shader::TextureType::ColorArrayCube:
    case Shader::TextureType::Buffer:
        num_layers = range.extent.layers;
        break;
    case Shader::TextureType::ColorCube:
        num_layers = 6;
        break;
    default:
        break;
    }
    return const_cast<ImageView*>(this)->MakeView(
        sampled_format, ToTextureType(texture_type), range.base.level, range.extent.levels,
        range.base.layer, num_layers, view_swizzle_identity ? nullptr : &view_swizzle);
}

MTL::Texture* ImageView::Handle(Shader::TextureType texture_type) const noexcept {
    const size_t index = static_cast<size_t>(texture_type);
    if (index >= views.size()) {
        return nullptr;
    }
    const u32 attempt_bit = 1u << index;
    if (!views[index] && image_handle != nullptr && (view_attempted & attempt_bit) == 0) {
        // Build the type the shader wants, but only once. The image can't change
        // shape, so a type that fails to view now always will.
        view_attempted |= attempt_bit;
        views[index] = Adopt(MakeViewForType(texture_type));
    }
    if (views[index]) {
        // The 2D-array stand-in is a copy and goes stale when the guest writes
        // the volume again. Re-record it at most once per frame.
        if (volume_copy_num_levels != 0) {
            const u64 frame = runtime->FrameIndex();
            if (volume_copy_frame != frame) {
                volume_copy_frame = frame;
                RefreshVolumeCopy(views[index].get());
            }
        }
        return views[index].get();
    }
    // Nothing of the requested type could be built, so the shader gets NOTHING.
    // Do NOT substitute another view of the image, Metal won't reject a 2D
    // handle where the shader declared a cube, it just samples garbage. Null
    // reads as zero, same as what Vulkan gives here.
    static std::atomic<u32> reported{0};
    if (reported++ < 12) {
        LOG_WARNING(Render_Metal,
                    "No view for shader texture type {}; binding nothing, so it reads zero "
                    "- base {}x{}x{} type={} levels={} layers={} "
                    "fmt={} samples={} view_type={} range(level {}+{}, layer {}+{}) gpu_addr=0x{:x}",
                    index, image_handle != nullptr ? image_handle->width() : 0,
                    image_handle != nullptr ? image_handle->height() : 0,
                    image_handle != nullptr ? image_handle->depth() : 0,
                    image_handle != nullptr ? static_cast<u32>(image_handle->textureType()) : 0u,
                    image_handle != nullptr ? image_handle->mipmapLevelCount() : 0,
                    image_handle != nullptr ? image_handle->arrayLength() : 0,
                    image_handle != nullptr ? static_cast<u32>(image_handle->pixelFormat()) : 0u,
                    image_handle != nullptr ? image_handle->sampleCount() : 0,
                    static_cast<u32>(type), range.base.level, range.extent.levels,
                    range.base.layer, range.extent.layers, gpu_addr);
    }
    return nullptr;
}

MTL::Texture* ImageView::StorageView(Shader::TextureType texture_type,
                                     Shader::ImageFormat image_format) {
    if (image_format == Shader::ImageFormat::Typeless) {
        return Handle(texture_type);
    }
    const FormatInfo info = SurfaceFormat(runtime->device, StorageFormatOf(image_format));
    if (info.support == FormatSupport::Unsupported || image_handle == nullptr) {
        return Handle(texture_type);
    }
    // A storage view reinterprets the same bits, so it only works between
    // formats of the same texel size. A guest declaring a wider format than the
    // surface holds isn't something Metal can express.
    if (info.format == pixel_format) {
        return Handle(texture_type);
    }
    const u32 key = (static_cast<u32>(image_format) << 8) | static_cast<u32>(texture_type);
    if (const auto it = storage_views.find(key); it != storage_views.end()) {
        return it->second.get();
    }
    Owned<MTL::Texture> view =
        Adopt(MakeView(info.format, ToTextureType(texture_type), range.base.level,
                       range.extent.levels, range.base.layer, range.extent.layers, nullptr));
    if (!view) {
        return Handle(texture_type);
    }
    MTL::Texture* const raw = view.get();
    storage_views.emplace(key, std::move(view));
    return raw;
}

MTL::Texture* ImageView::ColorView() {
    return render_target.get();
}

MTL::Texture* ImageView::DepthView() {
    if (depth_view) {
        return depth_view.get();
    }
    depth_view = Adopt(MakeView(pixel_format, image_handle->textureType(), range.base.level,
                                range.extent.levels, range.base.layer, range.extent.layers,
                                nullptr));
    return depth_view.get();
}

MTL::Texture* ImageView::StencilView() {
    if (stencil_view) {
        return stencil_view.get();
    }
    // A packed depth/stencil texture is viewed as stencil-only by asking for
    // the X<n>_Stencil8 companion format.
    const MTL::PixelFormat stencil_format =
        pixel_format == MTL::PixelFormatDepth32Float_Stencil8 ? MTL::PixelFormatX32_Stencil8
                                                              : MTL::PixelFormatX24_Stencil8;
    stencil_view = Adopt(MakeView(stencil_format, image_handle->textureType(), range.base.level,
                                  range.extent.levels, range.base.layer, range.extent.layers,
                                  nullptr));
    return stencil_view.get();
}

bool ImageView::IsRescaled() const noexcept {
    // Ask the image instead of caching it. A view outlives the decision, the
    // cache toggles resolution before invalidating what pointed at it.
    if (slot_images == nullptr) {
        return false;
    }
    return (*slot_images)[image_id].IsRescaled();
}

// ---------------------------------------------------------------------------
// Sampler
// ---------------------------------------------------------------------------

Sampler::Sampler(TextureCacheRuntime& runtime, const Tegra::Texture::TSCEntry& tsc) {
    ScopedAutoreleasePool pool;

    MTL::SamplerDescriptor* descriptor = MTL::SamplerDescriptor::alloc()->init();
    // Needed before a sampler can be referenced from an argument buffer, only a
    // sampler made with this flag has a usable gpuResourceID. Without it the
    // shader samples through an invalid handle and reads black instead of
    // failing.
    descriptor->setSupportArgumentBuffers(true);
    descriptor->setMinFilter(SamplerFilter(tsc.min_filter));
    descriptor->setMagFilter(SamplerFilter(tsc.mag_filter));
    descriptor->setMipFilter(SamplerMipFilter(tsc.mipmap_filter));
    // The MAGNIFICATION filter decides how WrapMode::Clamp resolves, matching
    // Vulkan. Passing the minification filter picks the wrong address mode for
    // any sampler whose two filters differ.
    descriptor->setSAddressMode(WrapMode(runtime.device, tsc.wrap_u, tsc.mag_filter));
    descriptor->setTAddressMode(WrapMode(runtime.device, tsc.wrap_v, tsc.mag_filter));
    descriptor->setRAddressMode(WrapMode(runtime.device, tsc.wrap_p, tsc.mag_filter));
    // Only meaningful with ClampToBorderColor. Metal's default is transparent
    // black, which makes every out-of-bounds shadow lookup fully shadowed.
    descriptor->setBorderColor(BorderColorFor(tsc.BorderColor()));
    // With mipmapping off the guest still leaves its LOD clamps filled in, and
    // honouring them can pick a mip it never meant to sample. Pinned to
    // [0, 0.25] to match Vulkan.
    const bool mipmapped =
        tsc.mipmap_filter != Tegra::Texture::TextureMipmapFilter::None;
    descriptor->setLodMinClamp(mipmapped ? tsc.MinLod() : 0.0f);
    descriptor->setLodMaxClamp(mipmapped ? tsc.MaxLod() : 0.25f);
    // Metal has no sampler LOD bias, MSL takes it as an argument to the sample
    // call instead, so a guest that biases mip selection doesn't get it.
    if (tsc.LodBias() != 0.0f) {
        static std::atomic<u32> reported{0};
        if (reported++ == 0) {
            LOG_DEBUG(Render_Metal,
                      "Sampler LOD bias {} ignored; Metal has no sampler-level bias",
                      tsc.LodBias());
        }
    }
    // Redundant, already set above.
    descriptor->setSupportArgumentBuffers(true);

    if (tsc.depth_compare_enabled != 0) {
        descriptor->setCompareFunction(DepthCompareFunction(tsc.depth_compare_func));
    }

    // Maxwell can ask for the min or max of the footprint instead of a weighted
    // average, which is how min/max depth pyramids get built. Averaging those
    // gives back a depth no surface is at. Mutually exclusive with depth
    // comparison like in Vulkan, comparison wins.
    //
    // Only ever requested, never assumed. Min/max reduction needs device support
    // this hardware doesn't have, and asking anyway doesn't degrade, it makes
    // newSamplerState return NOTHING. The validation layer says so outright
    // ("Use of minimum or maximum reductionMode requires device support"),
    // without it the failure is silent and the caller gets a null sampler. That
    // null skips the argument table bind, so the slot keeps pointing at whatever
    // sampler was there before, and a shader sampling through a stale id page
    // faults, which discards the submission and its neighbours. Weighted average
    // is a small visual compromise, no sampler at all is not.
    const auto reduction = tsc.reduction_filter.Value();
    if (reduction != Tegra::Texture::SamplerReduction::WeightedAverage &&
        !runtime.device.SupportsSamplerReduction()) {
        static std::atomic<u32> unsupported{0};
        if (unsupported++ == 0) {
            LOG_WARNING(Render_Metal,
                        "Sampler reduction mode {} is unsupported on this device; using a "
                        "weighted average",
                        static_cast<u32>(reduction));
        }
    } else if (reduction != Tegra::Texture::SamplerReduction::WeightedAverage) {
        if (tsc.depth_compare_enabled == 0) {
            descriptor->setReductionMode(SamplerReductionMode(reduction));
        } else {
            static std::atomic<u32> reported{0};
            if (reported++ == 0) {
                LOG_DEBUG(Render_Metal,
                          "Sampler asks for reduction mode {} together with depth comparison; "
                          "keeping the comparison",
                          static_cast<u32>(reduction));
            }
        }
    }

    const u32 max_anisotropy =
        std::clamp(static_cast<u32>(tsc.MaxAnisotropy()), 1U, 16U);
    descriptor->setMaxAnisotropy(max_anisotropy);

    has_linear_filtering = tsc.min_filter == Tegra::Texture::TextureFilter::Linear ||
                           tsc.mag_filter == Tegra::Texture::TextureFilter::Linear;
    has_depth_comparison = tsc.depth_compare_enabled != 0;

    sampler = Adopt(runtime.device.GetDevice()->newSamplerState(descriptor));
    if (!sampler) {
        // Never silent. A null sampler isn't bound at all, so the shader keeps
        // sampling through whichever sampler last held that argument table slot.
        // See the reduction-mode note above for how that ends.
        static std::atomic<u32> failed{0};
        const u32 n = failed.fetch_add(1, std::memory_order_relaxed) + 1;
        if (n <= 4 || n % 500 == 0) {
            LOG_ERROR(Render_Metal,
                      "newSamplerState returned nothing ({} so far); the shader will sample "
                      "through a stale argument table slot",
                      n);
        }
    }

    // Fallback carrying the GUEST's own anisotropy. TSCEntry::MaxAnisotropy()
    // already folds the user's forced setting in, but on shadow maps and
    // single/dual-channel data textures that's a wrong result and not extra
    // quality. SamplerFor picks between the two per draw.
    const u32 guest_anisotropy = std::clamp(1U << tsc.max_anisotropy, 1U, 16U);
    if (max_anisotropy > guest_anisotropy) {
        descriptor->setMaxAnisotropy(guest_anisotropy);
        sampler_default_anisotropy =
            Adopt(runtime.device.GetDevice()->newSamplerState(descriptor));
        descriptor->setMaxAnisotropy(max_anisotropy);
    }

    // Variants for pairings the guest can ask for and Metal leaves undefined.
    // Built here because a sampler state is immutable, so picking one at bind
    // time means we had to make it up front.
    if (has_linear_filtering) {
        descriptor->setMinFilter(MTL::SamplerMinMagFilterNearest);
        descriptor->setMagFilter(MTL::SamplerMinMagFilterNearest);
        descriptor->setMipFilter(MTL::SamplerMipFilterNearest);
        sampler_nearest = Adopt(runtime.device.GetDevice()->newSamplerState(descriptor));
        descriptor->setMinFilter(SamplerFilter(tsc.min_filter));
        descriptor->setMagFilter(SamplerFilter(tsc.mag_filter));
        descriptor->setMipFilter(SamplerMipFilter(tsc.mipmap_filter));
    }
    if (has_depth_comparison) {
        descriptor->setCompareFunction(MTL::CompareFunctionNever);
        sampler_no_compare = Adopt(runtime.device.GetDevice()->newSamplerState(descriptor));
    }

    descriptor->release();
}

// ---------------------------------------------------------------------------
// Framebuffer
// ---------------------------------------------------------------------------

Framebuffer::Framebuffer(TextureCacheRuntime& runtime,
                         std::span<ImageView*, NUM_RT> color_buffers, ImageView* depth_buffer,
                         const RenderTargets& key) {
    CreateFramebuffer(runtime, color_buffers, depth_buffer, key.is_rescaled);
    ClampRenderArea(key.size.width, key.size.height);
}

Framebuffer::Framebuffer(TextureCacheRuntime& runtime, ImageView* color_buffer,
                         ImageView* depth_buffer, u32 width, u32 height, bool is_rescaled_)
    : is_rescaled{is_rescaled_} {
    std::array<ImageView*, NUM_RT> color_buffers{};
    color_buffers[0] = color_buffer;
    CreateFramebuffer(runtime, color_buffers, depth_buffer, is_rescaled_);
    ClampRenderArea(width, height);
}

Framebuffer::~Framebuffer() = default;

void Framebuffer::ClampRenderArea(u32 width, u32 height) {
    // Never larger than the smallest attachment. This becomes the pass
    // descriptor's renderTargetWidth/Height, and declaring more than a texture
    // has is out of bounds, Metal won't trim it. It also bounds every draw's
    // scissor, so getting it wrong clips geometry away.
    //
    // Measured from the image VIEWS and not the Metal textures, a view over a
    // mip range doesn't necessarily report that level's dimensions.
    pass_state.render_area_width = (std::min)(width, attachment_width);
    pass_state.render_area_height = (std::min)(height, attachment_height);

    // Deduplicated by the sizes involved, so a mismatch reports once per shape
    // instead of once per pass.
    if (attachment_width < width || attachment_height < height) {
        static std::mutex seen_mutex;
        static std::set<u64> seen;
        const u64 key = (static_cast<u64>(width) << 48) | (static_cast<u64>(height) << 32) |
                        (static_cast<u64>(attachment_width) << 16) |
                        static_cast<u64>(attachment_height);
        bool first = false;
        {
            std::scoped_lock lock{seen_mutex};
            first = seen.insert(key).second;
        }
        if (first) {
            LOG_WARNING(Render_Metal,
                        "Render area clamped: guest asked for {}x{}, attachments resolved "
                        "{}x{} - the difference is clipped, not scaled",
                        width, height, attachment_width, attachment_height);
        }
    }
}

void Framebuffer::CreateFramebuffer(TextureCacheRuntime& runtime,
                                    std::span<ImageView*, NUM_RT> color_buffers,
                                    ImageView* depth_buffer, bool is_rescaled_) {
    is_rescaled = is_rescaled_;
    diag_image_id = color_buffers[0] != nullptr ? color_buffers[0]->image_id.index : 0xFFFFFFFFu;
    pass_state = RenderPassState{};
    attachment_images = {};
    num_attachment_images = 0;
    attachment_width = std::numeric_limits<u32>::max();
    attachment_height = std::numeric_limits<u32>::max();
    // Attachment extents, and how many array layers the pass renders into. The
    // layer count has to be the max over the attachments' view ranges. Left at
    // one, Metal renders every draw into slice zero no matter what the vertex
    // shader writes to [[render_target_array_index]].
    u32 layers = 1;
    const auto fit = [&, this](const ImageView* view) {
        // ImageViewBase::size is the guest's UNSCALED extent while the caller
        // works in scaled coordinates, so a scaled pass has to scale here or the
        // whole pass gets clamped to the 1x extent.
        const u32 view_width = is_rescaled ? runtime.resolution.ScaleUp(view->size.width)
                                           : view->size.width;
        const u32 view_height = is_rescaled ? runtime.resolution.ScaleUp(view->size.height)
                                            : view->size.height;
        attachment_width = (std::min)(attachment_width, view_width);
        attachment_height = (std::min)(attachment_height, view_height);
        layers = (std::max)(layers, static_cast<u32>(view->range.extent.layers));
        // 1 except for volume render targets, the shared cache's layer extent
        // doesn't carry their plane count. See RenderTargetLayers.
        layers = (std::max)(layers, view->RenderTargetLayers());
    };

    u32 num_color = 0;
    // Parent image per colour slot, for the overlap test below. Not part of
    // RenderPassState, we only need it while the pass is being assembled.
    std::array<MTL::Texture*, NUM_RT> attachment_parent{};
    for (u32 index = 0; index < NUM_RT; ++index) {
        ImageView* const view = color_buffers[index];
        if (view == nullptr) {
            continue;
        }
        MTL::Texture* const render_target = view->RenderTarget();
        if (render_target == nullptr) {
            // A view we couldn't build. It MUST stay out of the count, a pass
            // that declares a colour attachment and binds nothing at that index
            // aborts the process from inside MTLCompilerService, with no
            // validation message and no error return.
            //
            // We tried substituting a scratch attachment here so the pass stays
            // complete and reverted it. Letting those draws run again is exactly
            // what the incomplete flag is for, un-guarded the mismatched writes
            // scribble tile memory into the open scene pass and Z-A came back
            // with full-screen vertical striping. That's also why
            // GetScratchAttachment has no callers. Leave it unless a Z-A run
            // says otherwise.
            static std::atomic<u32> reported{0};
            if (reported++ < 4) {
                LOG_WARNING(Render_Metal,
                            "Colour attachment {} has no usable view; leaving it unbound", index);
            }
            pass_state.incomplete = true;
            continue;
        }
        // The same texture MAY show up twice in one pass. The guest can point
        // two render target registers at one surface and we bind both instead of
        // sending one to scratch, MoltenVK does the same on this hardware and
        // renders those passes correctly.
        //
        // Comparing view pointers isn't enough, two attachments can be different
        // MTLTexture objects viewing one image and Metal's validation checks the
        // memory. Compare the parent image plus the level/slice the view renders
        // into.
        MTL::Texture* const parent_image = view->ImageHandle();
        const u32 this_level = view->RenderTargetLevel();
        const u32 this_slice = view->RenderTargetSlice();

        bool aliases_earlier = false;
        for (u32 earlier = 0; earlier < index; ++earlier) {
            const bool same_view = pass_state.color[earlier].texture == render_target;
            const bool same_subresource =
                parent_image != nullptr && attachment_parent[earlier] == parent_image &&
                pass_state.color[earlier].level == this_level &&
                pass_state.color[earlier].slice == this_slice;
            if (!same_view && !same_subresource) {
                continue;
            }
            aliases_earlier = true;
            static std::atomic<u32> reported{0};
            if (reported++ < 4) {
                LOG_DEBUG(Render_Metal,
                          "Colour attachment {} aliases {} ({}x{} fmt={}, {}); both are bound",
                          index, earlier, static_cast<u32>(render_target->width()),
                          static_cast<u32>(render_target->height()),
                          static_cast<u32>(render_target->pixelFormat()),
                          same_view ? "same view" : "same image subresource");
            }
            break;
        }
        // The validation layer aborts on overlapping attachments, which makes it
        // unusable on any title that aliases render targets. Under validation
        // only we drop the duplicate so the run continues. That differs from a
        // normal run and loses that slot's writes, so we say so in the log. Only
        // a TRAILING duplicate can go. A hole in the middle declares an
        // attachment and binds nothing, which aborts inside MTLCompilerService
        // with no message at all.
        if (aliases_earlier && ValidationLayerActive()) {
            const bool is_trailing = [&] {
                for (u32 later = index + 1; later < NUM_RT; ++later) {
                    if (color_buffers[later] != nullptr &&
                        color_buffers[later]->RenderTarget() != nullptr) {
                        return false;
                    }
                }
                return true;
            }();
            if (is_trailing) {
                static std::atomic<u32> reported{0};
                if (reported++ < 4) {
                    LOG_WARNING(Render_Metal,
                                "Validation layer active: dropping aliased colour attachment {} "
                                "so the render pass validates. Rendering WILL differ from a "
                                "normal run - writes to that slot are lost",
                                index);
                }
                continue;
            }
        }
        pass_state.color[index].texture = render_target;
        attachment_parent[index] = parent_image;
        if (parent_image != nullptr) {
            attachment_ranges[num_attachment_images] = view->range;
            attachment_bits[num_attachment_images] = 1u << index;
            attachment_images[num_attachment_images++] = parent_image;
        }
        // Zero except when the target is a whole 3D texture, see
        // ImageView::RenderTargetLevel.
        pass_state.color[index].level = view->RenderTargetLevel();
        pass_state.color[index].slice = view->RenderTargetSlice();
        // Default to preserving contents; the rasterizer overrides this to
        // Clear or DontCare per draw, which is where the tile-memory win is.
        pass_state.color[index].load_action = LoadAction::Load;
        pass_state.color[index].store_action = StoreAction::Store;
        num_color = index + 1;
        pass_state.sample_count = view->Samples();
        fit(view);
    }
    pass_state.num_color = num_color;

    if (depth_buffer != nullptr) {
        pass_state.depth_stencil.texture = depth_buffer->RenderTarget();
        if (pass_state.depth_stencil.texture == nullptr) {
            // Same contract as the colour slots above: a declared depth target
            // whose view failed makes the pass narrower than its pipelines.
            pass_state.incomplete = true;
        }
        if (depth_buffer->ImageHandle() != nullptr) {
            attachment_ranges[num_attachment_images] = depth_buffer->range;
            attachment_bits[num_attachment_images] = Scheduler::DEPTH_ATTACHMENT_BIT;
            attachment_images[num_attachment_images++] = depth_buffer->ImageHandle();
        }
        pass_state.depth_stencil.has_stencil =
            depth_buffer->Format() == MTL::PixelFormatDepth32Float_Stencil8 ||
            depth_buffer->Format() == MTL::PixelFormatDepth24Unorm_Stencil8 ||
            depth_buffer->Format() == MTL::PixelFormatStencil8;
        pass_state.sample_count = depth_buffer->Samples();
        fit(depth_buffer);
    }

    pass_state.num_layers = layers;

}

} // namespace Metal
