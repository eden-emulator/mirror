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

    vk::SetDeletionTimeline(master_semaphore->CurrentTick());
    AcquireNewChunk();
    AllocateWorkerCommandBuffer();
    worker_thread = std::jthread([this](std::stop_token token) { WorkerThread(token); });
}

Scheduler::~Scheduler() = default;

u64 Scheduler::Flush(VkSemaphore signal_semaphore, VkSemaphore wait_semaphore) {
    // When flushing, we only send data to the worker thread; no waiting is necessary.
    const u64 signal_value = SubmitExecution(signal_semaphore, wait_semaphore);
    AllocateNewContext();
    return signal_value;
}

void Scheduler::Finish(VkSemaphore signal_semaphore, VkSemaphore wait_semaphore) {
    // When finishing, we need to wait for the submission to have executed on the device.
    const u64 presubmit_tick = CurrentTick();
    SubmitExecution(signal_semaphore, wait_semaphore);
    Wait(presubmit_tick);
    AllocateNewContext();
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
        AcquireNewChunk();
    }
}

void Scheduler::BeginDynamicRendering(const Framebuffer* framebuffer, const DeferredClear* clear) {
    const VkExtent2D render_area = framebuffer->RenderArea();
    std::array<VkImageView, 9> attachment_views{};
    const auto& color_views = framebuffer->ColorAttachments();
    for (size_t index = 0; index < color_views.size(); ++index) {
        attachment_views[index] = color_views[index];
    }
    attachment_views[8] = framebuffer->DepthAttachment();
    state.renderpass = VkRenderPass{};
    state.framebuffer = VkFramebuffer{};
    state.attachment_views = attachment_views;
    state.color_resolve_views = framebuffer->ColorResolveAttachments();
    state.color_resolve_modes = framebuffer->ColorResolveModes();
    state.depth_resolve_view = framebuffer->DepthResolveAttachment();
    state.depth_resolve_mode = framebuffer->DepthResolveMode();
    state.stencil_resolve_mode = framebuffer->StencilResolveMode();
    state.discards_msaa_color = framebuffer->DiscardsMsaaColor();
    state.discards_msaa_depth = framebuffer->DiscardsMsaaDepth();
    state.render_area = render_area;
    state.num_color = framebuffer->NumColorAttachments();
    state.has_depth = framebuffer->HasAspectDepthBit();
    state.has_stencil = framebuffer->HasAspectStencilBit();
    state.layer_count = framebuffer->NumLayers();
    state.rendering = true;

    if (GPU::Logging::IsActive() && Settings::values.gpu_log_vulkan_calls.GetValue()) {
        const std::string render_pass_info =
            fmt::format("renderArea={}x{}, numImages={}", render_area.width, render_area.height,
                        framebuffer->NumImages());
        GPU::Logging::GPULogger::GetInstance().LogRenderPassBegin(render_pass_info);
    }

    pending_begin = true;
    has_pending_begin_clear = clear != nullptr;
    pending_begin_clear = clear != nullptr ? *clear : DeferredClear{};
    num_renderpass_images = framebuffer->NumImages();
    renderpass_images = framebuffer->Images();
    renderpass_image_ranges = framebuffer->ImageRanges();
}

void Scheduler::FlushPendingRenderPass() {
    if (!pending_begin) {
        return;
    }

    pending_begin = false;
    const bool had_clear = has_pending_begin_clear;
    has_pending_begin_clear = false;
    RecordDynamicBegin(had_clear ? &pending_begin_clear : nullptr);
}

bool Scheduler::RetractUnrecordedRenderPass() {
    if (!pending_begin) {
        return false;
    }
    pending_begin = false;
    if (has_pending_begin_clear) {
        deferred_clear = pending_begin_clear;
        has_pending_begin_clear = false;
    }
    state.renderpass = VkRenderPass{};
    state.framebuffer = VkFramebuffer{};
    state.attachment_views = {};
    state.rendering = false;
    state.uses_transform_feedback = false;
    num_renderpass_images = 0;
    return true;
}

