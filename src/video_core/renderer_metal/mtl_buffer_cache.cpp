// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <atomic>
#include <cstring>
#include <algorithm>
#include <set>
#include <ranges>
#include <span>

#include <boost/container/small_vector.hpp>

#include "common/alignment.h"
#include "common/literals.h"
#include "common/logging.h"
#include "common/settings.h"
#include "video_core/metal_common/metal_device.h"
#include "video_core/renderer_metal/maxwell_to_mtl.h"
#include "video_core/renderer_metal/mtl_buffer_cache.h"
#include "video_core/renderer_metal/mtl_scheduler.h"
#include "video_core/texture_cache/expand_format.h"

namespace Metal {
using namespace Common::Literals;

namespace {
// Indices are absolute, `first` is folded in. Quad and quad-strip winding has to match
// the Vulkan backend's QuadArrayIndexBuffer / QuadStripIndexBuffer, or quads get culled.
void GenerateIndices(Tegra::Engines::Maxwell3D::Regs::PrimitiveTopology topology, u32 first,
                     u32 count, std::span<u32> out) {
    using Topology = Tegra::Engines::Maxwell3D::Regs::PrimitiveTopology;

    size_t at = 0;
    const auto emit = [&](u32 vertex) {
        if (at < out.size()) {
            out[at++] = first + vertex;
        }
    };

    switch (topology) {
    case Topology::Quads:
        for (u32 quad = 0; quad + 4 <= count; quad += 4) {
            for (const u32 corner : {0u, 1u, 2u, 0u, 2u, 3u}) {
                emit(quad + corner);
            }
        }
        break;
    case Topology::QuadStrip:
        for (u32 quad = 0; quad * 2 + 4 <= count; ++quad) {
            for (const u32 corner : {0u, 3u, 1u, 0u, 2u, 3u}) {
                emit(quad * 2 + corner);
            }
        }
        break;
    case Topology::TriangleFan:
    case Topology::Polygon:
        // Every triangle shares vertex 0.
        for (u32 vertex = 1; vertex + 1 < count; ++vertex) {
            emit(0);
            emit(vertex);
            emit(vertex + 1);
        }
        break;
    case Topology::LineLoop:
        // A strip plus the closing segment back to the first vertex.
        for (u32 vertex = 0; vertex < count; ++vertex) {
            emit(vertex);
        }
        emit(0);
        break;
    default:
        break;
    }
}

/// 256 satisfies both macOS (256) and Apple GPUs (4).
constexpr u32 UNIFORM_BUFFER_ALIGNMENT = 256;

constexpr u32 STORAGE_BUFFER_ALIGNMENT = 16;

/// Zero-filled stand-in for a vertex slot the guest never bound. Metal 4 binds a stream
/// as a bare address with no length, so an unfilled slot still fetches `stride * index`
/// off the end. 4 MiB covers a 16-bit index space at the ~40-byte strides Legends Z-A
/// uses; at the 4 KiB this used to be it faulted the GPU.
constexpr u64 NULL_BUFFER_SIZE = 4_MiB;

} // Anonymous namespace

bool SupportsPrimitiveRestart(Tegra::Engines::Maxwell3D::Regs::PrimitiveTopology topology) {
    using Topology = Tegra::Engines::Maxwell3D::Regs::PrimitiveTopology;
    switch (topology) {
    case Topology::Points:
    case Topology::Lines:
    case Topology::Triangles:
    case Topology::LinesAdjacency:
    case Topology::TrianglesAdjacency:
    case Topology::Patches:
        return false;
    default:
        return true;
    }
}

bool NeedsIndexConversion(Tegra::Engines::Maxwell3D::Regs::PrimitiveTopology topology) {
    using Topology = Tegra::Engines::Maxwell3D::Regs::PrimitiveTopology;
    switch (topology) {
    case Topology::Quads:
    case Topology::QuadStrip:
    case Topology::TriangleFan:
    case Topology::Polygon:
    case Topology::LineLoop:
        return true;
    default:
        return false;
    }
}

u32 ConvertedIndexCount(Tegra::Engines::Maxwell3D::Regs::PrimitiveTopology topology, u32 count) {
    using Topology = Tegra::Engines::Maxwell3D::Regs::PrimitiveTopology;
    switch (topology) {
    case Topology::Quads:
        return (count / 4) * 6;
    case Topology::QuadStrip:
        return count >= 4 ? ((count - 2) / 2) * 6 : 0;
    case Topology::TriangleFan:
    case Topology::Polygon:
        return count >= 3 ? (count - 2) * 3 : 0;
    case Topology::LineLoop:
        return count >= 2 ? count + 1 : 0;
    default:
        return count;
    }
}

MTL::PrimitiveType ConvertedPrimitive(Tegra::Engines::Maxwell3D::Regs::PrimitiveTopology topology) {
    using Topology = Tegra::Engines::Maxwell3D::Regs::PrimitiveTopology;
    return topology == Topology::LineLoop ? MTL::PrimitiveTypeLineStrip
                                          : MTL::PrimitiveTypeTriangle;
}

// ---------------------------------------------------------------------------
// Buffer
// ---------------------------------------------------------------------------

Buffer::Buffer(BufferCacheRuntime&, VideoCommon::NullBufferParams null_params)
    : VideoCommon::BufferBase(null_params), tracker{4096}, is_null{true} {}

Buffer::Buffer(BufferCacheRuntime& runtime, VAddr cpu_addr_, u64 size_bytes_, bool)
    : VideoCommon::BufferBase(cpu_addr_, size_bytes_), device{&runtime.device},
      scheduler{&runtime.scheduler}, tracker{size_bytes_} {
    buffer = runtime.memory_allocator.CreateBuffer(size_bytes_, MemoryUsage::DeviceLocal);
    if (!buffer) {
        LOG_ERROR(Render_Metal, "Failed to allocate a {} byte guest buffer", size_bytes_);
        return;
    }
    buffer->setLabel(ToNSString("eden guest buffer"));
}

Buffer::~Buffer() {
    if (scheduler == nullptr) {
        return;
    }

    for (BufferView& view : views) {
        if (view.texture) {
            scheduler->KeepAlive(view.texture.get(), view.texture.get());
        }
    }
    if (buffer) {
        // Passed as an allocation too, so the sweep drops it from the residency set.
        scheduler->KeepAlive(buffer.get(), buffer.get());
    }
}

void Buffer::MarkUsage(u64 offset, u64 size) noexcept {
    if (scheduler != nullptr) {
        const u64 tick = scheduler->CurrentTick();
        if (tick != last_usage_tick) {
            previous_usage_tick = last_usage_tick;
            last_usage_tick = tick;
            tracker.Reset();
        }
    }
    tracker.Track(offset, size);
}

bool Buffer::IsRegionUsed(u64 offset, u64 size) const noexcept {
    if (scheduler != nullptr && last_usage_tick != scheduler->CurrentTick()) {
        return false;
    }
    return tracker.IsUsed(offset, size);
}

bool Buffer::IsUsedInFlight() const noexcept {
    if (scheduler == nullptr) {
        return true;
    }
    const u64 tick =
        last_usage_tick != scheduler->CurrentTick() ? last_usage_tick : previous_usage_tick;
    return tick != 0 && !scheduler->IsFree(tick);
}

MTL::Texture* Buffer::View(u32 offset, u32 size, VideoCore::Surface::PixelFormat format) {
    if (!buffer || device == nullptr) {
        return nullptr;
    }

    for (auto it = views.begin(); it != views.end(); ++it) {
        if (it->offset == offset && it->size == size && it->format == format) {
            if (it != views.begin()) {
                std::rotate(views.begin(), it, it + 1);
            }
            return views.front().texture.get();
        }
    }

    const FormatInfo info = SurfaceFormat(*device, format);
    if (info.support == FormatSupport::Unsupported) {
        LOG_ERROR(Render_Metal, "Texel buffer view with unsupported format {}",
                  static_cast<u32>(format));
        return nullptr;
    }
    // A texel buffer is guest memory with no upload pass to widen the bytes, so a
    // format the texture cache expands would read every texel at the wrong stride.
    if (VideoCommon::IsPixelFormatExpanded(format)) {
        LOG_ERROR(Render_Metal, "Texel buffer view with format {}, which needs upload conversion",
                  static_cast<u32>(format));
        return nullptr;
    }

    const u32 bytes_per_element = VideoCore::Surface::BytesPerBlock(format);
    if (bytes_per_element == 0) {
        return nullptr;
    }

    MTL::TextureDescriptor* descriptor = MTL::TextureDescriptor::alloc()->init();
    descriptor->setTextureType(MTL::TextureTypeTextureBuffer);
    descriptor->setPixelFormat(info.format);
    descriptor->setWidth(size / bytes_per_element);
    descriptor->setHeight(1);
    descriptor->setDepth(1);
    descriptor->setMipmapLevelCount(1);
    descriptor->setUsage(MTL::TextureUsageShaderRead | MTL::TextureUsageShaderWrite);
    descriptor->setStorageMode(buffer->storageMode());

    Owned<MTL::Texture> texture =
        Adopt(buffer->newTexture(descriptor, offset, Common::AlignUp(size, 256)));
    descriptor->release();

    if (!texture) {
        LOG_ERROR(Render_Metal, "Failed to create a texel buffer view");
        return nullptr;
    }

    // A texture over a buffer is its own MTL::Allocation, so the parent buffer being
    // resident does not cover it. Without this a shader sampling a texel buffer page
    // faults and takes out its whole submission ("innocent victim" errors).
    device->AddResidentAllocation(texture.get());

    // Bounded, or a streaming buffer viewed at a moving offset keeps one texture per slice.
    constexpr size_t MAX_VIEWS = 64;
    if (views.size() >= MAX_VIEWS) {
        views.pop_back();
    }
    views.insert(views.begin(), BufferView{
                                    .offset = offset,
                                    .size = size,
                                    .format = format,
                                    .texture = std::move(texture),
                                });
    return views.front().texture.get();
}

// ---------------------------------------------------------------------------
// BufferCacheRuntime
// ---------------------------------------------------------------------------

BufferCacheRuntime::BufferCacheRuntime(const Device& device_, MemoryAllocator& memory_allocator_,
                                       Scheduler& scheduler_, StagingBufferPool& staging_pool_)
    : device{device_}, memory_allocator{memory_allocator_}, scheduler{scheduler_},
      staging_pool{staging_pool_}, index_pass{device_, scheduler_, staging_pool_} {
    ReserveNullBuffer();
}

void BufferCacheRuntime::ReserveNullBuffer() {
    null_buffer = memory_allocator.CreateBuffer(NULL_BUFFER_SIZE, MemoryUsage::DeviceLocal);
    if (!null_buffer) {
        throw Exception("Failed to allocate the null buffer");
    }
    null_buffer->setLabel(ToNSString("eden null buffer"));

    write_sink = memory_allocator.CreateBuffer(NULL_BUFFER_SIZE, MemoryUsage::DeviceLocal);
    if (!write_sink) {
        throw Exception("Failed to allocate the null write sink");
    }
    write_sink->setLabel(ToNSString("eden null write sink"));

    const std::span<u8> mapped = MemoryAllocator::Map(null_buffer.get());
    if (!mapped.empty()) {
        std::fill(mapped.begin(), mapped.end(), u8{0});
        MemoryAllocator::FlushRange(null_buffer.get(), 0, NULL_BUFFER_SIZE);
    } else {
        ClearBuffer(null_buffer.get(), 0, NULL_BUFFER_SIZE, 0);
    }
}

void BufferCacheRuntime::ExtendUniformBinding(BufferBinding& binding, u32 shader_size) {
    // No-op on purpose. Three fixes were tried and all were worse: refilling from guest
    // memory throws away GPU writes, a GPU blit ends the open render pass once per
    // constant buffer per draw, and widening in place assumes the following bytes belong
    // to the same guest buffer. Don't retry one without knowing what those bytes are.
    (void)binding;
    (void)shader_size;
}

void BufferCacheRuntime::TickFrame(Common::SlotVector<Buffer>& slot_buffers) noexcept {
    for (auto it = slot_buffers.begin(); it != slot_buffers.end(); ++it) {
        it->ResetUsageTracking();
    }
    ResetDrawBindings();
}

u64 BufferCacheRuntime::CurrentTick() const noexcept {
    return scheduler.CurrentTick();
}

u64 BufferCacheRuntime::KnownGpuTick() const noexcept {
    return scheduler.GetMasterSemaphore().KnownGpuTick();
}

void BufferCacheRuntime::Wait(u64 buffer_tick) {
    scheduler.Wait(buffer_tick);
}

bool BufferCacheRuntime::IsFree(u64 tick) const {
    return scheduler.IsFree(tick);
}

void BufferCacheRuntime::Finish() {
    scheduler.Finish();
}

u64 BufferCacheRuntime::GetDeviceLocalMemory() const {
    return device.GetRecommendedWorkingSetSize();
}

u64 BufferCacheRuntime::GetDeviceMemoryUsage() const {
    return device.GetDevice()->currentAllocatedSize();
}

bool BufferCacheRuntime::CanReportMemoryUsage() const {
    return true;
}

u32 BufferCacheRuntime::GetUniformBufferAlignment() const {
    return UNIFORM_BUFFER_ALIGNMENT;
}

u32 BufferCacheRuntime::GetStorageBufferAlignment() const {
    return STORAGE_BUFFER_ALIGNMENT;
}

bool BufferCacheRuntime::PrefersDirectUpload() const {
    return memory_allocator.PrefersDirectUpload();
}

StagingBufferRef BufferCacheRuntime::UploadStagingBuffer(size_t size) {
    return staging_pool.Request(size, MemoryUsage::Upload);
}

StagingBufferRef BufferCacheRuntime::DownloadStagingBuffer(size_t size, bool deferred) {
    return staging_pool.Request(size, MemoryUsage::Download, deferred);
}

void BufferCacheRuntime::FreeDeferredStagingBuffer(StagingBufferRef& ref) {
    staging_pool.FreeDeferred(ref);
}

bool BufferCacheRuntime::CanReorderUpload(const Buffer& buffer,
                                          std::span<const VideoCommon::BufferCopy> copies) {
    if (Settings::values.disable_buffer_reorder.GetValue()) {
        return false;
    }
    // Only safe when nothing in the submission being recorded reads the destination range.
    const bool reorderable = std::ranges::none_of(copies, [&buffer](const VideoCommon::BufferCopy& copy) {
        return buffer.IsRegionUsed(copy.dst_offset, copy.size);
    });
    if (reorderable && buffer.IsUsedInFlight()) {
        reordered_upload_needs_barrier = true;
    }
    return reorderable;
}

void BufferCacheRuntime::PreCopyBarrier() {
    // Empty on purpose. Metal 4 has no automatic hazard tracking, but a copy gets its own
    // encoder and Scheduler fires a barrierAfterQueueStages(StageAll) when it opens one.
}

void BufferCacheRuntime::PostCopyBarrier() {
    // See PreCopyBarrier.
}

void BufferCacheRuntime::CopyBuffer(MTL::Buffer* dst_buffer, MTL::Buffer* src_buffer,
                                    std::span<const VideoCommon::BufferCopy> copies, bool barrier,
                                    bool can_reorder_upload) {
    if (src_buffer == nullptr || dst_buffer == nullptr || copies.empty()) {
        return;
    }

    // Metal aborts the process inside copyFromBuffer if a copy runs off either end.
    boost::container::small_vector<VideoCommon::BufferCopy, 8> owned_copies;
    owned_copies.reserve(copies.size());
    const size_t src_size = src_buffer->length();
    const size_t dst_size = dst_buffer->length();
    for (const VideoCommon::BufferCopy& copy : copies) {
        if (copy.src_offset + copy.size > src_size || copy.dst_offset + copy.size > dst_size) {
            static std::atomic<u32> rejected{0};
            const u32 n = ++rejected;
            if (n == 1 || n % 500 == 0) {
                const auto label = [](MTL::Buffer* buffer) {
                    NS::String* name = buffer->label();
                    return name != nullptr ? name->utf8String() : "<unlabelled>";
                };
                LOG_ERROR(Render_Metal,
                          "Rejected out-of-bounds buffer copy #{}: src '{}' {}+{} of {}, "
                          "dst '{}' {}+{} of {}",
                          n, label(src_buffer), copy.src_offset, copy.size, src_size,
                          label(dst_buffer), copy.dst_offset, copy.size, dst_size);
            }
            continue;
        }
        owned_copies.push_back(copy);
    }
    if (owned_copies.empty()) {
        return;
    }

    // The shared cache never flushes, Vulkan's staging memory is host coherent.
    for (const VideoCommon::BufferCopy& copy : owned_copies) {
        MemoryAllocator::FlushRange(src_buffer, copy.src_offset, copy.size);
    }

    // Reorderable uploads get their own command buffer and leave the render pass open,
    // the path below ends it. Disabling this cost Odyssey 43 fps -> 28. Re-enabled:
    // Legends Z-A's wasted shadow map tile traffic halved, 26.0 -> 12.6 GB/s and
    // 2087 -> 1058 store/reload round trips a second, with no frame rate change.
    if (can_reorder_upload) {
        // An earlier submission still running may read what this overwrites.
        const bool wait_for_queue = std::exchange(reordered_upload_needs_barrier, false);
        scheduler.RecordUpload([src_buffer, dst_buffer, wait_for_queue,
                                copies = owned_copies](MTL4::ComputeCommandEncoder* encoder) {
                if (wait_for_queue) {
                    encoder->barrierAfterQueueStages(
                        MTL::StageAll,
                        static_cast<MTL::Stages>(MTL::StageDispatch | MTL::StageBlit),
                        MTL4::VisibilityOptionDevice);
                }
                for (const VideoCommon::BufferCopy& copy : copies) {
                    encoder->copyFromBuffer(src_buffer, copy.src_offset, dst_buffer,
                                            copy.dst_offset, copy.size);
                }
            });
        return;
    }

    // Without this the recording thread's mirror still reports a pass open.
    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([src_buffer, dst_buffer,
                      copies = std::move(owned_copies)](CommandContext& context) {
        BlitEncoder encoder = context.RequestBlit();
        if (encoder == nullptr) {
            return;
        }
        for (const VideoCommon::BufferCopy& copy : copies) {
            encoder->copyFromBuffer(src_buffer, copy.src_offset, dst_buffer, copy.dst_offset,
                                    copy.size);
        }
    });
}

void BufferCacheRuntime::ClearBuffer(MTL::Buffer* dest_buffer, u32 offset, size_t size,
                                     u32 value) {
    if (dest_buffer == nullptr || size == 0) {
        return;
    }

    // A blit fill takes a byte pattern, anything else goes through the compute fill.
    const u8 byte_value = static_cast<u8>(value & 0xFF);
    const bool uniform_bytes = (value & 0xFF) == ((value >> 8) & 0xFF) &&
                               (value & 0xFF) == ((value >> 16) & 0xFF) &&
                               (value & 0xFF) == ((value >> 24) & 0xFF);

    const size_t words = size / sizeof(u32);
    if (!uniform_bytes && words > 0 && index_pass.IsValid()) {
        index_pass.Fill(dest_buffer, offset, static_cast<u32>(words), value);
        // The compute fill can't do a trailing partial word, guest clears are aligned.
        if (size % sizeof(u32) != 0) {
            LOG_DEBUG(Render_Metal, "Buffer clear of {} bytes has a partial trailing word", size);
        }
        return;
    }

    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([dest_buffer, offset, size, byte_value](CommandContext& context) {
        BlitEncoder encoder = context.RequestBlit();
        if (encoder == nullptr) {
            return;
        }
        encoder->fillBuffer(dest_buffer, NS::Range::Make(offset, size), byte_value);
    });
}

std::span<u8> BufferCacheRuntime::BindMappedUniformBuffer(size_t stage, u32 binding_index,
                                                          u32 size) {
    // Sub-allocated out of one chunk instead of hitting the pool per binding, which once
    // measured 40% of all Draw time. The chunk MUST be dropped when the submission tick
    // changes, or a write into it can race a region the pool has reissued.
    const u32 aligned = Common::AlignUp(size, UNIFORM_BUFFER_ALIGNMENT);
    const u64 tick = scheduler.CurrentTick();

    if (uniform_arena.buffer == nullptr || uniform_arena.tick != tick ||
        uniform_arena.used + aligned > uniform_arena.span.size()) {
        const size_t chunk = (std::max)(UNIFORM_ARENA_SIZE, static_cast<size_t>(aligned));
        const StagingBufferRef ref = staging_pool.Request(chunk, MemoryUsage::Upload);
        if (ref.buffer == nullptr || ref.mapped_span.size() < aligned) {
            // The pool could not serve the chunk, so bind directly.
            const StagingBufferRef single = staging_pool.Request(size, MemoryUsage::Upload);
            BindBuffer(single.buffer, static_cast<u32>(single.offset), size);
            return single.mapped_span;
        }
        uniform_arena = UniformArena{
            .buffer = ref.buffer,
            .span = ref.mapped_span,
            .base = static_cast<u32>(ref.offset),
            .used = 0,
            .tick = tick,
        };
    }

    const u32 at = uniform_arena.used;
    uniform_arena.used += aligned;
    // Stream buffer slots and Metal's constant buffers are both 256-aligned.
    BindBuffer(uniform_arena.buffer, uniform_arena.base + at, size);
    return uniform_arena.span.subspan(at, size);
}

void BufferCacheRuntime::BindBuffer(MTL::Buffer* buffer, u32 offset, u32 size, bool is_written) {
    MTL::Buffer* const null_target = is_written ? write_sink.get() : null_buffer.get();
    pending_bindings.push_back(BufferBinding{
        .buffer = buffer != nullptr ? buffer : null_target,
        .offset = buffer != nullptr ? offset : 0,
        .size = buffer != nullptr ? size : static_cast<u32>(NULL_BUFFER_SIZE),
        .is_written = is_written,
    });
}

void BufferCacheRuntime::BindTextureBuffer(Buffer& buffer, u32 offset, u32 size,
                                           VideoCore::Surface::PixelFormat format) {
    // MSL declares a texel buffer as `texture_buffer<>`, so the argument buffer wants a
    // texture handle. Not the texture cache's image view for the same descriptor.
    texel_views.push_back(buffer.View(offset, size, format));
}

void BufferCacheRuntime::BindIndexBuffer(PrimitiveTopology topology, IndexFormat index_format,
                                         u32 first_index, u32 num_indices, MTL::Buffer* buffer,
                                         u32 offset, u32 size) {
    index_binding = IndexBufferBinding{};
    if (buffer == nullptr) {
        return;
    }

    const u32 index_size = index_format == IndexFormat::UnsignedByte    ? 1
                           : index_format == IndexFormat::UnsignedShort ? 2
                                                                        : 4;
    // Applied to the source, because a converted buffer starts at its own index zero.
    const u32 source_offset = offset + first_index * index_size;

    const auto bind_converted = [&](const ConvertedIndices& converted) {
        if (!converted.valid) {
            return false;
        }
        index_binding.buffer = converted.buffer;
        index_binding.offset = converted.offset;
        index_binding.type = MTL::IndexTypeUInt32;
        index_binding.num_indices = converted.num_indices;
        index_binding.converted = true;
        index_binding.valid = true;
        return true;
    };

    // The guest's indices may still have writes in flight, so a compute pass expands them.
    if (NeedsIndexConversion(topology)) {
        if (!bind_converted(index_pass.Convert(topology, index_format, num_indices, 0, buffer,
                                               source_offset))) {
            LOG_WARNING(Render_Metal, "Could not convert indexed topology {}",
                        static_cast<u32>(topology));
        }
        return;
    }

    // Metal always restarts at the index type's maximum and offers no register to move it
    // or switch it off, so any other guest restart index means rewriting to 32-bit.
    const u32 metal_restart =
        index_format == IndexFormat::UnsignedShort ? 0xFFFFu : 0xFFFFFFFFu;
    const bool topology_restarts = SupportsPrimitiveRestart(topology);
    // 0xFFFFFFFF while drawing 16-bit indices means "never restart", and guests do it.
    const u32 format_max = index_format == IndexFormat::UnsignedShort ? 0xFFFFu
                           : index_format == IndexFormat::UnsignedByte ? 0xFFu
                                                                       : 0xFFFFFFFFu;
    const bool restart_reachable = restart_index <= format_max;
    // Every one of these conditions matters: rewriting an index buffer costs a compute
    // dispatch, which ends the open render encoder and forces a store and reload of every
    // attachment. Widening one that did not need it cost ~170 dispatches per submission.
    const bool needs_restart_remap = restart_enabled && topology_restarts &&
                                     restart_reachable &&
                                     index_format != IndexFormat::UnsignedByte &&
                                     restart_index != metal_restart;

    // Restart turned OFF is its own problem: Metal can't turn it off, so a 16-bit strip
    // still gets cut at a legitimate vertex 0xFFFF. Widening to 32-bit fixes that.
    const bool needs_restart_disable_widen = !restart_enabled && topology_restarts &&
                                             index_format == IndexFormat::UnsignedShort;

    // A LIST topology with restart enabled is the case both branches above miss, and the
    // one that faults the GPU. Metal restarts on strips and fans only, so a marker in a
    // list buffer is just vertex 0xFFFF, and against Legends Z-A's 36-byte stride that
    // reads 2.3 MB past a 192 KiB stream with validation staying quiet. Real hardware
    // drops the primitive holding the marker, so the widen pass collapses it to zero area.
    const u32 list_verts_per_prim =
        restart_enabled && !topology_restarts && restart_reachable &&
                index_format != IndexFormat::UnsignedByte
            ? IndexPass::VerticesPerPrimitive(topology)
            : 0;
    const bool needs_list_restart_scrub = list_verts_per_prim > 1;

    // Metal has no 8-bit index type, so widen.
    if (index_format == IndexFormat::UnsignedByte || needs_restart_remap ||
        needs_restart_disable_widen || needs_list_restart_scrub) {
        const u32 remap = restart_enabled ? restart_index : 0xFFFFFFFFu;
        if (!bind_converted(index_pass.Widen(index_format, num_indices, 0, buffer, source_offset,
                                             remap, list_verts_per_prim))) {
            LOG_WARNING(Render_Metal, "Could not widen an index buffer");
        }
        return;
    }

    index_binding.buffer = buffer;
    index_binding.offset = offset;
    index_binding.type = index_format == IndexFormat::UnsignedInt ? MTL::IndexTypeUInt32
                                                                  : MTL::IndexTypeUInt16;
    index_binding.valid = true;
}

void BufferCacheRuntime::BindQuadIndexBuffer(PrimitiveTopology topology, u32 first, u32 count) {
    // The shared cache only routes quads here, Vulkan draws fans and polygons natively.
    BindGeneratedIndexBuffer(topology, first, count);
}

bool BufferCacheRuntime::BindGeneratedIndexBuffer(PrimitiveTopology topology, u32 first,
                                                  u32 count) {
    index_binding = IndexBufferBinding{};

    const u32 num_indices = ConvertedIndexCount(topology, count);
    if (num_indices == 0) {
        return false;
    }
    const size_t bytes = static_cast<size_t>(num_indices) * sizeof(u32);

    // Tried caching this by (topology, first, count) and reverted it: quad draws that vary
    // in size change the key nearly every draw, each costing a newBuffer plus a residency
    // commit. Doing it properly means one buffer plus a baseVertex, as vk_buffer_cache does.
    const StagingBufferRef ref = staging_pool.Request(bytes, MemoryUsage::Upload);
    if (ref.buffer == nullptr || ref.mapped_span.size() < bytes) {
        LOG_WARNING(Render_Metal, "No staging space for {} generated indices", num_indices);
        return false;
    }

    // The mapped span is only byte aligned, and an unaligned u32 store is undefined.
    boost::container::small_vector<u32, 256> indices(num_indices);
    GenerateIndices(topology, first, count, indices);
    std::memcpy(ref.mapped_span.data(), indices.data(), bytes);
    // Required on a discrete Mac, where Managed storage keeps separate copies.
    MemoryAllocator::FlushRange(ref.buffer, ref.offset, bytes);

    index_binding.buffer = ref.buffer;
    index_binding.offset = static_cast<u32>(ref.offset);
    // The expansion writes absolute vertex numbers, which can exceed 16 bits.
    index_binding.type = MTL::IndexTypeUInt32;
    index_binding.num_indices = num_indices;
    index_binding.converted = true;
    index_binding.valid = true;
    return true;
}

void BufferCacheRuntime::BindVertexBuffer(u32 index, MTL::Buffer* buffer, u32 offset, u32 size,
                                          u32 stride) {
    if (index >= vertex_bindings.size()) {
        return;
    }
    // Vertex streams bind directly to encoder slots, not through the argument buffer.
    vertex_bindings[index] = BufferBinding{
        .buffer = buffer != nullptr ? buffer : null_buffer.get(),
        .offset = buffer != nullptr ? offset : 0,
        .size = size,
    };
}

void BufferCacheRuntime::BindVertexBuffers(VideoCommon::HostBindings<Buffer>& bindings) {
    for (u32 index = 0; index < bindings.buffers.size(); ++index) {
        BindVertexBuffer(bindings.min_index + index,
                         bindings.buffers[index] ? bindings.buffers[index]->Handle() : nullptr,
                         static_cast<u32>(bindings.offsets[index]),
                         static_cast<u32>(bindings.sizes[index]),
                         static_cast<u32>(bindings.strides[index]));
    }
}

void BufferCacheRuntime::BindTransformFeedbackBuffer(u32 index, MTL::Buffer* buffer, u32 offset,
                                                     u32 size) {
    // Metal has no stream-out stage, capture runs the vertex program as a kernel.
    if (index >= transform_feedback_bindings.size()) {
        return;
    }
    transform_feedback_bindings[index] = BufferBinding{
        .buffer = buffer,
        .offset = offset,
        .size = size,
    };
}

void BufferCacheRuntime::BindTransformFeedbackBuffers(VideoCommon::HostBindings<Buffer>& bindings) {
    // min_index is ignored on purpose. HostBindings only computes it for vertex buffers,
    // so adding it here pushes every slot out of bounds and drops all four bindings.
    for (u32 index = 0; index < bindings.buffers.size(); ++index) {
        BindTransformFeedbackBuffer(index,
                                    bindings.buffers[index] ? bindings.buffers[index]->Handle()
                                                            : nullptr,
                                    static_cast<u32>(bindings.offsets[index]),
                                    static_cast<u32>(bindings.sizes[index]));
    }
}

} // namespace Metal
