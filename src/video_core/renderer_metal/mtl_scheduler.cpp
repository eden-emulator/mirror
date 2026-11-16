// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <atomic>
#include <array>
#include <cstdlib>
#include <cstring>
#include <string>
#include <mutex>
#include <utility>
#include <vector>

#include <boost/container/small_vector.hpp>

#include "common/literals.h"
#include "common/logging.h"
#include "common/settings.h"
#include "common/thread.h"
#include "video_core/metal_common/metal_device.h"
#include "video_core/renderer_metal/mtl_scheduler.h"

namespace Metal {
using namespace Common::Literals;

// Stages an encoder-scoped barrier can name on a render encoder. StageAll is
// rejected, and only the validation layer tells you.
constexpr MTL::Stages RENDER_ENCODER_STAGES =
    static_cast<MTL::Stages>(MTL::StageVertex | MTL::StageFragment);

// Producing side: vertex only, Fragment is NOT legal and silently invalidates it.
constexpr MTL::Stages RENDER_ENCODER_AFTER_STAGES = MTL::StageVertex;

// Blit is in here because Metal 4 has no blit encoder, copies run on compute.
constexpr MTL::Stages COMPUTE_ENCODER_STAGES =
    static_cast<MTL::Stages>(MTL::StageDispatch | MTL::StageBlit);

RenderEncoder CommandContext::RequestRender(const RenderPassState& state) {
    return scheduler.WorkerRequestRender(state);
}

RenderEncoder CommandContext::CurrentRender() const {
    if (scheduler.encoder.type != EncoderType::Render) {
        return {};
    }
    // A wrapper built without the tables silently drops every binding.
    RenderEncoder wrapper{scheduler.encoder.mtl4_render.get()};
    wrapper.SetArgumentTables(scheduler.mtl4_vertex_table.get(),
                              scheduler.mtl4_fragment_table.get(),
                              &scheduler.mtl4_inline_rings[scheduler.mtl4_allocator_index],
                              &scheduler.mtl4_null_bindings);
    return wrapper;
}

MTL::Buffer* CommandContext::CurrentPassVisibilityBuffer() const {
    if (scheduler.encoder.type != EncoderType::Render) {
        return nullptr;
    }
    return scheduler.encoder.pass.visibility_buffer;
}

ComputeEncoder CommandContext::RequestCompute() {
    return scheduler.WorkerRequestCompute();
}

BlitEncoder CommandContext::RequestBlit() {
    return scheduler.WorkerRequestBlit();
}

void CommandContext::EndEncoding() {
    scheduler.WorkerEndEncoding();
}

CommandContext::RenderStateCache& CommandContext::RenderCache() const {
    return scheduler.encoder.render_cache;
}

void CommandContext::InvalidateRenderCache() const {
    scheduler.encoder.render_cache = RenderStateCache{};
}

MTL4::CommandBuffer* CommandContext::MTL4CommandBuffer() const {
    // The handle outlives the submission, so a null check alone is not enough.
    return scheduler.mtl4_committed ? nullptr : scheduler.mtl4_command_buffer.get();
}

void Scheduler::CommandChunk::ExecuteAll(CommandContext& context) {
    Command* command = first;
    while (command != nullptr) {
        Command* const next = command->GetNext();
        command->Execute(context);
        command->~Command();
        command = next;
    }
    submit = false;
    command_offset = 0;
    first = nullptr;
    last = nullptr;
}

Scheduler::Scheduler(const Device& device_)
    : device{device_}, master_semaphore{std::make_unique<MasterSemaphore>(device_)} {
    CreateMetal4Bindings();
    AcquireNewChunk();
    AllocateNewCommandBuffer();
    worker_thread = std::jthread([this](std::stop_token token) { WorkerThread(token); });
}

Scheduler::~Scheduler() {
    // MasterSemaphore's feedback handler captures `this`.
    Finish();

    worker_thread.request_stop();
    event_cv.notify_all();
    worker_thread = {};

    SweepKeepAlive();
}

void Scheduler::AcquireNewChunk() {
    std::scoped_lock lock{reserve_mutex};
    if (chunk_reserve.empty()) {
        chunk = std::make_unique<CommandChunk>();
        return;
    }
    chunk = std::move(chunk_reserve.back());
    chunk_reserve.pop_back();
}

void Scheduler::CreateMetal4Bindings() {
    ScopedAutoreleasePool pool;
    NS::Error* error = nullptr;

    // MTL4 wants the bind counts up front, so these cover the widest set we bind.
    const auto make_table = [&](const char* label, u32 buffers, u32 textures,
                                u32 samplers) -> Owned<MTL4::ArgumentTable> {
        MTL4::ArgumentTableDescriptor* descriptor = MTL4::ArgumentTableDescriptor::alloc()->init();
        descriptor->setMaxBufferBindCount(buffers);
        descriptor->setMaxTextureBindCount(textures);
        descriptor->setMaxSamplerStateBindCount(samplers);
        // Defaults to false, which leaves every slot UNINITIALISED: a binding we
        // never write reads garbage as a resource id and faults the GPU silently.
        descriptor->setInitializeBindings(true);
        // Vertex attribute strides travel with the address on MTL4.
        descriptor->setSupportAttributeStrides(true);
        descriptor->setLabel(ToNSString(label));
        Owned<MTL4::ArgumentTable> table =
            Adopt(device.GetDevice()->newArgumentTable(descriptor, &error));
        descriptor->release();
        if (!table) {
            LOG_ERROR(Render_Metal, "Failed to create the {} argument table: {}", label,
                      DescribeError(error));
        }
        return table;
    };

    // Hard per-stage limits in Metal. Going over (samplers above 16 especially)
    // is only reported by the validation layer; the table is silently wrong.
    constexpr u32 MAX_TABLE_BUFFERS = 31;
    constexpr u32 MAX_TABLE_TEXTURES = 128;
    constexpr u32 MAX_TABLE_SAMPLERS = 16;

    mtl4_vertex_table =
        make_table("eden vertex args", MAX_TABLE_BUFFERS, MAX_TABLE_TEXTURES, MAX_TABLE_SAMPLERS);
    mtl4_fragment_table =
        make_table("eden fragment args", MAX_TABLE_BUFFERS, MAX_TABLE_TEXTURES, MAX_TABLE_SAMPLERS);
    mtl4_compute_table =
        make_table("eden compute args", MAX_TABLE_BUFFERS, MAX_TABLE_TEXTURES, MAX_TABLE_SAMPLERS);

    constexpr u64 INLINE_RING_SIZE = 4_MiB;
    for (size_t i = 0; i < MAX_MTL4_ALLOCATORS; ++i) {
        mtl4_inline_buffers[i] = Adopt(device.GetDevice()->newBuffer(
            INLINE_RING_SIZE, MTL::ResourceStorageModeShared));
        if (!mtl4_inline_buffers[i]) {
            LOG_ERROR(Render_Metal, "Failed to allocate a Metal 4 inline constant ring");
            continue;
        }
        mtl4_inline_buffers[i]->setLabel(ToNSString("eden mtl4 inline constants"));
        mtl4_inline_rings[i].buffer = mtl4_inline_buffers[i].get();
        mtl4_inline_rings[i].mapped =
            static_cast<u8*>(mtl4_inline_buffers[i]->contents());
        mtl4_inline_rings[i].capacity = INLINE_RING_SIZE;
        mtl4_inline_rings[i].offset = 0;
        // Nothing is implicitly resident on MTL4. Set methods are not thread safe.
        device.AddResidentAllocation(mtl4_inline_buffers[i].get());
    }

    // Shaders load through raw device pointers, so an unbound buffer slot can't
    // sit at address 0. Sized for a strided read of a whole vertex batch.
    constexpr u64 NULL_BUFFER_SIZE = 1_MiB;
    mtl4_null_buffer = Adopt(device.GetDevice()->newBuffer(
        NULL_BUFFER_SIZE, MTL::ResourceStorageModeShared));
    if (mtl4_null_buffer) {
        mtl4_null_buffer->setLabel(ToNSString("eden mtl4 null binding"));
        std::memset(mtl4_null_buffer->contents(), 0, NULL_BUFFER_SIZE);
        device.AddResidentAllocation(mtl4_null_buffer.get());
        mtl4_null_bindings.buffer = mtl4_null_buffer->gpuAddress();
    } else {
        LOG_ERROR(Render_Metal,
                  "Failed to allocate the Metal 4 null binding buffer; unbound buffer slots "
                  "will keep their previous contents");
    }

    device.CommitResidency();
}

void Scheduler::AllocateNewCommandBuffer() {
    ScopedAutoreleasePool mtl4_pool;

    // beginCommandBuffer recycles the driver's encoder context, so an encoder left
    // open here is the wiped-context crash in AGX::ComputeContext::endComputePass.
    if (encoder.type != EncoderType::None) {
        LOG_WARNING(Render_Metal, "Encoder still open at command buffer recycle; closing it");
        WorkerEndEncoding();
    }

    mtl4_allocator_index = (mtl4_allocator_index + 1) % MAX_MTL4_ALLOCATORS;
    Owned<MTL4::CommandAllocator>& allocator = mtl4_allocators[mtl4_allocator_index];
    if (!allocator) {
        allocator = Adopt(device.GetDevice()->newCommandAllocator());
        if (!allocator) {
            LOG_ERROR(Render_Metal, "Failed to create a Metal 4 command allocator");
            return;
        }
    } else {
        const u64 owner_tick = mtl4_allocator_ticks[mtl4_allocator_index];
        if (owner_tick != 0) {
            master_semaphore->Wait(owner_tick);
        }
        allocator->reset();
    }
    // The upload allocator rotates with the main one, covered by the wait above.
    if (Owned<MTL4::CommandAllocator>& upload = mtl4_upload_allocators[mtl4_allocator_index]) {
        upload->reset();
    }
    mtl4_allocator_ticks[mtl4_allocator_index] = 0;
    mtl4_inline_rings[mtl4_allocator_index].Reset();

    // A Metal 4 command buffer is a reusable handle, not a per-frame object.
    if (!mtl4_command_buffer) {
        mtl4_command_buffer = Adopt(device.GetDevice()->newCommandBuffer());
        if (!mtl4_command_buffer) {
            LOG_ERROR(Render_Metal, "Failed to create a Metal 4 command buffer");
            return;
        }
    }
    mtl4_command_buffer->beginCommandBuffer(allocator.get());
    mtl4_committed = false;
    mtl4_command_buffer->setLabel(ToNSString("eden frame"));
    if (MTL::ResidencySet* const set = device.GetResidencySet()) {
        mtl4_command_buffer->useResidencySet(set);
    }
}

void Scheduler::DispatchWork() {
    std::scoped_lock lock{chunk_mutex};
    DispatchWorkLocked();
}

void Scheduler::DispatchWorkLocked() {
    if (chunk->Empty()) {
        return;
    }
    {
        std::scoped_lock lock{queue_mutex};
        work_queue.push(std::move(chunk));
    }
    event_cv.notify_all();
    AcquireNewChunk();
}

void Scheduler::WaitWorker() {
    DispatchWork();

    // The execution lock also waits out the chunk already in flight.
    std::unique_lock lock{queue_mutex};
    event_cv.wait(lock, [this] { return work_queue.empty(); });
    std::scoped_lock execution_lock{execution_mutex};
}


void Scheduler::RequestRenderPass(const RenderPassState& state) {
    // Returning early on a redundant request is only safe because everything that
    // closes the encoder behind our back calls
    // RequestOutsideRenderPassOperationContext first.
    const bool same_pass = recorded_pass_active && last_requested_pass.IsCompatibleWith(state);

    // A pass boundary is the only place we can close a submission for size:
    // splitting one stores and reloads every attachment on a TBDR, ~16 MiB each way
    // for the 1080p depth targets Legends Z-A leans on. The check does NOT need a
    // pass to be open; gating on that let submissions hit 56 encoders and 16 ms
    // against a bound of 24.
    if (!same_pass && (passes_since_flush >= MAX_PASSES_PER_SUBMISSION || flush_at_pass_boundary)) {
        Flush();
    }

    last_requested_pass = state;

    recorded_pass_active = true;
    if (!same_pass) {
        pass_wrote_depth = false;
        pass_drew = false;
        pass_written_attachments = 0;
    pass_storage_writes = false;
    }
    // A depth clear is a load action, and the feedback gate only sees draws.
    if (state.depth_stencil.texture != nullptr &&
        state.depth_stencil.depth_load == LoadAction::Clear) {
        pass_wrote_depth = true;
    }
    // Cleared attachments no longer match memory either.
    for (u32 index = 0; index < state.num_color; ++index) {
        if (state.color[index].texture != nullptr &&
            state.color[index].load_action == LoadAction::Clear) {
            pass_written_attachments |= 1u << index;
        }
    }
    if (state.depth_stencil.texture != nullptr &&
        (state.depth_stencil.depth_load == LoadAction::Clear ||
         state.depth_stencil.stencil_load == LoadAction::Clear)) {
        pass_written_attachments |= DEPTH_ATTACHMENT_BIT;
    }
    if (same_pass) {
        return;
    }
    ++passes_since_flush;
    Record([state](CommandContext& context) { (void)context.RequestRender(state); });
}

void Scheduler::WorkerBeginUpload() {
    if (mtl4_upload_open) {
        return;
    }
    Owned<MTL4::CommandAllocator>& allocator = mtl4_upload_allocators[mtl4_allocator_index];
    if (!allocator) {
        allocator = Adopt(device.GetDevice()->newCommandAllocator());
        if (!allocator) {
            LOG_ERROR(Render_Metal, "Failed to create a Metal 4 upload allocator");
            return;
        }
    }
    if (!mtl4_upload_buffer) {
        mtl4_upload_buffer = Adopt(device.GetDevice()->newCommandBuffer());
        if (!mtl4_upload_buffer) {
            LOG_ERROR(Render_Metal, "Failed to create the Metal 4 upload command buffer");
            return;
        }
        mtl4_upload_buffer->setLabel(ToNSString("eden mtl4 uploads"));
    }
    mtl4_upload_buffer->beginCommandBuffer(allocator.get());
    if (MTL::ResidencySet* const set = device.GetResidencySet()) {
        mtl4_upload_buffer->useResidencySet(set);
    }
    mtl4_upload_encoder = Retain(mtl4_upload_buffer->computeCommandEncoder());
    if (!mtl4_upload_encoder) {
        LOG_ERROR(Render_Metal, "Failed to open the Metal 4 upload encoder");
        return;
    }
    mtl4_upload_open = true;
    mtl4_upload_has_work = false;
}

void Scheduler::RecordUpload(std::function<void(MTL4::ComputeCommandEncoder*)>&& command) {
    Record([cmd = std::move(command)](CommandContext& context) {
        Scheduler& self = context.OwningScheduler();
        self.WorkerBeginUpload();
        if (!self.mtl4_upload_encoder) {
            return;
        }
        cmd(self.mtl4_upload_encoder.get());
        self.mtl4_upload_has_work = true;
    });
}

void Scheduler::WorkerCommitUpload() {
    if (!mtl4_upload_open) {
        return;
    }
    if (mtl4_upload_encoder) {
        if (mtl4_upload_has_work) {
            // Only producer that never sets compute_or_blit_since_render_barrier,
            // so without this the next render encoder opens on StageFragment alone
            // and the tiler fetches vertex data this copy hasn't landed yet. Commit
            // order isn't completion order and Metal 4 does no hazard tracking.
            mtl4_upload_encoder->barrierAfterStages(COMPUTE_ENCODER_STAGES, MTL::StageAll,
                                                    MTL4::VisibilityOptionDevice);
        }
        mtl4_upload_encoder->endEncoding();
        mtl4_upload_encoder.reset();
    }
    mtl4_upload_buffer->endCommandBuffer();
    if (mtl4_upload_has_work) {
        MTL4::CommandBuffer* buffers[] = {mtl4_upload_buffer.get()};
        device.GetMTL4Queue()->commit(buffers, 1);
    }
    mtl4_upload_open = false;
    mtl4_upload_has_work = false;
}


void Scheduler::RequestOutsideRenderPassOperationContext() {
    if (!recorded_pass_active) {
        return;
    }
    recorded_pass_active = false;
    pass_wrote_depth = false;
    pass_drew = false;
    pass_written_attachments = 0;
    pass_storage_writes = false;
    Record([](CommandContext& context) { context.EndEncoding(); });
    if (flush_at_pass_boundary) {
        Flush();
    }
}

void Scheduler::FlushAtPassBoundary() {
    if (!recorded_pass_active) {
        Flush();
        return;
    }
    flush_at_pass_boundary = true;
}

void Scheduler::RequestCloseEncoders() {
    // Unconditional: RequestOutsideRenderPassOperationContext only tracks *render*
    // passes and can leave a compute or copy encoder open, which is fatal for
    // MetalFX and friends (AGX::ComputeContext::endComputePass faults).
    recorded_pass_active = false;
    pass_wrote_depth = false;
    pass_drew = false;
    pass_written_attachments = 0;
    pass_storage_writes = false;
    Record([](CommandContext& context) {
        context.EndEncoding();

        // Ending the encoder does not order it, and MetalFX never gets the
        // encoder-open barrier. Compute and not render because a render encoder
        // can't name Fragment as a producer stage on TBDR.
        ComputeEncoder barrier = context.RequestCompute();
        if (barrier != nullptr) {
            barrier.barrierBeforeQueueStages(COMPUTE_ENCODER_STAGES, MTL::StageAll);
        }
        context.EndEncoding();
    });
}

void Scheduler::RequestFeedbackBarrier() {
    if (!pass_wrote_depth) {
        return;
    }
    RequestOutsideRenderPassOperationContext();
}

u64 Scheduler::Flush(std::function<void()> after_commit) {
    // Claimed here so submissions are numbered in request order; the commit
    // itself is recorded and runs on the worker.
    const u64 signal_tick = master_semaphore->NextTick();

    const auto commit = [this, signal_tick,
                         after_commit = std::move(after_commit)](CommandContext& context) {
        context.EndEncoding();

        if (on_submit) {
            on_submit();
        }

        if (mtl4_command_buffer && !mtl4_committed) {
            // A dead queue refuses everything forever.
            if (master_semaphore->ConsumeDeadQueue()) {
                static_cast<void>(device.RecreateMTL4Queue());
            }
            // Let the queue drain after a fault. Metal starts refusing submissions
            // "for causing prior/excessive GPU errors" once enough pile up behind a
            // bad one, and that state is terminal. Use `signal_tick - 1`, NOT
            // CurrentTick() - 1: waiting on the worker for work only the worker can
            // submit deadlocks on the first fault.
            if (signal_tick > 1 && master_semaphore->ConsumeNewFault()) {
                master_semaphore->Wait(signal_tick - 1);
            }
            device.CommitResidency();

            // Uploads go out first. MTL4 executes in commit order, which is what
            // makes RecordUpload safe for reorderable copies.
            WorkerCommitUpload();
            master_semaphore->SignalAndCommitMTL4(
                device.GetMTL4Queue(), mtl4_command_buffer.get(), signal_tick,
                pending_signal_tick.load(std::memory_order_relaxed), encoder.opened);
            encoder.opened = SubmissionContents{};
            pending_signal_tick.store(0, std::memory_order_release);
            // The allocator stays busy until the GPU drains.
            mtl4_allocator_ticks[mtl4_allocator_index] = signal_tick;
            mtl4_committed = true;
        } else {
            // We claimed a tick with nothing to attach it to. Leaving it strands
            // every later Wait on a value nothing signals.
            pending_signal_tick.store(
                std::max(pending_signal_tick.load(std::memory_order_relaxed), signal_tick),
                std::memory_order_release);
            LOG_WARNING(Render_Metal,
                        "No Metal 4 command buffer for tick {}; folding it into the next",
                        signal_tick);
        }

        if (after_commit) {
            after_commit();
        }
    };

    SweepKeepAlive();

    // ONE acquisition of chunk_mutex: a concurrent DispatchWork between the record
    // and the MarkSubmit would mark a different, empty chunk.
    {
        std::scoped_lock lock{chunk_mutex};
        RecordLocked(commit);
        chunk->MarkSubmit();
        recorded_pass_active = false;
        flush_at_pass_boundary = false;
        passes_since_flush = 0;
        DispatchWorkLocked();
    }
    return signal_tick;
}

void Scheduler::Finish() {
    Wait(Flush({}));
}

void Scheduler::Wait(u64 tick) {
    if (tick == 0) {
        return;
    }
    // Without this every wait falls through to WaitWorker() below, and the callers
    // are hot enough to re-sync the whole pipeline.
    if (master_semaphore->IsFree(tick)) {
        return;
    }
    if (tick >= master_semaphore->CurrentTick()) {
        // Nothing has been submitted that would ever signal this tick.
        Flush();
    } else if (pending_signal_tick.load(std::memory_order_acquire) != 0) {
        // A carried tick sits below CurrentTick(), so the test above misses it.
        Flush();
    }
    WaitWorker();
    master_semaphore->Wait(tick);
}

void Scheduler::KeepAlive(NS::Object* object, const MTL::Allocation* allocation) {
    if (object == nullptr) {
        return;
    }
    object->retain();
    std::scoped_lock lock{keep_alive_mutex};
    // CurrentTick(), not the last submitted one: the command is being recorded now.
    keep_alive.push_back(PendingRelease{object, allocation, master_semaphore->CurrentTick()});
}

void Scheduler::SweepKeepAlive() {
    std::scoped_lock lock{keep_alive_mutex};

    // Residency runs before the release, and both halves matter.
    // RemoveResidentAllocation takes a pointer into the object, so releasing first
    // names freed memory and corrupts the set. (Tried it, the whole screen went to
    // garbage.) And only once nothing in flight still names the allocation:
    // residency is NOT refcounted while keep_alive holds one entry per reference,
    // so dropping it on the first completion strands the second -- a GPU page fault
    // (MTL4CommandQueueErrorDomain code 2) that fails everything queued behind it.
    bool removed_any = false;
    boost::container::small_vector<const MTL::Allocation*, 16> removed;
    for (const PendingRelease& pending : keep_alive) {
        if (pending.allocation == nullptr || !master_semaphore->IsFree(pending.tick)) {
            continue;
        }
        const bool still_in_flight =
            std::any_of(keep_alive.begin(), keep_alive.end(),
                        [this, &pending](const PendingRelease& other) {
                            return other.allocation == pending.allocation &&
                                   !master_semaphore->IsFree(other.tick);
                        });
        const bool already_removed =
            std::find(removed.begin(), removed.end(), pending.allocation) != removed.end();
        if (!still_in_flight && !already_removed) {
            device.RemoveResidentAllocation(pending.allocation);
            removed.push_back(pending.allocation);
            removed_any = true;
        }
    }

    std::erase_if(keep_alive, [this](const PendingRelease& pending) {
        if (!master_semaphore->IsFree(pending.tick)) {
            return false;
        }
        pending.object->release();
        return true;
    });

    if (removed_any) {
        // Republish, or the OS keeps backing the removed allocations.
        device.CommitResidency();
    }
}

void Scheduler::WorkerThread(std::stop_token stop_token) {
    Common::SetCurrentThreadName("MetalWorker");
    Common::SetCurrentThreadToPerformanceCores();

    CommandContext context{*this};

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
            std::unique_lock lock{queue_mutex};
            event_cv.wait(lock, stop_token, [&] { return TryPopQueue(work); });
            if (stop_token.stop_requested()) {
                break;
            }

            // Take the execution lock before releasing the queue lock, so a
            // WaitWorker() seeing an empty queue can't race ahead of this chunk.
            static_cast<void>(std::exchange(lock, std::unique_lock{execution_mutex}));

            const bool has_submit = work->HasSubmit();

            {
                // Encoders come back autoreleased; the pool is per chunk so they
                // don't pile up for the life of the thread.
                ScopedAutoreleasePool pool;
                work->ExecuteAll(context);
            }

            if (has_submit) {
                AllocateNewCommandBuffer();
            }
        }

