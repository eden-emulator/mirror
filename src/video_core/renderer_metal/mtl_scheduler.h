// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>

#include <boost/container/small_vector.hpp>
#include <span>
#include <thread>
#include <utility>
#include <vector>

#include "common/alignment.h"
#include "common/common_funcs.h"
#include "common/common_types.h"
#include "common/polyfill_thread.h"
#include "video_core/metal_common/metal_wrapper.h"
#include "video_core/renderer_metal/mtl_encoder.h"
#include "video_core/renderer_metal/mtl_master_semaphore.h"
#include "video_core/renderer_metal/mtl_render_pass_state.h"

namespace Metal {

class Device;

/// Metal only allows one open encoder at a time per command buffer.
enum class EncoderType {
    None,
    Render,
    Compute,
    Blit,
};

/// A command asks for the encoder it needs and we close whatever was open first, so no call
/// site has to know the rule.
class CommandContext {
public:
    explicit CommandContext(class Scheduler& scheduler_) : scheduler{scheduler_} {}

    [[nodiscard]] RenderEncoder RequestRender(const RenderPassState& state);

    /// The render encoder that is open right now, or null.
    [[nodiscard]] RenderEncoder CurrentRender() const;

    [[nodiscard]] ComputeEncoder RequestCompute();
    [[nodiscard]] BlitEncoder RequestBlit();

    void EndEncoding();

    /// For the few things that encode into a command buffer instead of an encoder (MetalFX).
    [[nodiscard]] MTL4::CommandBuffer* MTL4CommandBuffer() const;

    /// All of it encoder state, so reset it whenever an encoder is opened or re-entered.
    struct RenderStateCache {
        static constexpr u32 MAX_BUFFERS = 31;
        static constexpr u32 MAX_VIEWPORTS = 16;

        MTL::RenderPipelineState* pipeline{};
        MTL::DepthStencilState* depth_stencil{};
        std::array<MTL::Buffer*, MAX_BUFFERS> vertex_buffer{};
        std::array<size_t, MAX_BUFFERS> vertex_offset{};
        MTL::Buffer* fragment_buffer{};
        size_t fragment_offset{};

        std::array<MTL::Viewport, MAX_VIEWPORTS> viewports{};
        std::array<MTL::ScissorRect, MAX_VIEWPORTS> scissors{};
        u32 viewport_count{};

        u32 cull_mode{~0u};
        u32 winding{~0u};
        u32 depth_clip{~0u};
        u32 fill_mode{~0u};
        u32 stencil_front{~0u};
        u32 stencil_back{~0u};
        std::array<f32, 3> depth_bias{};
        std::array<f32, 4> blend_color{};
        /// Visibility slot the encoder is counting into, or ~0 for disabled.
        u32 visibility_slot{~0u};
        bool depth_bias_valid{};
        bool blend_color_valid{};
    };

    [[nodiscard]] RenderStateCache& RenderCache() const;

    /// Ask an encoder that was opened without a visibility buffer to count and the driver faults.
    [[nodiscard]] MTL::Buffer* CurrentPassVisibilityBuffer() const;

    /// Needed after anything that binds through CurrentRender() without keeping the cache up
    /// to date (the clear and blit helpers).
    void InvalidateRenderCache() const;

    [[nodiscard]] Scheduler& OwningScheduler() const noexcept {
        return scheduler;
    }

private:
    Scheduler& scheduler;
};

/// Records GPU work on the calling thread as chunked lambdas and replays it on a worker
/// thread, driving the encoder state machine.
class Scheduler : Common::NonCopyable, Common::NonMoveable {
    friend class CommandContext;

public:
    explicit Scheduler(const Device& device);
    ~Scheduler();

    /// @p after_commit runs on the worker right after the commit, for work that has to be
    /// ordered behind this submission and not just recorded before it. signalDrawable is why.
    u64 Flush(std::function<void()> after_commit = {});

    /// Flushes now when no render pass is open, otherwise at the end of the open one: a
    /// submission boundary inside a pass stores and reloads every attachment on a tiler.
    void FlushAtPassBoundary();

