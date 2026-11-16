// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <limits>
#include <span>
#include <vector>

#include "video_core/buffer_cache/buffer_cache_base.h"
#include "video_core/buffer_cache/memory_tracker_base.h"
#include "video_core/buffer_cache/usage_tracker.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/metal_common/metal_memory_allocator.h"
#include "video_core/metal_common/metal_wrapper.h"
#include "video_core/renderer_metal/mtl_index_pass.h"
#include "video_core/renderer_metal/mtl_staging_buffer_pool.h"
#include "video_core/surface.h"

namespace Metal {

class Device;
class Scheduler;
class BufferCacheRuntime;

class Buffer : public VideoCommon::BufferBase {
public:
    explicit Buffer(BufferCacheRuntime&, VideoCommon::NullBufferParams null_params);
    /// @p sparse_compatible is ignored, Metal has no sparse buffer binding.
    explicit Buffer(BufferCacheRuntime& runtime, VAddr cpu_addr_, u64 size_bytes_,
                    bool sparse_compatible);

    /// Hands the allocation to the scheduler instead of freeing it here. Metal 4 command
    /// buffers don't retain what they touch, and the shared cache kills a Buffer as soon
    /// as it invalidates the region, maybe while a submitted command buffer still reads it.
    ~Buffer();

    // Declaring the destructor suppresses the implicit move operations, and the
    // slot vector the cache stores these in needs them. Moving leaves the source
    // holding a null allocation, so its destructor has nothing to defer.
    Buffer(Buffer&&) noexcept = default;
    Buffer& operator=(Buffer&&) noexcept = default;
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    /// A linear MTLTexture over a range of this buffer, for texel buffer bindings.
    /// Cached per (offset, size, format), creating one is not free.
    [[nodiscard]] MTL::Texture* View(u32 offset, u32 size, VideoCore::Surface::PixelFormat format);

    [[nodiscard]] MTL::Buffer* Handle() const noexcept {
        return buffer.get();
    }

    /// Whether the submission being recorded reads the range. Reorderable uploads are
    /// committed ahead of that submission, so earlier ones do not count.
    [[nodiscard]] bool IsRegionUsed(u64 offset, u64 size) const noexcept;

    /// Whether a submission the GPU may still be executing reads this buffer, which a
    /// reordered upload then has to wait for.
    [[nodiscard]] bool IsUsedInFlight() const noexcept;

    void MarkUsage(u64 offset, u64 size) noexcept;

    void ResetUsageTracking() noexcept {
        tracker.Reset();
    }

    [[nodiscard]] u64 LastUsageTick() const noexcept {
        return last_usage_tick;
    }

    operator MTL::Buffer*() const noexcept {
        return buffer.get();
    }

private:
    struct BufferView {
        u32 offset{};
        u32 size{};
        VideoCore::Surface::PixelFormat format{};
        Owned<MTL::Texture> texture;
    };

