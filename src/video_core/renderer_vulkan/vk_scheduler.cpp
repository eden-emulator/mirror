// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>
#include <mutex>
#include <thread>
#include <utility>

#include <fmt/format.h>

#include "video_core/renderer_vulkan/vk_query_cache.h"

#include "common/make_unique_for_overwrite.h"
#include "common/settings.h"
#include "common/thread.h"
#include "video_core/gpu_logging/gpu_logging.h"
#include "video_core/renderer_vulkan/vk_command_pool.h"
#include "video_core/renderer_vulkan/vk_graphics_pipeline.h"
#include "video_core/renderer_vulkan/vk_master_semaphore.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_state_tracker.h"
#include "video_core/renderer_vulkan/vk_texture_cache.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {
namespace {
struct BeginRenderingCommand {
    void operator()(vk::CommandBuffer cmdbuf, vk::CommandBuffer) const {
        BeginRendering(cmdbuf, attachments);
    }

    RenderingAttachments attachments;
};
} // Anonymous namespace

void Scheduler::CommandChunk::ExecuteAll(vk::CommandBuffer cmdbuf,
                                         vk::CommandBuffer upload_cmdbuf) {
    auto command = first;
    while (command != nullptr) {
        auto next = command->GetNext();
        command->Execute(cmdbuf, upload_cmdbuf);
        command->~Command();
        command = next;
    }
    submit = false;
    command_offset = 0;
    first = nullptr;
    last = nullptr;
}

Scheduler::Scheduler(const Device& device_, StateTracker& state_tracker_)
    : device{device_}, state_tracker{state_tracker_},
      master_semaphore{std::make_unique<MasterSemaphore>(device)},
      command_pool{std::make_unique<CommandPool>(*master_semaphore, device)} {
    compute_write_barrier = VkMemoryBarrier2{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .pNext = nullptr,
        .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
        .dstStageMask = device.GetBufferConsumerStages() | vk::PIPELINE_STAGE_ATTACHMENTS,
        .dstAccessMask = device.GetBufferConsumerAccess() | vk::ACCESS_ATTACHMENTS,
    };
    renderpass_write_barrier = compute_write_barrier;
    renderpass_write_barrier.srcStageMask = vk::PIPELINE_STAGE_GRAPHICS_SHADERS;
    upload_write_barrier = compute_write_barrier;
    upload_write_barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    upload_write_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    if (device.IsExtTransformFeedbackSupported()) {
        renderpass_write_barrier.srcStageMask |= VK_PIPELINE_STAGE_2_TRANSFORM_FEEDBACK_BIT_EXT;
        renderpass_write_barrier.srcAccessMask |=
            VK_ACCESS_2_TRANSFORM_FEEDBACK_WRITE_BIT_EXT |
            VK_ACCESS_2_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT;
    }

    AcquireNewChunk();
    AllocateWorkerCommandBuffer();
    worker_thread = std::jthread([this](std::stop_token token) { WorkerThread(token); });
}

Scheduler::~Scheduler() = default;

u64 Scheduler::Flush(VkSemaphore signal_semaphore, VkSemaphore wait_semaphore) {
    // When flushing, we only send data to the worker thread; no waiting is necessary.
    return SubmitExecution(signal_semaphore, wait_semaphore);
}

void Scheduler::Finish(VkSemaphore signal_semaphore, VkSemaphore wait_semaphore) {
    // When finishing, we need to wait for the submission to have executed on the device.
    const u64 presubmit_tick = CurrentTick();
    SubmitExecution(signal_semaphore, wait_semaphore);
    Wait(presubmit_tick);
}

void Scheduler::WaitWorker() {
    DispatchWork();

    // Ensure the queue is drained.
    {
        std::unique_lock ql{queue_mutex};
        event_cv.wait(ql, [this] { return work_queue.empty(); });
    }

    // Now wait for execution to finish.
    std::scoped_lock el{execution_mutex};
}

void Scheduler::DispatchWork() {
    if (chunk && !chunk->Empty()) {
        {
            std::scoped_lock ql{queue_mutex};
            work_queue.push(std::move(chunk));
        }
        event_cv.notify_all();
        recorded_attachments = nullptr;
        AcquireNewChunk();
    }
}

