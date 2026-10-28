// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <optional>
#include "core/hle/service/service.h"
#include "core/hle/service/kernel_helpers.h"

namespace Kernel {
class KClientSession;
class KProcess;
class KEvent;
}
namespace Service::Nvidia { class Module; }
namespace Service::AM {

struct GpuDeviceRegistry;

class IGpuSession final : public ServiceFramework<IGpuSession> {
public:
    IGpuSession(Core::System& system, std::shared_ptr<Nvidia::Module> module,
                Kernel::KProcess* process);
    ~IGpuSession() override;
    Result RegisterAddressSpace(u32 id);
    Result HandleSyncRequest(Kernel::KServerSession& session, HLERequestContext& ctx) override;

private:
    void GetAddressSpacePartition(HLERequestContext& ctx);
    void CreateAddressSpace(HLERequestContext& ctx);
    void CreateGraphicsDevice(HLERequestContext& ctx);
    void DestroyGraphicsDevice(HLERequestContext& ctx);
    void GetNotificationRange(HLERequestContext& ctx);
    void GetNotificationEvent(HLERequestContext& ctx);
    void RequestWorker(HLERequestContext& ctx);
    void AcquireWorker(HLERequestContext& ctx);

    std::shared_ptr<GpuDeviceRegistry> m_devices;
    Kernel::KClientSession* m_worker{};
    Kernel::KProcess* const m_process;
    std::shared_ptr<Nvidia::Module> m_module;
};
} // namespace Service::AM
