// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <string>

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

void WriteGrayscalePgm(const std::filesystem::path& path, VkExtent2D extent,
                       std::span<const u8> pixels) {
    Common::FS::IOFile file{path, Common::FS::FileAccessMode::Write,
                            Common::FS::FileType::BinaryFile};
    if (!file.IsOpen()) {
        return;
    }

    const std::string header =
        "P5\n" + std::to_string(extent.width) + " " + std::to_string(extent.height) + "\n255\n";
    if (file.Write(header) != header.size()) {
        return;
    }

    const size_t expected = static_cast<size_t>(extent.width) * extent.height;
    void(file.Write(pixels.subspan(0, std::min(expected, pixels.size()))));
    void(file.Flush());
}

} // Anonymous namespace

FrameGen::FrameGen(MemoryAllocator& memory_allocator_, Scheduler& scheduler_)
    : memory_allocator{memory_allocator_}, scheduler{scheduler_} {}

FrameGen::~FrameGen() = default;

void FrameGen::Process(const Device& device, Frame* frame) {
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
    if (!mipmaps || built_extent.width != extent.width || built_extent.height != extent.height) {
        Rebuild(device, extent);
    }

    mipmaps->Dispatch(device, scheduler, *frame->image_view, frame_count);
    ++frame_count;

    const bool dump_requested = Settings::values.frame_gen_dump_flow.GetValue();
    if (!dump_requested) {
        dumped = false;
    } else if (!dumped) {
        DumpFlowPyramid(device);
        dumped = true;
    }
}

void FrameGen::Rebuild(const Device& device, VkExtent2D extent) {
    scheduler.Finish();
    mipmaps.emplace(device, memory_allocator, *shaders, extent, LSFG_FLOW_SCALE);
    built_extent = extent;
    frame_count = 0;
}

void FrameGen::DumpFlowPyramid(const Device& device) {
    const std::filesystem::path directory =
        Common::FS::GetEdenPath(Common::FS::EdenPath::LosslessDir) / "debug";
    if (!Common::FS::CreateDirs(directory)) {
        return;
    }

    for (size_t level = 0; level < LSFG_MIP_LEVELS; ++level) {
        const VkExtent2D extent = mipmaps->GetLevelExtent(level);
        const VkDeviceSize size = static_cast<VkDeviceSize>(extent.width) * extent.height;
        vk::Buffer readback = CreateWrappedBuffer(memory_allocator, size, MemoryUsage::Download);

        scheduler.RequestOutsideRenderPassOperationContext();
        scheduler.Record([image = mipmaps->GetLevelImage(level), dst = *readback,
                          extent](vk::CommandBuffer cmdbuf) {
            DownloadColorImage(cmdbuf, image, dst,
                               VkExtent3D{.width = extent.width, .height = extent.height,
                                          .depth = 1});
        });
        scheduler.Finish();

        readback.Invalidate();
        WriteGrayscalePgm(directory / ("flow_mip" + std::to_string(level) + ".pgm"), extent,
                          readback.Mapped());
    }
}

} // namespace Vulkan