void Scheduler::BeginRenderPassImpl(const Framebuffer* framebuffer) {
    const RenderingAttachments& attachments = framebuffer->Attachments();
    PublishComputeWrites();
    state.framebuffer_id = framebuffer->Id();
    ++renderpass_serial;
    renderpass_pristine = true;
    attachments_touched = 0;
    attachments_written = 0;

    if (GPU::Logging::IsActive() && Settings::values.gpu_log_vulkan_calls.GetValue()) {
        const VkExtent2D render_area = attachments.render_area.extent;
        const std::string render_pass_info =
            fmt::format("renderArea={}x{}, numImages={}", render_area.width, render_area.height,
                        framebuffer->NumImages());
        GPU::Logging::GPULogger::GetInstance().LogRenderPassBegin(render_pass_info);
    }

    BeginRenderingCommand command{attachments};
    BeginRenderingCommand* recorded = chunk->Record(command);
    if (recorded == nullptr) {
        DispatchWork();
        recorded = chunk->Record(command);
    }
    recorded_attachments = &recorded->attachments;
    num_renderpass_images = framebuffer->NumImages();
    renderpass_images = framebuffer->Images();
    renderpass_image_ranges = framebuffer->ImageRanges();
    framebuffer->MarkResolveShadowsUpToDate();
}

void Scheduler::RequestRenderpass(const Framebuffer* framebuffer, u32 touched, u32 written) {
    if (framebuffer->Id() != state.framebuffer_id) {
        EndRenderPass();
        BeginRenderPassImpl(framebuffer);
    }
    renderpass_pristine = false;
    attachments_touched |= touched;
    attachments_written |= written;
}

bool Scheduler::OverrideLoadOps(const Framebuffer* framebuffer, u32 attachments,
                                VkAttachmentLoadOp load_op, const VkClearValue& value) {
    if (framebuffer->Id() != state.framebuffer_id) {
        EndRenderPass();
        BeginRenderPassImpl(framebuffer);
    }
    if (!renderpass_pristine || recorded_attachments == nullptr) {
        return false;
    }
    const auto set_load_op = [&](VkRenderingAttachmentInfo& attachment, bool discards_msaa) {
        attachment.loadOp = load_op;
        attachment.clearValue = value;
        if (discards_msaa && load_op == VK_ATTACHMENT_LOAD_OP_CLEAR) {
            attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        }
    };
    for (u32 slot = 0; slot < recorded_attachments->num_colors; ++slot) {
        if ((attachments & (1u << slot)) != 0) {
            set_load_op(recorded_attachments->colors[slot], framebuffer->DiscardsMsaaColor());
        }
    }
    if ((attachments & DEPTH_ATTACHMENT_BIT) != 0) {
        set_load_op(recorded_attachments->depth, framebuffer->DiscardsMsaaDepthStencil());
    }
    if ((attachments & STENCIL_ATTACHMENT_BIT) != 0) {
        set_load_op(recorded_attachments->stencil, framebuffer->DiscardsMsaaDepthStencil());
    }
    attachments_touched |= attachments;
    attachments_written |= attachments;
    return true;
}

void Scheduler::RequestOutsideRenderPassOperationContext() {
    EndRenderPass();
    PublishComputeWrites();
}

void Scheduler::RequestComputeDispatchContext() {
    EndRenderPass();
    compute_writes = true;
}

void Scheduler::RelaxAttachmentOps(RenderingAttachments& attachments) const {
    const bool load_op_none = device.IsLoadOpNoneSupported();
    const bool drop_unused = device.IsExtDynamicRenderingUnusedAttachmentsSupported();
    const auto relax = [&](VkRenderingAttachmentInfo& attachment, u32 bit) {
        if (attachment.imageView == VK_NULL_HANDLE) {
            return true;
        }
        if (attachment.resolveMode != VK_RESOLVE_MODE_NONE ||
            attachment.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR ||
            attachment.storeOp != VK_ATTACHMENT_STORE_OP_STORE ||
            (attachments_written & bit) != 0) {
            return false;
        }
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_NONE;
        return (attachments_touched & bit) == 0;
    };
    for (u32 slot = 0; slot < attachments.num_colors; ++slot) {
        VkRenderingAttachmentInfo& color = attachments.colors[slot];
        if (!relax(color, 1u << slot)) {
            continue;
        }
        if (drop_unused) {
            color.imageView = VK_NULL_HANDLE;
        } else if (load_op_none) {
            color.loadOp = VK_ATTACHMENT_LOAD_OP_NONE;
        }
    }
    const bool depth_unused = relax(attachments.depth, DEPTH_ATTACHMENT_BIT);
    const bool stencil_unused = relax(attachments.stencil, STENCIL_ATTACHMENT_BIT);
    if (drop_unused && depth_unused && stencil_unused) {
        attachments.depth.imageView = VK_NULL_HANDLE;
        attachments.stencil.imageView = VK_NULL_HANDLE;
    }
}