        std::scoped_lock reserve_lock{reserve_mutex};
        chunk_reserve.emplace_back(std::move(work));
    }

    WorkerEndEncoding();
}

RenderEncoder Scheduler::WorkerRequestRender(const RenderPassState& state) {
    if (!mtl4_command_buffer || mtl4_committed) {
        return {};
    }
    // A spurious restart is a tile store plus reload on Apple hardware.
    if (encoder.type == EncoderType::Render && encoder.pass.IsCompatibleWith(state)) {
        // Orders vertex -> {vertex, fragment} and that's all it can do; real
        // fragment-side dependencies have to end the encoder instead.
        encoder.mtl4_render->barrierAfterEncoderStages(RENDER_ENCODER_AFTER_STAGES,
                                                       RENDER_ENCODER_STAGES,
                                                       MTL4::VisibilityOptionDevice);

        // The argument tables are scheduler-wide while this cache is per encoder,
        // so anything binding during the pass overwrites slots it thinks it owns.
        encoder.render_cache = CommandContext::RenderStateCache{};

        RenderEncoder wrapper{encoder.mtl4_render.get()};
        wrapper.SetArgumentTables(mtl4_vertex_table.get(), mtl4_fragment_table.get(),
                                  &mtl4_inline_rings[mtl4_allocator_index],
                                  &mtl4_null_bindings);
        return wrapper;
    }
    WorkerEndEncoding();

    MTL4::RenderPassDescriptor* mtl4_descriptor = BuildMTL4RenderPassDescriptor(state);
    encoder.mtl4_render = Retain(mtl4_command_buffer->renderCommandEncoder(mtl4_descriptor));
    if (!encoder.mtl4_render) {
        LOG_ERROR(Render_Metal, "Failed to create a Metal 4 render command encoder");
        return {};
    }
    encoder.type = EncoderType::Render;
    ++encoder.opened.render_encoders;
    encoder.pass = state;
    encoder.render_cache = CommandContext::RenderStateCache{};

    // Biggest target this submission writes. Pixel count is a rough proxy for
    // cost, but we have no per-encoder timing to do better.
    if (const u64 pixels = u64{state.render_area_width} * state.render_area_height;
        pixels > u64{encoder.opened.pass_width} * encoder.opened.pass_height) {
        encoder.opened.pass_width = state.render_area_width;
        encoder.opened.pass_height = state.render_area_height;
        encoder.opened.pass_samples = state.sample_count;
        encoder.opened.color_format =
            state.num_color > 0 && state.color[0].texture
                ? static_cast<u32>(state.color[0].texture->pixelFormat())
                : 0;
        encoder.opened.depth_format =
            state.depth_stencil.texture
                ? static_cast<u32>(state.depth_stencil.texture->pixelFormat())
                : 0;
    }

    // Metal 4 does NO hazard tracking, and a missed dependency is a silent
    // wrong-pixel bug, not a validation error. Fragment alone: naming Vertex kills
    // the tiling overlap on every one of ~160 encoder opens a frame. The gap is a
    // *vertex* shader sampling a texture a prior pass wrote; widen back to
    // RENDER_ENCODER_STAGES if it shows up.
    const MTL::Stages consumers =
        compute_or_blit_since_render_barrier ? RENDER_ENCODER_STAGES : MTL::StageFragment;
    compute_or_blit_since_render_barrier = false;
    encoder.mtl4_render->barrierAfterQueueStages(MTL::StageAll, consumers,
                                                 MTL4::VisibilityOptionDevice);

    RenderEncoder wrapper{encoder.mtl4_render.get()};
    wrapper.SetArgumentTables(mtl4_vertex_table.get(), mtl4_fragment_table.get(),
                              &mtl4_inline_rings[mtl4_allocator_index], &mtl4_null_bindings);
    return wrapper;
}