    void Finish();

    void WaitWorker();

    void DispatchWork();

    /// Recorded, so it takes effect in submission order and not right away.
    void RequestRenderPass(const RenderPassState& state);

    void RequestOutsideRenderPassOperationContext();

    /// Closes any open encoder without looking at the render-pass mirror. See the definition.
    void RequestCloseEncoders();

    /// Ends the pass so a texture that is also an attachment can be sampled, but only if a draw
    /// wrote depth since the last break. Each break stores and reloads every attachment.
    void RequestFeedbackBarrier();

    /// Pass the state too: a stale last_requested_pass answers for an encoder the worker no
    /// longer has.
    void NotifyRenderPassOpened(const RenderPassState& state) noexcept {
        recorded_pass_active = true;
        last_requested_pass = state;
    }

    void MarkDepthWritten() noexcept {
        pass_wrote_depth = true;
    }

    /// Attachment bits for MarkPassDrawn: bit i is colour attachment i.
    static constexpr u32 DEPTH_ATTACHMENT_BIT = 1u << 8;
    static constexpr u32 ALL_ATTACHMENTS = ~0u;

    /// @p written_attachments says which attachments the draw may have modified, so a later
    /// draw sampling an untouched one keeps the pass. Defaults to all of them.
    void MarkPassDrawn(u32 written_attachments = ALL_ATTACHMENTS,
                       bool storage_writes = true) noexcept {
        pass_drew = true;
        pass_written_attachments |= written_attachments;
        pass_storage_writes |= storage_writes;
    }

    /// Before the first draw the attachment still matches the texture behind it, so a feedback
    /// break is pure cost.
    [[nodiscard]] bool PassDrew() const noexcept {
        return pass_drew;
    }

    /// Attachments whose tile contents may differ from memory, see MarkPassDrawn.
    [[nodiscard]] u32 PassWrittenAttachments() const noexcept {
        return pass_written_attachments;
    }

    /// Not deferred despite the name, that got reverted, it measured no better. Attachment RMW
    /// is safe, Apple orders load/store per fragment in primitive order.
    void RequestDeferredFragmentBarrier() {
        // No write to order against yet, and the break still costs a full attachment store and
        // reload. Legends Z-A fires ~80 of these a frame against 1080p targets. Attachment
        // writes are ordered by the feedback-loop test, so only storage writes need the break.
        if (!pass_drew || !pass_storage_writes) {
            return;
        }
        RequestOutsideRenderPassOperationContext();
    }

    [[nodiscard]] bool IsRenderPassActive() const noexcept {
        return recorded_pass_active;
    }

    /// The watchdog measures passes, draw count is a poor proxy: Legends Z-A hit 97 passes
    /// running ~14 ms in one submission and got killed, after which the MTL4 queue fails
    /// everything behind it.
    static constexpr u32 MAX_PASSES_PER_SUBMISSION = 24;

    /// The lambda lives inline in a chunk arena, so keep it small and trivially relocatable.
    template <typename T>
        requires std::is_invocable_v<T, CommandContext&>
    void Record(T&& command) {
        std::scoped_lock lock{chunk_mutex};
        RecordLocked(command);
    }

    using RenderEncoderRef = RenderEncoder;
    using ComputeEncoderRef = ComputeEncoder;
    using BlitEncoderRef = BlitEncoder;

    /// Record with chunk_mutex already held, see the comment in Flush.
    template <typename T>
        requires std::is_invocable_v<T, CommandContext&>
    void RecordLocked(T&& command) {
        if (chunk->Record(command)) {
            return;
        }
        DispatchWorkLocked();
        (void)chunk->Record(command);
    }

    [[nodiscard]] u64 CurrentTick() const noexcept {
        return master_semaphore->CurrentTick();
    }

    [[nodiscard]] bool IsFree(u64 tick) const noexcept {
        return master_semaphore->IsFree(tick);
    }

    /// Blocks until the GPU reaches @p tick, flushing first if it hasn't been submitted yet.
    void Wait(u64 tick);