void Scheduler::BeginRenderPassImpl(const Framebuffer* framebuffer, VkRenderPass renderpass,
                                    const VkClearValue* clear_values, u32 clear_value_count) {
    if (device.IsKhrDynamicRenderingSupported()) {
        BeginDynamicRendering(framebuffer, nullptr);
        return;
    }
    const VkExtent2D render_area = framebuffer->RenderArea();
    const VkFramebuffer framebuffer_handle = framebuffer->Handle();
    state.renderpass = renderpass;
    state.framebuffer = framebuffer_handle;
    state.render_area = render_area;
    state.rendering = true;

    if (GPU::Logging::IsActive() && Settings::values.gpu_log_vulkan_calls.GetValue()) {
        const std::string render_pass_info =
            fmt::format("renderArea={}x{}, numImages={}", render_area.width, render_area.height,
                        framebuffer->NumImages());
        GPU::Logging::GPULogger::GetInstance().LogRenderPassBegin(render_pass_info);
    }

    std::array<VkClearValue, 9> values{};
    for (u32 i = 0; i < clear_value_count && i < values.size(); ++i) {
        values[i] = clear_values[i];
    }
    Record([renderpass, framebuffer_handle, render_area, values, clear_value_count](
               vk::CommandBuffer cmdbuf) {
        const VkRenderPassBeginInfo renderpass_bi{
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .pNext = nullptr,
            .renderPass = renderpass,
            .framebuffer = framebuffer_handle,
            .renderArea =
                {
                    .offset = {.x = 0, .y = 0},
                    .extent = render_area,
                },
            .clearValueCount = clear_value_count,
            .pClearValues = clear_value_count != 0 ? values.data() : nullptr,
        };
        cmdbuf.BeginRenderPass(renderpass_bi, VK_SUBPASS_CONTENTS_INLINE);
    });
    num_renderpass_images = framebuffer->NumImages();
    renderpass_images = framebuffer->Images();
    renderpass_image_ranges = framebuffer->ImageRanges();
}

void Scheduler::RealizeDeferredClear() {
    if (deferred_clear.framebuffer == nullptr) {
        return;
    }
    const DeferredClear dc = deferred_clear;
    deferred_clear = {};

    if (device.IsKhrDynamicRenderingSupported()) {
        EndRenderPass();
        BeginDynamicRendering(dc.framebuffer, &dc);
        return;
    }

    std::array<VkClearValue, 9> clear_values{};
    u32 count = 0;
    const RenderPassKey& base = dc.framebuffer->RenderPassKeyBase();
    for (u32 slot = 0; slot < 8; ++slot) {
        if (base.color_formats[slot] == VideoCore::Surface::PixelFormat::Invalid) {
            continue;
        }
        clear_values[count++] = dc.color_values[slot];
    }
    if (base.depth_format != VideoCore::Surface::PixelFormat::Invalid) {
        clear_values[count++] = dc.depth_stencil_value;
    }
    const u32 color_discard_mask =
        dc.framebuffer->DiscardsMsaaColor() ? dc.color_clear_mask : 0u;
    const VkRenderPass renderpass = dc.framebuffer->RenderPassVariant(
        dc.color_clear_mask, dc.depth_stencil, color_discard_mask);
    EndRenderPass();
    BeginRenderPassImpl(dc.framebuffer, renderpass, clear_values.data(), count);
}

bool Scheduler::DeferColorClear(const Framebuffer* framebuffer, u32 rt_slot,
                                const VkClearValue& value) {
    if (IsRenderPassActive()) {
        return false;
    }
    if (deferred_clear.framebuffer != nullptr && deferred_clear.framebuffer != framebuffer) {
        RealizeDeferredClear();
        EndRenderPass();
    }
    deferred_clear.framebuffer = framebuffer;
    deferred_clear.color_clear_mask |= 1u << rt_slot;
    deferred_clear.color_values[rt_slot] = value;
    return true;
}

bool Scheduler::DeferDepthStencilClear(const Framebuffer* framebuffer, const VkClearValue& value) {
    if (IsRenderPassActive()) {
        return false;
    }
    if (deferred_clear.framebuffer != nullptr && deferred_clear.framebuffer != framebuffer) {
        RealizeDeferredClear();
        EndRenderPass();
    }
    deferred_clear.framebuffer = framebuffer;
    deferred_clear.depth_stencil = true;
    deferred_clear.depth_stencil_value = value;
    return true;
}

