// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <span>
#include <string>
#include <vector>

#include "common/fs/file.h"
#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "common/settings.h"
#include "video_core/host_shaders/vulkan_fidelityfx_fsr_vert_spv.h"
#include "video_core/host_shaders/vulkan_present_frag_spv.h"
#include "video_core/renderer_vulkan/present/frame_gen.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/vulkan_common/vulkan_device.h"

namespace Vulkan {

namespace {

constexpr size_t COLOR_CHANNELS = 4;
constexpr u64 LSFG_REQUIRED_FRAMES = 2;
constexpr u32 LSFG_RECURRENCE_FRAMES = 2;

[[nodiscard]] f32 ConfiguredFlowScale() {
    if (Settings::values.frame_gen_flow_scale_auto.GetValue()) {
        return std::clamp(1.0f / Settings::values.resolution_info.up_factor, 0.25f, 1.0f);
    }
    return static_cast<f32>(Settings::values.frame_gen_flow_scale.GetValue()) / 100.0f;
}

bool IsBlueFirst(VkFormat format) {
    return format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB;
}

VkDeviceSize BytesPerTexel(VkFormat format) {
    switch (format) {
    case VK_FORMAT_R8_UNORM:
        return 1;
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        return 8;
    default:
        return COLOR_CHANNELS;
    }
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
    WritePortablePixmap(path, "P5", extent, pixels.subspan(0, (std::min)(expected, pixels.size())));
}

void WriteRaw(const std::filesystem::path& path, std::span<const u8> pixels) {
    Common::FS::IOFile file{path, Common::FS::FileAccessMode::Write,
                            Common::FS::FileType::BinaryFile};
    if (!file.IsOpen()) {
        return;
    }
    void(file.Write(pixels));
    void(file.Flush());
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

VkImageMemoryBarrier2 MakeTransitionBarrier(VkImage image, VkPipelineStageFlags2 src_stage,
                                            VkAccessFlags2 src_access,
                                            VkPipelineStageFlags2 dst_stage,
                                            VkAccessFlags2 dst_access, VkImageLayout old_layout,
                                            VkImageLayout new_layout) {
    return VkImageMemoryBarrier2{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .pNext = nullptr,
        .srcStageMask = src_stage,
        .srcAccessMask = src_access,
        .dstStageMask = dst_stage,
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
}

void DrawSourceFrame(vk::CommandBuffer cmdbuf, VkPipeline pipeline, VkPipelineLayout layout,
                     VkDescriptorSet set, LsfgImage& destination, VkExtent2D extent) {
    const VkImageMemoryBarrier2 before = MakeTransitionBarrier(
        destination.Handle(), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_NONE,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        destination.Layout(), VK_IMAGE_LAYOUT_GENERAL);
    cmdbuf.PipelineBarrier(before);

    const VkViewport viewport{
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<f32>(extent.width),
        .height = static_cast<f32>(extent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    const VkRect2D scissor{
        .offset = {0, 0},
        .extent = extent,
    };
    BeginRendering(cmdbuf, destination.View(), extent, VK_ATTACHMENT_LOAD_OP_DONT_CARE);
    cmdbuf.BindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    cmdbuf.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, set, {});
    cmdbuf.SetViewport(0, viewport);
    cmdbuf.SetScissor(0, scissor);
    cmdbuf.Draw(3, 1, 0, 0);
    cmdbuf.EndRendering();

    const VkImageMemoryBarrier2 after = MakeTransitionBarrier(
        destination.Handle(), VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
    cmdbuf.PipelineBarrier(after);

    destination.SetLayout(VK_IMAGE_LAYOUT_GENERAL);
}

void UpdateSourceSet(const Device& device, VkDescriptorSet set, VkSampler sampler,
                     VkImageView view) {
    const VkDescriptorImageInfo image_info{
        .sampler = sampler,
        .imageView = view,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
    };
    const VkWriteDescriptorSet write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .pNext = nullptr,
        .dstSet = set,
        .dstBinding = 0,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &image_info,
        .pBufferInfo = nullptr,
        .pTexelBufferView = nullptr,
    };
    device.GetLogical().UpdateDescriptorSets(std::array{write}, {});
}

} // Anonymous namespace

FrameGen::FrameGen(MemoryAllocator& memory_allocator_, Scheduler& scheduler_)
    : memory_allocator{memory_allocator_}, scheduler{scheduler_} {}

FrameGen::~FrameGen() = default;

void FrameGen::Process(const Device& device, VkImageView source, VkExtent2D extent) {
    generated = false;

    if (!shaders) {
        shaders.emplace(device);
        CreateInputPass(device);
    }

    if (unavailable || !Settings::values.frame_gen.GetValue()) {
        if (chain) {
            scheduler.Finish();
            chain.reset();
            outputs = {};
        }
        warm_streak = 0;
        return;
    }

    if (!shaders->IsValid()) {
        unavailable = true;
        return;
    }

    if (source == VK_NULL_HANDLE) {
        warm_streak = 0;
        return;
    }

    const f32 flow_scale = ConfiguredFlowScale();
    const size_t generations = Settings::FrameGenMaxGenerations();
    if (!chain || built_extent.width != extent.width || built_extent.height != extent.height ||
        built_flow_scale != flow_scale || built_generations != generations) {
        Rebuild(device, extent, flow_scale, generations);
    }

    const u64 count = frame_count++;
    last_count = count;
    last_generations = (std::min)(plan.generations, built_generations);

    const size_t input_slot = count % INPUT_SLOTS;
    const bool warm = plan.warm && count + 1 >= LSFG_REQUIRED_FRAMES &&
                      scheduler.IsFree(input_ticks[input_slot]);
    warm_streak = warm ? warm_streak + 1 : 0;
    generated = warm && warm_streak >= LSFG_RECURRENCE_FRAMES && plan.generations > 0;

    if (warm) {
        const VkDescriptorSet set = input_sets[input_slot];
        input_ticks[input_slot] = scheduler.CurrentTick();
        UpdateSourceSet(device, set, *input_sampler, source);
        scheduler.RequestOutsideRenderPassOperationContext();
        scheduler.Record([this, set, extent, count](vk::CommandBuffer cmdbuf) {
            DrawSourceFrame(cmdbuf, *input_pipeline, *input_layout, set, chain->Input(count),
                            extent);
            chain->DispatchShared(cmdbuf, count);
        });
    }

    const bool dump_requested = generated && Settings::values.frame_gen_dump_flow.GetValue();
    if (!dump_requested) {
        dumped = false;
    } else if (!dumped) {
        DumpDebugImages(count);
        dumped = true;
    }
}

size_t FrameGen::WantedGenerations(size_t capacity) {
    if (unavailable) {
        plan = {};
        return 0;
    }
    plan = pacer.Plan(capacity);
    return plan.generations;
}

size_t FrameGen::GeneratedFrameCount() const {
    return generated ? last_generations : 0;
}

std::chrono::nanoseconds FrameGen::PaceStep() const {
    const f32 step = plan.interval / static_cast<f32>(last_generations + 1);
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<f32>(step));
}

const LsfgImage& FrameGen::Generate(const Device& device, size_t generation) {
    LsfgImage& output = outputs[generation];
    chain->SetTarget(device, last_generations, generation, output.View());

    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([this, count = last_count, generation_count = last_generations, generation,
                      image = output.Handle(), extent = built_extent](vk::CommandBuffer cmdbuf) {
        chain->DispatchGeneration(cmdbuf, count, generation_count, generation, image, extent);
    });
    return output;
}

void FrameGen::CreateInputPass(const Device& device) {
    input_set_layout =
        CreateWrappedDescriptorSetLayout(device, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER});
    input_layout = CreateWrappedPipelineLayout(device, input_set_layout);
    input_pool = CreateWrappedDescriptorPool(device, INPUT_SLOTS, INPUT_SLOTS);
    std::array<VkDescriptorSetLayout, INPUT_SLOTS> layouts;
    layouts.fill(*input_set_layout);
    input_sets = CreateWrappedDescriptorSets(input_pool, layouts);
    input_vertex_shader = BuildShader(device, VULKAN_FIDELITYFX_FSR_VERT_SPV);
    input_fragment_shader = BuildShader(device, VULKAN_PRESENT_FRAG_SPV);
    input_pipeline = CreateWrappedPipeline(device, LSFG_DEFAULT_FORMAT, input_layout,
                                           std::tie(input_vertex_shader, input_fragment_shader));
    input_sampler = CreateNearestNeighborSampler(device);
}

void FrameGen::Rebuild(const Device& device, VkExtent2D extent, f32 flow_scale,
                       size_t generations) {
    scheduler.Finish();
    chain.reset();

    built_flow_scale = flow_scale;

    chain.emplace(device, memory_allocator, *shaders, extent, LSFG_DEFAULT_FORMAT,
                  built_flow_scale);
    outputs = {};
    for (LsfgImage& output : std::span{outputs}.first(generations)) {
        output = LsfgImage(device, memory_allocator, extent, LSFG_DEFAULT_FORMAT);
    }
    built_extent = extent;
    built_generations = generations;
    frame_count = 0;
    warm_streak = 0;
    generated = false;
}

void FrameGen::DumpDebugImages(u64 count) {
    const std::filesystem::path directory =
        Common::FS::GetEdenPath(Common::FS::EdenPath::LosslessDir) / "debug";
    if (!Common::FS::CreateDirs(directory)) {
        return;
    }

    const auto dump = [&](const std::string& name, LsfgImage& image) {
        const VkExtent2D extent = image.Extent();
        const VkFormat format = image.Format();
        const VkDeviceSize texel_size = BytesPerTexel(format);
        const VkDeviceSize size =
            static_cast<VkDeviceSize>(extent.width) * extent.height * texel_size;

        vk::Buffer buffer = CreateWrappedBuffer(memory_allocator, size, MemoryUsage::Download);

        scheduler.RequestOutsideRenderPassOperationContext();
        scheduler.Record(
            [handle = image.Handle(), dst = *buffer, extent](vk::CommandBuffer cmdbuf) {
                DownloadColorImage(
                    cmdbuf, handle, dst,
                    VkExtent3D{.width = extent.width, .height = extent.height, .depth = 1});
            });
        scheduler.Finish();

        buffer.Invalidate();
        const std::span<u8> mapped = buffer.Mapped();

        if (format == LSFG_FLOW_FORMAT) {
            WriteGrayscalePgm(directory / (name + ".pgm"), extent, mapped);
        } else if (texel_size == COLOR_CHANNELS) {
            WriteColorPpm(directory / (name + ".ppm"), extent, mapped, IsBlueFirst(format));
        } else {
            WriteRaw(directory / (name + "_" + std::to_string(extent.width) + "x" +
                                  std::to_string(extent.height) + ".f16"),
                     mapped.subspan(0, std::min<size_t>(size, mapped.size())));
        }
    };

    dump("in0", chain->Input(0));
    dump("in1", chain->Input(1));

    for (size_t level = 0; level < LSFG_MIP_LEVELS; ++level) {
        dump("flow_mip" + std::to_string(level), chain->FlowLevel(level));
    }
    for (size_t index = 0; index < 2; ++index) {
        dump("alpha0_" + std::to_string(index), chain->AlphaOutput(0, count, index));
        dump("alpha6_" + std::to_string(index), chain->AlphaOutput(LSFG_MIP_LEVELS - 1, count,
                                                                  index));
    }
    for (size_t level = 0; level < LSFG_BETA_OUTPUTS; ++level) {
        dump("beta_" + std::to_string(level), chain->BetaOutput(level));
    }

    dump("gamma0", chain->GammaOutput(0));
    dump("gamma6", chain->GammaOutput(LSFG_MIP_LEVELS - 1));
    dump("delta2_out1", chain->DeltaOutput1(LSFG_DELTA_INSTANCES - 1));
    dump("delta2_out2", chain->DeltaOutput2(LSFG_DELTA_INSTANCES - 1));
}

} // namespace Vulkan