ComputeEncoder Scheduler::WorkerRequestCompute() {
    if (!mtl4_command_buffer || mtl4_committed) {
        return {};
    }
    // A copy encoder IS a compute encoder, so an open one takes dispatches
    // directly. Ending it to open an identical encoder costs a queue-scope drain
    // for nothing (Legends Z-A interleaves ~340 of each a frame).
    if (encoder.type == EncoderType::Compute || encoder.type == EncoderType::Blit) {
        MTL4::ComputeCommandEncoder* const open = encoder.type == EncoderType::Compute
                                                      ? encoder.mtl4_compute.get()
                                                      : encoder.mtl4_blit.get();
        // Metal 4 does not order dispatches inside one compute encoder. On an M1 a
        // second dispatch reproducibly read the pre-write value of what the first
        // wrote. Serializes independent dispatches too, until per-resource tracking.
        open->barrierAfterEncoderStages(COMPUTE_ENCODER_STAGES, COMPUTE_ENCODER_STAGES,
                                        MTL4::VisibilityOptionDevice);
        // Always set it, a copy encoder reused for compute never had one bound.
        ComputeEncoder wrapper{open};
        wrapper.SetArgumentTable(mtl4_compute_table.get(),
                                 &mtl4_inline_rings[mtl4_allocator_index], &mtl4_null_bindings);
        return wrapper;
    }
    WorkerEndEncoding();
    encoder.mtl4_compute = Retain(mtl4_command_buffer->computeCommandEncoder());
    if (!encoder.mtl4_compute) {
        LOG_ERROR(Render_Metal, "Failed to create a Metal 4 compute command encoder");
        return {};
    }
    encoder.type = EncoderType::Compute;
    ++encoder.opened.compute_encoders;
    compute_or_blit_since_render_barrier = true;

    encoder.mtl4_compute->barrierAfterQueueStages(MTL::StageAll, COMPUTE_ENCODER_STAGES,
                                                  MTL4::VisibilityOptionDevice);

    ComputeEncoder wrapper{encoder.mtl4_compute.get()};
    wrapper.SetArgumentTable(mtl4_compute_table.get(),
                             &mtl4_inline_rings[mtl4_allocator_index], &mtl4_null_bindings);
    return wrapper;
}