    /// Metal 4 command buffers do NOT retain the resources their commands touch, so anything
    /// transient a recorded command refers to is a UAF without this.
    void KeepAlive(NS::Object* object, const MTL::Allocation* allocation = nullptr);

    [[nodiscard]] MasterSemaphore& GetMasterSemaphore() const noexcept {
        return *master_semaphore;
    }

    void RegisterOnSubmit(std::function<void()>&& func) {
        on_submit = std::move(func);
    }

private:
    class Command {
    public:
        virtual ~Command() = default;
        virtual void Execute(CommandContext& context) const = 0;

        Command* GetNext() const noexcept {
            return next;
        }

        void SetNext(Command* next_) noexcept {
            next = next_;
        }

    private:
        Command* next = nullptr;
    };

    template <typename T>
    class TypedCommand final : public Command {
    public:
        explicit TypedCommand(T&& command_) : command{std::move(command_)} {}
        ~TypedCommand() override = default;

        TypedCommand(TypedCommand&&) = delete;
        TypedCommand& operator=(TypedCommand&&) = delete;

        void Execute(CommandContext& context) const override {
            command(context);
        }

    private:
        T command;
    };

    class CommandChunk final {
    public:
        void ExecuteAll(CommandContext& context);

        template <typename T>
        bool Record(T& command) {
            using FuncType = TypedCommand<T>;
            static_assert(sizeof(FuncType) < sizeof(data), "Lambda is too large");

            command_offset = Common::AlignUp(command_offset, alignof(FuncType));
            if (command_offset > sizeof(data) - sizeof(FuncType)) {
                return false;
            }
            Command* const current_last = last;
            last = new (data.data() + command_offset) FuncType(std::move(command));

            if (current_last) {
                current_last->SetNext(last);
            } else {
                first = last;
            }
            command_offset += sizeof(FuncType);
            return true;
        }

        void MarkSubmit() noexcept {
            submit = true;
        }

        [[nodiscard]] bool Empty() const noexcept {
            return command_offset == 0;
        }

        [[nodiscard]] bool HasSubmit() const noexcept {
            return submit;
        }

    private:
        Command* first = nullptr;
        Command* last = nullptr;

        size_t command_offset = 0;
        bool submit = false;
        alignas(std::max_align_t) std::array<u8, 0x8000> data{};
    };

    /// Retained, not raw: an encoder stays open across chunks while the worker's autorelease
    /// pool is per chunk.
    struct EncoderState {
        EncoderType type{EncoderType::None};
        /// There is no MTL4 blit encoder at all, so `mtl4_blit` is compute-typed on purpose.
        Owned<MTL4::RenderCommandEncoder> mtl4_render{};
        Owned<MTL4::ComputeCommandEncoder> mtl4_compute{};
        Owned<MTL4::ComputeCommandEncoder> mtl4_blit{};
        RenderPassState pass{};
        CommandContext::RenderStateCache render_cache{};
        /// Worker-thread state, reset at each commit.
        SubmissionContents opened{};
    };

    void WorkerThread(std::stop_token stop_token);

    void AcquireNewChunk();

    void DispatchWorkLocked();
    void AllocateNewCommandBuffer();

public:
    /// Records a staging upload onto the upload command buffer, so it doesn't evict the open
    /// render pass (one open encoder is per command buffer, not per queue). Only safe for copies
    /// the buffer cache marks can_reorder_upload: MTL4 runs the two buffers in commit order, so
    /// this lands ahead of every draw in that submission.
    void RecordUpload(std::function<void(MTL4::ComputeCommandEncoder*)>&& command);

private:
    void WorkerCommitUpload();
    void WorkerBeginUpload();

    RenderEncoder WorkerRequestRender(const RenderPassState& state);
    ComputeEncoder WorkerRequestCompute();

    BlitEncoder WorkerRequestBlit();
    void WorkerEndEncoding();

    const Device& device;

    std::unique_ptr<MasterSemaphore> master_semaphore;

    /// MTL4 splits buffer from allocator: the allocator owns the memory commands encode into
    /// and is reset once the GPU is done with it, the buffer is a reusable handle.
    Owned<MTL4::CommandBuffer> mtl4_command_buffer{};

