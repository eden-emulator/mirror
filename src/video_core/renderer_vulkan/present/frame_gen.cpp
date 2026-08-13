// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <string>
#include <vector>

#include "common/fs/file.h"
#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "common/settings.h"
#include "video_core/renderer_vulkan/present/frame_gen.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/renderer_vulkan/vk_present_manager.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/vulkan_common/vulkan_device.h"

namespace Vulkan {

namespace {

constexpr f32 LSFG_FLOW_SCALE = 1.0f;
constexpr size_t COLOR_CHANNELS = 4;

bool IsBlueFirst(VkFormat format) {
    return format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB;
}

void WritePortablePixmap(const std::filesystem::path& path, const std::string& magic,
                         VkExtent2D extent, std::span<const u8> pixels) {
    Common::FS::IOFile file{path, Common::FS::FileAccessMode::Write,
                            Common::FS::FileType::BinaryFile};
    if (!file.IsOpen()) {
        return;
    }

    const std::string header = magic + "\n" + std::to_string(extent.width) + " " +
                               std::to_string(extent.height) + "\n255\n";
    if (file.Write(header) != header.size()) {
        return;
    }

    void(file.Write(pixels));
    void(file.Flush());
}

void WriteGrayscalePgm(const std::filesystem::path& path, VkExtent2D extent,
                       std::span<const u8> pixels) {
    const size_t expected = static_cast<size_t>(extent.width) * extent.height;
    WritePortablePixmap(path, "P5", extent, pixels.subspan(0, std::min(expected, pixels.size())));
}

void WriteColorPpm(const std::filesystem::path& path, VkExtent2D extent,
                   std::span<const u8> pixels, bool blue_first) {
    const size_t pixel_count = static_cast<size_t>(extent.width) * extent.height;
    if (pixels.size() < pixel_count * COLOR_CHANNELS) {
        return;
    }

    std::vector<u8> rgb(pixel_count * 3);
    for (size_t i = 0; i < pixel_count; ++i) {
        const u8 first = pixels[i * COLOR_CHANNELS];
        const u8 green = pixels[i * COLOR_CHANNELS + 1];
        const u8 third = pixels[i * COLOR_CHANNELS + 2];
        rgb[i * 3] = blue_first ? third : first;
        rgb[i * 3 + 1] = green;
        rgb[i * 3 + 2] = blue_first ? first : third;
    }

    WritePortablePixmap(path, "P6", extent, rgb);
}

void CopyPresentedFrame(vk::CommandBuffer cmdbuf, VkImage source, LsfgImage& destination,
                        VkExtent2D extent) {
    const auto make_barrier = [](VkImage image, VkAccessFlags src_access, VkAccessFlags dst_access,
                                 VkImageLayout old_layout, VkImageLayout new_layout) {
        return VkImageMemoryBarrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = src_access,
            .dstAccessMask = dst_access,
            .oldLayout = old_layout,
            .newLayout = new_layout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange{
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };
    };

    const std::array before{
        make_barrier(source, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                     VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL),
        make_barrier(destination.Handle(), VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                     destination.Layout(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
    };
    cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT, 0, {}, {}, before);

    const VkImageCopy region{
        .srcSubresource{
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .srcOffset = {},
        .dstSubresource{
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .dstOffset = {},
        .extent = {.width = extent.width, .height = extent.height, .depth = 1},
    };
    cmdbuf.CopyImage(source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, destination.Handle(),
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, region);

    const std::array after{
        make_barrier(source, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL),
        make_barrier(destination.Handle(), VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL),
    };
    cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           0, {}, {}, after);

    destination.SetLayout(VK_IMAGE_LAYOUT_GENERAL);
}

} // Anonymous namespace

FrameGen::FrameGen(MemoryAllocator& memory_allocator_, Scheduler& scheduler_)
    : memory_allocator{memory_allocator_}, scheduler{scheduler_} {}

FrameGen::~FrameGen() = default;

void FrameGen::Process(const Device& device, Frame* frame, VkFormat format) {
    if (unavailable || !Settings::values.frame_gen.GetValue()) {
        return;
    }

    if (!shaders) {
        shaders.emplace(device);
        if (!shaders->IsValid()) {
            unavailable = true;
            return;
        }
    }

    const VkExtent2D extent{.width = frame->width, .height = frame->height};
    if (!chain || built_extent.width != extent.width || built_extent.height != extent.height ||
        built_format != format) {
        Rebuild(device, extent, format);
    }

    const u64 count = frame_count++;

    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([this, source = *frame->image, extent, count](vk::CommandBuffer cmdbuf) {
        CopyPresentedFrame(cmdbuf, source, chain->Input(count), extent);
        chain->Dispatch(cmdbuf, count);
    });

    const bool dump_requested = Settings::values.frame_gen_dump_flow.GetValue();
    if (!dump_requested) {
        dumped = false;
    } else if (!dumped) {
        DumpDebugImages();
        dumped = true;
    }
}

void FrameGen::Rebuild(const Device& device, VkExtent2D extent, VkFormat format) {
    scheduler.Finish();
    chain.reset();
    chain.emplace(device, memory_allocator, *shaders, extent, format, LSFG_FLOW_SCALE);
    built_extent = extent;
    built_format = format;
    frame_count = 0;
}

void FrameGen::DumpDebugImages() {
    const std::filesystem::path directory =
        Common::FS::GetEdenPath(Common::FS::EdenPath::LosslessDir) / "debug";
    if (!Common::FS::CreateDirs(directory)) {
        return;
    }

    const auto readback = [&](LsfgImage& image, VkDeviceSize size) {
        vk::Buffer buffer = CreateWrappedBuffer(memory_allocator, size, MemoryUsage::Download);
        const VkExtent2D extent = image.Extent();

        scheduler.RequestOutsideRenderPassOperationContext();
        scheduler.Record([handle = image.Handle(), dst = *buffer,
                          extent](vk::CommandBuffer cmdbuf) {
            DownloadColorImage(cmdbuf, handle, dst,
                               VkExtent3D{.width = extent.width,
                                          .height = extent.height,
                                          .depth = 1});
        });
        scheduler.Finish();

        buffer.Invalidate();
        return buffer;
    };

    for (size_t level = 0; level < LSFG_MIP_LEVELS; ++level) {
        LsfgImage& image = chain->FlowLevel(level);
        const VkExtent2D extent = image.Extent();
        const VkDeviceSize size = static_cast<VkDeviceSize>(extent.width) * extent.height;
        vk::Buffer buffer = readback(image, size);
        WriteGrayscalePgm(directory / ("flow_mip" + std::to_string(level) + ".pgm"), extent,
                          buffer.Mapped());
    }

    LsfgImage& output = chain->Output();
    const VkExtent2D extent = output.Extent();
    const VkDeviceSize size =
        static_cast<VkDeviceSize>(extent.width) * extent.height * COLOR_CHANNELS;
    vk::Buffer buffer = readback(output, size);
    WriteColorPpm(directory / "generated.ppm", extent, buffer.Mapped(), IsBlueFirst(built_format));
}

} // namespace Vulkan