BlitEncoder Scheduler::WorkerRequestBlit() {
    if (!mtl4_command_buffer || mtl4_committed) {
        return {};
    }
    if (encoder.type == EncoderType::Blit || encoder.type == EncoderType::Compute) {
        MTL4::ComputeCommandEncoder* const open = encoder.type == EncoderType::Blit
                                                      ? encoder.mtl4_blit.get()
                                                      : encoder.mtl4_compute.get();
        open->barrierAfterEncoderStages(COMPUTE_ENCODER_STAGES, COMPUTE_ENCODER_STAGES,
                                        MTL4::VisibilityOptionDevice);
        return BlitEncoder{open};
    }
    WorkerEndEncoding();
    encoder.mtl4_blit = Retain(mtl4_command_buffer->computeCommandEncoder());
    if (!encoder.mtl4_blit) {
        LOG_ERROR(Render_Metal, "Failed to create a Metal 4 copy (compute) encoder");
        return {};
    }
    encoder.type = EncoderType::Blit;
    ++encoder.opened.blit_encoders;
    compute_or_blit_since_render_barrier = true;

    encoder.mtl4_blit->barrierAfterQueueStages(MTL::StageAll, COMPUTE_ENCODER_STAGES,
                                               MTL4::VisibilityOptionDevice);

    return BlitEncoder{encoder.mtl4_blit.get()};
}

void Scheduler::WorkerEndEncoding() {
    // Residency belongs to the encoder: whatever was declared is forgotten here.

    switch (encoder.type) {
    case EncoderType::Render:
        if (encoder.mtl4_render) {
            encoder.mtl4_render->endEncoding();
        }
        encoder.mtl4_render = {};
        encoder.pass = {};
        break;
    case EncoderType::Compute:
        if (encoder.mtl4_compute) {
            encoder.mtl4_compute->endEncoding();
        }
        encoder.mtl4_compute = {};
        break;
    case EncoderType::Blit:
        if (encoder.mtl4_blit) {
            encoder.mtl4_blit->endEncoding();
        }
        encoder.mtl4_blit = {};
        break;
    case EncoderType::None:
        return;
    }
    encoder.type = EncoderType::None;

}

} // namespace Metal