    const Device* device{};
    Scheduler* scheduler{};
    Owned<MTL::Buffer> buffer;
    std::vector<BufferView> views;
    VideoCommon::UsageTracker tracker;
    u64 last_usage_tick{};
    u64 previous_usage_tick{};
    bool is_null{};
};

/// A resource binding recorded for the next draw or dispatch, consumed by the
/// rasterizer when it writes the argument buffer.
struct BufferBinding {
    MTL::Buffer* buffer{};
    u32 offset{};
    u32 size{};
    /// Whether a shader writes through this binding. Metal can't see through an argument
    /// buffer, so a written buffer needs ResourceUsageWrite or the write is undefined and
    /// nothing orders a later read against it.
    bool is_written{};
};

/// Where the index buffer for the next draw lives. Metal takes it as an argument to the
/// draw call rather than as bound state, so we record it here and consume it at encode
/// time.
struct IndexBufferBinding {
    MTL::Buffer* buffer{};
    u32 offset{};
    MTL::IndexType type{MTL::IndexTypeUInt16};
    bool valid{};
    /// Indices in the bound buffer after conversion. Only means anything when
    /// `converted` is set.
    u32 num_indices{};
    /// The bound buffer is one we generated, not the guest's. The draw has to use
    /// `num_indices` and start from index zero, the guest's first-index offset was
    /// already folded in when it was built.
    bool converted{};
};

/// Whether Metal has no primitive type for @p topology. Metal draws points, lines, line
/// strips, triangles and triangle strips. Fans, quads, quad strips, polygons and line
/// loops have to be expanded into an index buffer first.
[[nodiscard]] bool NeedsIndexConversion(
    Tegra::Engines::Maxwell3D::Regs::PrimitiveTopology topology);

/// Indices the expansion of @p count vertices produces, or @p count for a topology that
/// needs no expansion.
[[nodiscard]] u32 ConvertedIndexCount(
    Tegra::Engines::Maxwell3D::Regs::PrimitiveTopology topology, u32 count);

/// The Metal primitive an expanded @p topology is drawn with.
[[nodiscard]] MTL::PrimitiveType ConvertedPrimitive(
    Tegra::Engines::Maxwell3D::Regs::PrimitiveTopology topology);

class BufferCacheRuntime {
    friend Buffer;

    using PrimitiveTopology = Tegra::Engines::Maxwell3D::Regs::PrimitiveTopology;
    using IndexFormat = Tegra::Engines::Maxwell3D::Regs::IndexFormat;

public:
    explicit BufferCacheRuntime(const Device& device_, MemoryAllocator& memory_allocator_,
                                Scheduler& scheduler_, StagingBufferPool& staging_pool_);

    void TickFrame(Common::SlotVector<Buffer>& slot_buffers) noexcept;

    [[nodiscard]] u64 CurrentTick() const noexcept;
    [[nodiscard]] u64 KnownGpuTick() const noexcept;
    void Wait(u64 buffer_tick);

    [[nodiscard]] bool IsFree(u64 tick) const;
    void Finish();

    [[nodiscard]] u64 GetDeviceLocalMemory() const;
    [[nodiscard]] u64 GetDeviceMemoryUsage() const;
    [[nodiscard]] bool CanReportMemoryUsage() const;

    [[nodiscard]] u32 GetUniformBufferAlignment() const;
    [[nodiscard]] u32 GetStorageBufferAlignment() const;

    [[nodiscard]] StagingBufferRef UploadStagingBuffer(size_t size);
    [[nodiscard]] StagingBufferRef DownloadStagingBuffer(size_t size, bool deferred = false);

    [[nodiscard]] bool CanReorderUpload(const Buffer& buffer,
                                        std::span<const VideoCommon::BufferCopy> copies);

    void FreeDeferredStagingBuffer(StagingBufferRef& ref);

    /// Both barrier hooks are empty on purpose, see the note in the .cpp.
    void PreCopyBarrier();
    void PostCopyBarrier();

    /// Destination first, matching the shared BufferCache's call convention.
    /// Reversing these silently reverses every upload.
    void CopyBuffer(MTL::Buffer* dst_buffer, MTL::Buffer* src_buffer,
                    std::span<const VideoCommon::BufferCopy> copies, bool barrier,
                    bool can_reorder_upload = false);

    void ClearBuffer(MTL::Buffer* dest_buffer, u32 offset, size_t size, u32 value);

    /// The guest's primitive restart state, for the next index buffer binding. Passed
    /// separately because the shared cache's BindIndexBuffer signature is fixed, same
    /// shape as SetDrawIndirect.
    void SetPrimitiveRestart(bool enabled, u32 index) noexcept {
        restart_enabled = enabled;
        restart_index = index;
    }