void Scheduler::RequestRenderpass(const Framebuffer* framebuffer) {
    if (deferred_clear.framebuffer == framebuffer) {
        RealizeDeferredClear();
        return;
    }
    const VkExtent2D render_area = framebuffer->RenderArea();
    if (device.IsKhrDynamicRenderingSupported()) {
        std::array<VkImageView, 9> attachment_views{};
        const auto& color_views = framebuffer->ColorAttachments();
        for (size_t index = 0; index < color_views.size(); ++index) {
            attachment_views[index] = color_views[index];
        }
        attachment_views[8] = framebuffer->DepthAttachment();
        if (state.rendering && attachment_views == state.attachment_views &&
            render_area.width == state.render_area.width &&
            render_area.height == state.render_area.height) {
            return;
        }
        EndRenderPass();
        BeginDynamicRendering(framebuffer, nullptr);
        return;
    }
    const VkRenderPass renderpass = framebuffer->RenderPass();
    const VkFramebuffer framebuffer_handle = framebuffer->Handle();
    if (renderpass == state.renderpass && framebuffer_handle == state.framebuffer &&
        render_area.width == state.render_area.width &&
        render_area.height == state.render_area.height) {
        return;
    }
    // Ends any active pass and realizes a deferred clear
    EndRenderPass();
    BeginRenderPassImpl(framebuffer, renderpass, nullptr, 0);
}