    /// See RecordUpload. Kept out of the main buffer's encoder state machine so the render
    /// encoder stays open across an upload.
    Owned<MTL4::CommandBuffer> mtl4_upload_buffer{};
    Owned<MTL4::ComputeCommandEncoder> mtl4_upload_encoder{};
    bool mtl4_upload_open{false};
    bool mtl4_upload_has_work{false};
    bool mtl4_committed{true};

    /// Compute and blit encoders write the vertex/index buffers and generated textures, so only
    /// after one does a render encoder's vertex stage have to wait. Worker only.
    bool compute_or_blit_since_render_barrier{true};

    /// Allocators round-robin and only reset once the GPU has finished everything encoded from
    /// them, which the parallel tick array records. This is submission depth, not frame depth.
    /// Tried twelve, measured no better than three.
    static constexpr size_t MAX_MTL4_ALLOCATORS = 3;
    std::array<Owned<MTL4::CommandAllocator>, MAX_MTL4_ALLOCATORS> mtl4_allocators{};
    /// An allocator backs one command buffer at a time, so uploads can't share the main ring.
    std::array<Owned<MTL4::CommandAllocator>, MAX_MTL4_ALLOCATORS> mtl4_upload_allocators{};
    std::array<u64, MAX_MTL4_ALLOCATORS> mtl4_allocator_ticks{};
    size_t mtl4_allocator_index{0};

    /// Argument tables, the MTL4 replacement for setVertexBuffer and friends.
    Owned<MTL4::ArgumentTable> mtl4_vertex_table{};
    Owned<MTL4::ArgumentTable> mtl4_fragment_table{};
    Owned<MTL4::ArgumentTable> mtl4_compute_table{};

    /// One ring per allocator, so a ring is only rewound once the GPU has finished reading it.
    std::array<Owned<MTL::Buffer>, MAX_MTL4_ALLOCATORS> mtl4_inline_buffers{};
    std::array<InlineConstantRing, MAX_MTL4_ALLOCATORS> mtl4_inline_rings{};

    /// Zero-filled, always-resident memory an unbound buffer slot points at. The argument tables
    /// outlive every encoder, so a slot we never write keeps the last resource it got.
    Owned<MTL::Buffer> mtl4_null_buffer{};
    NullBindings mtl4_null_bindings{};

    void CreateMetal4Bindings();

    void SweepKeepAlive();

    struct PendingRelease {
        NS::Object* object;
        /// Non-null when the object is also in the device residency set.
        const MTL::Allocation* allocation;
        u64 tick;
    };
    std::mutex keep_alive_mutex;
    std::vector<PendingRelease> keep_alive;
    EncoderState encoder{};

    /// Tick claimed by a Flush that found no command buffer, folded into the next commit so no
    /// Wait is stranded on it. Single writer, so no RMW needed.
    std::atomic<u64> pending_signal_tick{};

    /// See RequestFeedbackBarrier.
    bool pass_wrote_depth{};
    u32 pass_written_attachments{};
    bool pass_storage_writes{};
    bool flush_at_pass_boundary{};
    /// See PassDrew.
    bool pass_drew{};

    /// Mirrors the encoder state on the recording thread, so callers can ask whether a pass is
    /// open without synchronising with the worker.
    bool recorded_pass_active{false};

    /// So RequestRenderPass can tell a new encoder from a request the worker folds into the
    /// open one.
    RenderPassState last_requested_pass{};

    u32 passes_since_flush{};

    std::unique_ptr<CommandChunk> chunk;
    /// Guards `chunk` itself. Taking queue_mutex here instead would mean holding it across
    /// every command's construction.
    std::mutex chunk_mutex;
    std::function<void()> on_submit;

    std::queue<std::unique_ptr<CommandChunk>> work_queue;
    std::vector<std::unique_ptr<CommandChunk>> chunk_reserve;

    std::mutex execution_mutex;
    std::mutex reserve_mutex;
    std::mutex queue_mutex;
    std::condition_variable_any event_cv;
    std::jthread worker_thread;
};

} // namespace Metal