void Scheduler::PublishComputeWrites() {
    if (!std::exchange(compute_writes, false)) {
        return;
    }
    Record([barrier = &compute_write_barrier](vk::CommandBuffer cmdbuf) {
        cmdbuf.PipelineBarrier(*barrier);
    });
}

bool Scheduler::UpdateGraphicsPipeline(GraphicsPipeline* pipeline) {
    if (state.graphics_pipeline == pipeline) {
        if (pipeline && pipeline->UsesExtendedDynamicState() &&
            state.needs_state_enable_refresh) {
            state_tracker.InvalidateStateEnableFlag();
            state.needs_state_enable_refresh = false;
        }
        return false;
    }

    state.graphics_pipeline = pipeline;

    if (!pipeline) {
        return true;
    }

    if (!pipeline->UsesExtendedDynamicState()) {
        state.needs_state_enable_refresh = true;
    } else if (state.needs_state_enable_refresh) {
        state_tracker.InvalidateStateEnableFlag();
        state.needs_state_enable_refresh = false;
    }

    return true;
}

bool Scheduler::UpdateRescaling(bool is_rescaling) {
    if (state.rescaling_defined && is_rescaling == state.is_rescaling) {
        return false;
    }
    state.rescaling_defined = true;
    state.is_rescaling = is_rescaling;
    return true;
}

bool Scheduler::UpdateDescriptorBufferChunk(u32 descriptor_chunk) {
    if (state.descriptor_buffer_bound && descriptor_chunk == state.descriptor_buffer_chunk) {
        return false;
    }
    state.descriptor_buffer_bound = true;
    state.descriptor_buffer_chunk = descriptor_chunk;
    return true;
}

void Scheduler::WorkerThread(std::stop_token stop_token) {
    Common::SetCurrentThreadName("VulkanWorker");
    Common::SetCurrentThreadPriority(Common::ThreadPriority::Critical);
    Common::SetCurrentThreadToPerformanceCores();

    const auto TryPopQueue{[this](auto& work) -> bool {
        if (work_queue.empty()) {
            return false;
        }

        work = std::move(work_queue.front());
        work_queue.pop();
        event_cv.notify_all();
        return true;
    }};

    while (!stop_token.stop_requested()) {
        std::unique_ptr<CommandChunk> work;

        {
            std::unique_lock lk{queue_mutex};

            // Wait for work.
            event_cv.wait(lk, stop_token, [&] { return TryPopQueue(work); });

            // If we've been asked to stop, we're done.
            if (stop_token.stop_requested()) {
                return;
            }

            // Exchange lock ownership so that we take the execution lock before
            // the queue lock goes out of scope. This allows us to force execution
            // to complete in the next step.
            void(std::exchange(lk, std::unique_lock{execution_mutex}));

            // Perform the work, tracking whether the chunk was a submission
            // before executing.
            const bool has_submit = work->HasSubmit();
            work->ExecuteAll(current_cmdbuf, current_upload_cmdbuf);

            // If the chunk was a submission, reallocate the command buffer.
            if (has_submit) {
                AllocateWorkerCommandBuffer();
            }
        }

        {
            std::scoped_lock rl{reserve_mutex};

            // Recycle the chunk back to the reserve.
            chunk_reserve.emplace_back(std::move(work));
        }
    }
}

void Scheduler::AllocateWorkerCommandBuffer() {
    current_cmdbuf = vk::CommandBuffer(command_pool->Commit(), device.GetDispatchLoader());
    current_cmdbuf.Begin({
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr,
    });
    current_upload_cmdbuf = vk::CommandBuffer(command_pool->Commit(), device.GetDispatchLoader());
    current_upload_cmdbuf.Begin({
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr,
    });
}