void Scheduler::RequestOutsideRenderPassOperationContext() {
    EndRenderPass();
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

void Scheduler::WorkerThread(std::stop_token stop_token) {
    Common::SetCurrentThreadName("VulkanWorker");

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
            std::exchange(lk, std::unique_lock{execution_mutex});

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
    vk::SetDeletionTimeline(master_semaphore->CurrentTick());
    RecordWithUploadBuffer([signal_semaphore, wait_semaphore, signal_value,
                            this](vk::CommandBuffer cmdbuf, vk::CommandBuffer upload_cmdbuf) {
        static constexpr VkMemoryBarrier WRITE_BARRIER{
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
        };
        upload_cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, WRITE_BARRIER);
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
                    "vkQueueSubmit", "", VK_SUCCESS);
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

void Scheduler::AllocateNewContext() {
    // Enable counters once again. These are disabled when a command buffer is finished.
}

void Scheduler::InvalidateState() {
    state.graphics_pipeline = nullptr;
    state.rescaling_defined = false;
    state_tracker.InvalidateCommandBufferState();
}

namespace {
/// The part of a dynamic rendering begin that every pass needs.
struct DynamicRenderingBase {
    std::array<VkImageView, 9> views;
    VkExtent2D render_area;
    u32 num_color;
    u32 layers;
    VkImageView ds_resolve_view;
    VkResolveModeFlagBits depth_resolve_mode;
    VkResolveModeFlagBits stencil_resolve_mode;
    bool has_depth;
    bool has_stencil;
    bool ds_discard;
};

/// The part only a pass that actually clears something needs.
struct DynamicRenderingClears {
    std::array<VkClearValue, 8> color_values;
    VkClearValue ds_value;
    u32 color_clear_mask;
    u32 color_discard_mask;
    bool ds_clear;
};

/// Builds the rendering info and begins the instance. `resolve_views`/`resolve_modes` and `clears`
/// are null for passes with no colour resolve targets and no clears, so those tables never have to
/// be copied into the recorded command.
void IssueBeginRendering(vk::CommandBuffer cmdbuf, const DynamicRenderingBase& base,
                         const std::array<VkImageView, 8>* resolve_views,
                         const std::array<VkResolveModeFlagBits, 8>* resolve_modes,
                         const DynamicRenderingClears* clears) {
    std::array<VkRenderingAttachmentInfo, VideoCommon::NUM_RT> color_infos{};
    for (u32 index = 0; index < base.num_color; ++index) {
        const bool clear_slot =
            clears != nullptr && ((clears->color_clear_mask >> index) & 1u) != 0;
        const VkImageView resolve_view =
            resolve_views != nullptr ? (*resolve_views)[index] : VK_NULL_HANDLE;
        const bool has_resolve = resolve_view != VK_NULL_HANDLE;
        const bool discard_slot =
            has_resolve && clears != nullptr && ((clears->color_discard_mask >> index) & 1u) != 0;
        color_infos[index] = VkRenderingAttachmentInfo{
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .pNext = nullptr,
            .imageView = base.views[index],
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
            .resolveMode = has_resolve ? (*resolve_modes)[index] : VK_RESOLVE_MODE_NONE,
            .resolveImageView = resolve_view,
            .resolveImageLayout =
                has_resolve ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
            .loadOp = clear_slot ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = discard_slot ? VK_ATTACHMENT_STORE_OP_DONT_CARE
                                    : VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = clear_slot ? clears->color_values[index] : VkClearValue{},
        };
    }
    const bool ds_clear = clears != nullptr && clears->ds_clear;
    const VkClearValue ds_clear_value = clears != nullptr ? clears->ds_value : VkClearValue{};
    const bool has_ds_resolve = base.ds_resolve_view != VK_NULL_HANDLE;
    const VkAttachmentLoadOp ds_load_op = ds_clear ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                          : base.ds_discard ? VK_ATTACHMENT_LOAD_OP_DONT_CARE
                                                            : VK_ATTACHMENT_LOAD_OP_LOAD;
    const VkAttachmentStoreOp ds_store_op =
        base.ds_discard ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
    const VkRenderingAttachmentInfo depth_info{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = nullptr,
        .imageView = base.views[8],
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        .resolveMode = has_ds_resolve ? base.depth_resolve_mode : VK_RESOLVE_MODE_NONE,
        .resolveImageView = has_ds_resolve ? base.ds_resolve_view : VK_NULL_HANDLE,
        .resolveImageLayout = has_ds_resolve ? VK_IMAGE_LAYOUT_GENERAL
                                             : VK_IMAGE_LAYOUT_UNDEFINED,
        .loadOp = ds_load_op,
        .storeOp = ds_store_op,
        .clearValue = ds_clear ? ds_clear_value : VkClearValue{},
    };
    // Stencil gets its own struct because its resolve mode may differ from depth's.
    const VkRenderingAttachmentInfo stencil_info{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = nullptr,
        .imageView = base.views[8],
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        .resolveMode = has_ds_resolve ? base.stencil_resolve_mode : VK_RESOLVE_MODE_NONE,
        .resolveImageView = has_ds_resolve ? base.ds_resolve_view : VK_NULL_HANDLE,
        .resolveImageLayout = has_ds_resolve ? VK_IMAGE_LAYOUT_GENERAL
                                             : VK_IMAGE_LAYOUT_UNDEFINED,
        .loadOp = ds_load_op,
        .storeOp = ds_store_op,
        .clearValue = ds_clear ? ds_clear_value : VkClearValue{},
    };
    const VkRenderingInfo rendering_info{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .pNext = nullptr,
        .flags = 0,
        .renderArea =
            {
                .offset = {.x = 0, .y = 0},
                .extent = base.render_area,
            },
        .layerCount = base.layers,
        .viewMask = 0,
        .colorAttachmentCount = base.num_color,
        .pColorAttachments = color_infos.data(),
        .pDepthAttachment = base.has_depth ? &depth_info : nullptr,
        .pStencilAttachment = base.has_stencil ? &stencil_info : nullptr,
    };
    cmdbuf.BeginRendering(rendering_info);
}
} // Anonymous namespace

void Scheduler::RecordDynamicBegin(const DeferredClear* clear) {
    const DynamicRenderingBase base{
        .views = state.attachment_views,
        .render_area = state.render_area,
        .num_color = state.num_color,
        .layers = state.layer_count,
        .ds_resolve_view = state.depth_resolve_view,
        .depth_resolve_mode = state.depth_resolve_mode,
        .stencil_resolve_mode = state.stencil_resolve_mode,
        .has_depth = state.has_depth,
        .has_stencil = state.has_stencil,
        .ds_discard = state.discards_msaa_depth,
    };
    bool has_color_resolve = false;
    for (const VkImageView resolve_view : state.color_resolve_views) {
        has_color_resolve = has_color_resolve || resolve_view != VK_NULL_HANDLE;
    }
    if (clear == nullptr && !has_color_resolve) {
        // Common case. Every byte captured here is copied into the command chunk on every single
        // pass begin, and the clear values plus the two resolve tables are two thirds of them.
        Record([base](vk::CommandBuffer cmdbuf) {
            IssueBeginRendering(cmdbuf, base, nullptr, nullptr, nullptr);
        });
        return;
    }
    const DynamicRenderingClears clears{
        .color_values = clear != nullptr ? clear->color_values : std::array<VkClearValue, 8>{},
        .ds_value = clear != nullptr ? clear->depth_stencil_value : VkClearValue{},
        .color_clear_mask = clear != nullptr ? clear->color_clear_mask : 0u,
        .color_discard_mask =
            clear != nullptr && state.discards_msaa_color ? clear->color_clear_mask : 0u,
        .ds_clear = clear != nullptr && clear->depth_stencil,
    };
    Record([base, resolve_views = state.color_resolve_views,
            resolve_modes = state.color_resolve_modes, clears](vk::CommandBuffer cmdbuf) {
        IssueBeginRendering(cmdbuf, base, &resolve_views, &resolve_modes, &clears);
    });
}

void Scheduler::EndPendingOperations() {
    query_cache->CounterReset(VideoCommon::QueryType::ZPassPixelCount64);
    EndRenderPass();
}

void Scheduler::EndRenderPass()
    {
        RealizeDeferredClear();
        if (query_cache) {
            query_cache->NotifySegment(false);
        }
        if (!state.rendering) {
            return;
        }

        query_cache->CounterClose(VideoCommon::QueryType::StreamingByteCount);

        // Log render pass end
        if (GPU::Logging::IsActive() &&
            Settings::values.gpu_log_vulkan_calls.GetValue()) {
            GPU::Logging::GPULogger::GetInstance().LogRenderPassEnd();
        }

        query_cache->CounterEnable(VideoCommon::QueryType::ZPassPixelCount64, false);

        if (pending_begin) {
            if (!has_pending_begin_clear) {
                RetractUnrecordedRenderPass();
                return;
            }
            FlushPendingRenderPass();
        }

        Record([num_images = num_renderpass_images,
                       images = renderpass_images,
                       ranges = renderpass_image_ranges,
                       has_transform_feedback = state.uses_transform_feedback,
                       dynamic_rendering = device.IsKhrDynamicRenderingSupported()](
                          vk::CommandBuffer cmdbuf) {
            const bool use_sync2 = cmdbuf.HasPipelineBarrier2();
            std::array<VkImageMemoryBarrier, 9> barriers;
            std::array<VkImageMemoryBarrier2, 9> barriers2;
            for (size_t i = 0; i < num_images; ++i) {
                const VkImageSubresourceRange& range = ranges[i];
                const bool is_color = (range.aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) != 0;
                const bool is_depth_stencil = (range.aspectMask
                                              & (VK_IMAGE_ASPECT_DEPTH_BIT
                                                 | VK_IMAGE_ASPECT_STENCIL_BIT)) !=0;

                VkAccessFlags src_access = 0;

                if (is_color)
                    src_access |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                else if (is_depth_stencil)
                    src_access |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                else
                    src_access |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                                  | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

                if (!use_sync2) {
                    barriers[i] = VkImageMemoryBarrier{
                            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                            .pNext = nullptr,
                            .srcAccessMask = src_access,
                            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT
                                             | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT
                                             | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                                             | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                                             | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
                            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                            .image = images[i],
                            .subresourceRange = range,
                    };
                    continue;
                }

                VkPipelineStageFlags2 src_stage = 0;
                VkAccessFlags2 dst_access =
                    VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
                if (is_color) {
                    src_stage |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                    dst_access |= VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT
                                  | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                }
                if (is_depth_stencil) {
                    src_stage |= VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                                 | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
                    dst_access |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                                  | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                }
                if (!is_color && !is_depth_stencil) {
                    src_stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
                                | VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                                | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
                    dst_access |= VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT
                                  | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
                                  | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                                  | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                }
                barriers2[i] = VkImageMemoryBarrier2{
                        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                        .pNext = nullptr,
                        .srcStageMask = src_stage,
                        .srcAccessMask = static_cast<VkAccessFlags2>(src_access),
                        .dstStageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT
                                        | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        .dstAccessMask = dst_access,
                        .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
                        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .image = images[i],
                        .subresourceRange = range,
                };
            }
            if (dynamic_rendering) {
                cmdbuf.EndRendering();
            } else {
                cmdbuf.EndRenderPass();
            }
            if (use_sync2) {
                cmdbuf.PipelineBarrier2(0, nullptr, nullptr,
                                        vk::Span(barriers2.data(), num_images));
            } else {
                cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
                                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, vk::PIPELINE_STAGE_GRAPHICS_COMPUTE,
                                       0, nullptr, nullptr, vk::Span(barriers.data(), num_images));
            }
            if (has_transform_feedback) {
                static constexpr VkMemoryBarrier XFB_OUTPUT_BARRIER{
                    .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                    .pNext = nullptr,
                    .srcAccessMask = VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT,
                    .dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT,
                };
                cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT,
                                       VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                                       0, XFB_OUTPUT_BARRIER);
            }
        });

        state.renderpass = VkRenderPass{};
        state.framebuffer = VkFramebuffer{};
        state.attachment_views = {};
        state.rendering = false;
        state.uses_transform_feedback = false;
        num_renderpass_images = 0;
    }


void Scheduler::AcquireNewChunk() {
    std::scoped_lock rl{reserve_mutex};

    if (chunk_reserve.empty()) {
        // If we don't have anything reserved, we need to make a new chunk.
        chunk = std::make_unique<CommandChunk>();
    } else {
        // Otherwise, we can just take from the reserve.
        chunk = std::move(chunk_reserve.back());
        chunk_reserve.pop_back();
    }
}

} // namespace Vulkan
