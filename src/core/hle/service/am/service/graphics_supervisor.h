// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>

#include "core/hle/service/service.h"
#include "core/hle/service/am/service/graphics_syncpoint_cache.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/os/event.h"

namespace Kernel {
class KProcess;
class KTransferMemory;
class KEvent;
}

namespace Core::Timing {
struct EventType;
}
namespace Service::Nvidia {
class Module;
}

namespace Service::AM {
struct NativeImageRegistry;

class IGpuSession;

class IGraphicsSupervisor final : public ServiceFramework<IGraphicsSupervisor> {
public:
    explicit IGraphicsSupervisor(Core::System& system, Kernel::KProcess* process, u64 aruid);
    ~IGraphicsSupervisor() override;
    Result HandleSyncRequest(Kernel::KServerSession& session, HLERequestContext& ctx) override;

private:
    enum class Phase { Created, Initialized, ProcessBound, Ready };
    void Initialize(HLERequestContext& ctx);
    void BindProcess(HLERequestContext& ctx);
    void CompleteBootstrap(HLERequestContext& ctx);
    void InitializeImageContext(HLERequestContext& ctx);
    void GetNvidiaDriver(HLERequestContext& ctx);
    void CalculateImageRequirements(HLERequestContext& ctx);
    void CreateNativeMemory(HLERequestContext& ctx);
    void CreateNativeImage(HLERequestContext& ctx);
    void DestroyNativeImage(HLERequestContext& ctx);
    void ConfigureNativeWindowImagePool(HLERequestContext& ctx);
    void BindNativeMemory(HLERequestContext& ctx);
    void BindNativeGpuMemory(HLERequestContext& ctx);
    void DestroyNativeMemory(HLERequestContext& ctx);
    Result BindNativeMemoryBacking(u32 id, u64 address, u64 size, u64 flags, u64 offset);
    Result ReleaseNativeMemory(u32 id);
    void ConfigureNativeMemory(HLERequestContext& ctx);
    void InitializeFrameContext(HLERequestContext& ctx);
    void RegisterGpuAddressSpace(HLERequestContext& ctx);
    void OpenGpuSession(HLERequestContext& ctx);
    void OpenDisplaySession(HLERequestContext& ctx);
    void InitializeDisplayContext(HLERequestContext& ctx);
    void InitializeGpuContext(HLERequestContext& ctx);
    void InitializeSyncpointContext(HLERequestContext& ctx);
    void GetSyncpointSession(HLERequestContext& ctx);
    void BindSyncpointMemory(HLERequestContext& ctx);
    void RefreshSyncpointCache();

    std::vector<std::weak_ptr<IGpuSession>> m_gpu_sessions;
    std::shared_ptr<Nvidia::Module> m_nv_module;
    std::optional<size_t> m_native_memory_session;
    std::vector<u32> m_native_memories;
    std::shared_ptr<NativeImageRegistry> m_image_registry;
    u32 m_next_native_image_id{1};
    std::shared_ptr<Core::Timing::EventType> m_cache_poll;
    std::array<GraphicsSyncpointCache::CounterValue, GraphicsSyncpointCache::CounterCount>
        m_counter_values{};
    Kernel::KTransferMemory* m_syncpoint_rw{};
    Kernel::KTransferMemory* m_syncpoint_ro{};
    Kernel::KEvent* m_syncpoint_request_event{};
    KernelHelpers::ServiceContext m_context;
    Event m_syncpoint_cache_event;
    bool m_syncpoint_context_initialized{};
    Kernel::KProcess* const m_process;
    const u64 m_aruid;
    Phase m_phase{Phase::Created};
    bool m_display_context_initialized{};
    bool m_gpu_context_initialized{};
    bool m_gpu_context_ready{};
    bool m_frame_context_initialized{};
    bool m_frame_context_ready{};
    bool m_image_context_initialized{};
    bool m_image_formats_initialized{};
};

} // namespace Service::AM