u64 Scheduler::SubmitExecution(VkSemaphore signal_semaphore, VkSemaphore wait_semaphore) {
    EndPendingOperations();
    InvalidateState();

    const u64 signal_value = master_semaphore->NextTick();
    RecordWithUploadBuffer([signal_semaphore, wait_semaphore, signal_value,
                            this](vk::CommandBuffer cmdbuf, vk::CommandBuffer upload_cmdbuf) {
        upload_cmdbuf.PipelineBarrier(upload_write_barrier);
        upload_cmdbuf.End();
        cmdbuf.End();

        if (on_submit) {
            on_submit();
        }

        std::scoped_lock lock{submit_mutex};
        switch (const VkResult result = master_semaphore->SubmitQueue(
                    cmdbuf, upload_cmdbuf, signal_semaphore, wait_semaphore, signal_value)) {
        case VK_SUCCESS:
            // Log successful queue submission
            if (GPU::Logging::IsActive() &&
                Settings::values.gpu_log_vulkan_calls.GetValue()) {
                GPU::Logging::GPULogger::GetInstance().LogVulkanCall(
                    "vkQueueSubmit2", "", VK_SUCCESS);
            }
            break;
        case VK_ERROR_DEVICE_LOST:
            device.ReportLoss();
            [[fallthrough]];
        default:
            vk::Check(result);
            break;
        }
    });
    chunk->MarkSubmit();
    DispatchWork();
    return signal_value;
}

void Scheduler::InvalidateState() {
    state.graphics_pipeline = nullptr;
    state.rescaling_defined = false;
    state.descriptor_buffer_bound = false;
    state_tracker.InvalidateCommandBufferState();
}

void Scheduler::EndPendingOperations() {
    query_cache->CounterReset(VideoCommon::QueryType::ZPassPixelCount64);
    EndRenderPass();
}

void Scheduler::EndRenderPass()
    {
        if (state.framebuffer_id == 0) {
            return;
        }
        if (recorded_attachments != nullptr) {
            RelaxAttachmentOps(*recorded_attachments);
            recorded_attachments = nullptr;
        }

        query_cache->CounterClose(VideoCommon::QueryType::StreamingByteCount);

        // Log render pass end
        if (GPU::Logging::IsActive() &&
            Settings::values.gpu_log_vulkan_calls.GetValue()) {
            GPU::Logging::GPULogger::GetInstance().LogRenderPassEnd();
        }

        query_cache->CounterEnable(VideoCommon::QueryType::ZPassPixelCount64, false);
        query_cache->NotifySegment(false);

        Record([num_images = num_renderpass_images,
                       images = renderpass_images,
                       ranges = renderpass_image_ranges,
                       write_barrier = &renderpass_write_barrier,
                       num_memory_barriers =
                           static_cast<size_t>(std::exchange(renderpass_writes, false))](
                          vk::CommandBuffer cmdbuf) {
            std::array<VkImageMemoryBarrier2, 9> barriers;
            for (size_t i = 0; i < num_images; ++i) {
                const VkImageSubresourceRange& range = ranges[i];
                const bool is_color = (range.aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) != 0;
                const bool is_depth_stencil = (range.aspectMask
                                              & (VK_IMAGE_ASPECT_DEPTH_BIT
                                                 | VK_IMAGE_ASPECT_STENCIL_BIT)) !=0;

                VkAccessFlags2 src_access = 0;

                if (is_color)
                    src_access |= VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                else if (is_depth_stencil)
                    src_access |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                else
                    src_access |= VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
                                  | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

                barriers[i] = VkImageMemoryBarrier2{
                        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                        .pNext = nullptr,
                        .srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                                        | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT
                                        | VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                        .srcAccessMask = src_access,
                        .dstStageMask = vk::PIPELINE_STAGE_IMAGE_USERS,
                        .dstAccessMask = vk::ACCESS_IMAGE_USERS,
                        .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
                        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .image = images[i],
                        .subresourceRange = range,
                };
            }
            cmdbuf.EndRendering();
            cmdbuf.PipelineBarrier(0, vk::Span(write_barrier, num_memory_barriers), {},
                                   vk::Span(barriers.data(), num_images));
        });

        state.framebuffer_id = 0;
        num_renderpass_images = 0;
    }


void Scheduler::AcquireNewChunk() {
    std::scoped_lock rl{reserve_mutex};

    if (chunk_reserve.empty()) {
        // If we don't have anything reserved, we need to make a new chunk.
        chunk = Common::make_unique_for_overwrite<CommandChunk>();
    } else {
        // Otherwise, we can just take from the reserve.
        chunk = std::move(chunk_reserve.back());
        chunk_reserve.pop_back();
    }
}

} // namespace Vulkan