    /// @param first_index First index the draw reads, in indices. The shared cache
    ///        passes `index_buffer.first` here.
    /// @param num_indices `index_buffer.count`.
    void BindIndexBuffer(PrimitiveTopology topology, IndexFormat index_format, u32 first_index,
                         u32 num_indices, MTL::Buffer* buffer, u32 offset, u32 size);

    void BindQuadIndexBuffer(PrimitiveTopology topology, u32 first, u32 count);

    /// Expands a non-indexed draw of @p topology into a generated index buffer and binds
    /// it. Returns false when nothing was bound, and then the draw has to be dropped
    /// instead of issued with the guest's own vertices.
    bool BindGeneratedIndexBuffer(PrimitiveTopology topology, u32 first, u32 count);

    void BindVertexBuffer(u32 index, MTL::Buffer* buffer, u32 offset, u32 size, u32 stride);
    void BindVertexBuffers(VideoCommon::HostBindings<Buffer>& bindings);

    void BindTransformFeedbackBuffer(u32 index, MTL::Buffer* buffer, u32 offset, u32 size);
    void BindTransformFeedbackBuffers(VideoCommon::HostBindings<Buffer>& bindings);

    /// Buffers a capture would write into, by stream index.
    [[nodiscard]] std::span<const BufferBinding> GetTransformFeedbackBindings() const noexcept {
        return transform_feedback_bindings;
    }

    [[nodiscard]] std::span<u8> BindMappedUniformBuffer(size_t stage, u32 binding_index, u32 size);

    void BindUniformBuffer(MTL::Buffer* buffer, u32 offset, u32 size) {
        BindBuffer(buffer, offset, size);
    }

    void BindStorageBuffer(MTL::Buffer* buffer, u32 offset, u32 size, bool is_written) {
        BindBuffer(buffer, offset, size, is_written);
    }

    void BindTextureBuffer(Buffer& buffer, u32 offset, u32 size,
                           VideoCore::Surface::PixelFormat format);

    [[nodiscard]] bool ShouldLimitDynamicStorageBuffers() const {
        return false;
    }

    [[nodiscard]] u32 GetMaxDynamicStorageBuffers() const {
        return (std::numeric_limits<u32>::max)();
    }

    /// True when guest data can go straight into a destination buffer with no staging
    /// copy. See MemoryAllocator::PrefersDirectUpload.
    [[nodiscard]] bool PrefersDirectUpload() const;

    /// Vertex buffers recorded for the next draw, indexed by binding slot.
    [[nodiscard]] std::span<const BufferBinding> GetVertexBindings() const noexcept {
        return vertex_bindings;
    }

    [[nodiscard]] const IndexBufferBinding& GetIndexBinding() const noexcept {
        return index_binding;
    }

    /// The index rewriting pass. Exposed because geometry shader emulation drives it
    /// directly, for its own reasons rather than anything this cache asks of it.
    [[nodiscard]] IndexPass& GetIndexPass() noexcept {
        return index_pass;
    }

    /// Would widen @p binding so a shader can't read past what is bound. A no-op for now,
    /// see the .cpp for why every attempt was worse than leaving the binding alone. Metal
    /// has no robustBufferAccess, so a guest binding a constant buffer shorter than the
    /// block the shader declares reads undefined bytes.
    void ExtendUniformBinding(BufferBinding& binding, u32 shader_size);

private:
public:

    /// Buffer bound where a shader expects an unbound slot to read as zero.
    [[nodiscard]] MTL::Buffer* GetNullBuffer() const noexcept {
        return null_buffer.get();
    }

    /// Uniform and storage buffers the shared cache pushed since the last
    /// ClearPendingBindings(), in the order the shader declares them, which is the order
    /// the argument buffer expects them written.
    [[nodiscard]] std::span<const BufferBinding> GetPendingBindings() const noexcept {
        return pending_bindings;
    }

    /// Texel buffer views pushed since the last ClearPendingBindings(), in the order the
    /// shader declares them.
    [[nodiscard]] std::span<MTL::Texture* const> GetTexelViews() const noexcept {
        return texel_views;
    }

