// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>

#include "video_core/fence_manager.h"
#include "video_core/renderer_metal/mtl_buffer_cache.h"
#include "video_core/renderer_metal/mtl_texture_cache.h"

namespace Tegra {
class GPU;
}

namespace VideoCore {
class RasterizerInterface;
}

namespace Metal {

class Scheduler;

/// A fence over a scheduler tick. A stubbed fence records nothing and reports
/// signalled immediately, which is how FenceManager avoids submitting for most
/// guest fence signals.
class InnerFence : public VideoCommon::FenceBase {
public:
    explicit InnerFence(Scheduler& scheduler, bool is_stubbed);
    ~InnerFence();

    void Queue();

    [[nodiscard]] bool IsSignaled() const;

    void Wait();

private:
    Scheduler& scheduler;
    u64 wait_tick = 0;
};

using Fence = std::shared_ptr<InnerFence>;

/// Stand-in for the query cache the Metal backend doesn't have yet. With no
/// query cache "nothing pending" is the right answer. Saying otherwise would
/// force a flush on every fence.
class NullQueryCache {
public:
    [[nodiscard]] static bool ShouldWaitAsyncFlushes() {
        return false;
    }
    [[nodiscard]] static bool HasUncommittedFlushes() {
        return false;
    }
    static void PopAsyncFlushes() {}
    static void CommitAsyncFlushes() {}
};

struct FenceManagerParams {
    using FenceType = Fence;
    using BufferCacheType = BufferCache;
    using TextureCacheType = TextureCache;
    using QueryCacheType = NullQueryCache;

    static constexpr bool HAS_ASYNC_CHECK = true;
};

using GenericFenceManager = VideoCommon::FenceManager<FenceManagerParams>;

class FenceManager final : public GenericFenceManager {
public:
    explicit FenceManager(VideoCore::RasterizerInterface& rasterizer, Tegra::GPU& gpu,
                          TextureCache& texture_cache, BufferCache& buffer_cache,
                          NullQueryCache& query_cache, Scheduler& scheduler);

protected:
    Fence CreateFence(bool is_stubbed) override;
    void QueueFence(Fence& fence) override;
    bool IsFenceSignaled(Fence& fence) const override;
    void WaitFence(Fence& fence) override;

private:
    Scheduler& scheduler;
};

} // namespace Metal