    void ClearPendingBindings() noexcept {
        pending_bindings.clear();
        texel_views.clear();
    }

    void ResetDrawBindings() noexcept {
        pending_bindings.clear();
        texel_views.clear();
        index_binding = {};
    }

private:
    void BindBuffer(MTL::Buffer* buffer, u32 offset, u32 size, bool is_written = false);
    void ReserveNullBuffer();

    const Device& device;
    MemoryAllocator& memory_allocator;
    Scheduler& scheduler;
    StagingBufferPool& staging_pool;

    Owned<MTL::Buffer> null_buffer;
    /// Bound for a written storage buffer the guest left unbound. Shaders store through
    /// those regardless, and the stores must not reach null_buffer, which has to keep
    /// reading as zero for constant vertex attributes.
    Owned<MTL::Buffer> write_sink;

    /// One stream-buffer chunk we carve per-draw uniform bindings out of, instead of
    /// asking the staging pool for each of them.
    static constexpr size_t UNIFORM_ARENA_SIZE = 1024 * 1024;

    struct UniformArena {
        MTL::Buffer* buffer{};
        std::span<u8> span;
        u32 base{};
        u32 used{};
        u64 tick{};
    };
    UniformArena uniform_arena;
    /// Set by CanReorderUpload, consumed by the CopyBuffer that follows it.
    bool reordered_upload_needs_barrier{};

    /// Rewrites index buffers Metal can't draw from. Held by value because it owns a
    /// compute pipeline we only want to build once.
    IndexPass index_pass;

    // Resource bindings destined for the argument buffer.
    std::vector<BufferBinding> pending_bindings;
    std::vector<MTL::Texture*> texel_views;

    /// Vertex streams, by binding slot. Fixed size so a slot index stays stable between
    /// recording and encoding.
    std::array<BufferBinding, 33> vertex_bindings{};
    IndexBufferBinding index_binding{};

    /// Where a transform feedback capture would write. Maxwell has four streams and the
    /// shared cache binds them by index.
    std::array<BufferBinding, 4> transform_feedback_bindings{};

    bool restart_enabled{};
    u32 restart_index{0xFFFFFFFFu};
};

struct BufferCacheParams {
    using Runtime = Metal::BufferCacheRuntime;
    using Buffer = Metal::Buffer;
    using Async_Buffer = Metal::StagingBufferRef;
    using MemoryTracker = VideoCommon::MemoryTrackerBase<Tegra::MaxwellDeviceMemoryManager>;

    static constexpr bool IS_OPENGL = false;
    static constexpr bool HAS_PERSISTENT_UNIFORM_BUFFER_BINDINGS = false;
    static constexpr bool HAS_FULL_INDEX_AND_PRIMITIVE_SUPPORT = false;
    static constexpr bool NEEDS_BIND_UNIFORM_INDEX = false;
    static constexpr bool NEEDS_BIND_STORAGE_INDEX = false;
    static constexpr bool USE_MEMORY_MAPS = true;

    /// Bind and synchronize what the shader declares, not what the guest bound. A trait
    /// instead of a fix-up in the Metal runtime because the size has to be settled BEFORE
    /// the synchronize: BindHostGraphicsUniformBuffer synchronizes just the bytes it is
    /// about to bind, so widening afterwards reads memory we never uploaded for this draw.
    static constexpr bool NEEDS_UNIFORM_OVER_READ_PROTECTION = true;
    static constexpr bool SEPARATE_IMAGE_BUFFER_BINDINGS = false;
    static constexpr bool USE_MEMORY_MAPS_FOR_UPLOADS = true;
    static constexpr bool STREAM_DIRTY_UNIFORM_BUFFERS = true;
    static constexpr bool NEEDS_STORAGE_OVERRUN_ROOM = true;
};

using BufferCache = VideoCommon::BufferCache<BufferCacheParams>;

} // namespace Metal
