// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/ns/ns_types.h"
#include "core/hle/service/os/event.h"
#include "core/hle/service/service.h"

namespace Kernel {
class KReadableEvent;
}

namespace Service::NS {

class ISystemUpdateControl;

class ISystemUpdateInterface final : public ServiceFramework<ISystemUpdateInterface> {
public:
    explicit ISystemUpdateInterface(Core::System& system_);
    ~ISystemUpdateInterface() override;

private:
    Result GetBackgroundNetworkUpdateState(
        Out<BackgroundNetworkUpdateState> out_background_network_update_state);
    Result OpenSystemUpdateControl(
        Out<SharedPointer<ISystemUpdateControl>> out_system_update_control);
    Result GetSystemUpdateNotificationEventForContentDelivery(
        OutCopyHandle<Kernel::KReadableEvent> out_event);

private:
    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, D<&ISystemUpdateInterface::GetBackgroundNetworkUpdateState>, "GetBackgroundNetworkUpdateState"},
        FunctionInfo{1, D<&ISystemUpdateInterface::OpenSystemUpdateControl>, "OpenSystemUpdateControl"},
        FunctionInfo{2, nullptr, "NotifyExFatDriverRequired"},
        FunctionInfo{3, nullptr, "ClearExFatDriverStatusForDebug"},
        FunctionInfo{4, nullptr, "RequestBackgroundNetworkUpdate"},
        FunctionInfo{5, nullptr, "NotifyBackgroundNetworkUpdate"},
        FunctionInfo{6, nullptr, "NotifyExFatDriverDownloadedForDebug"},
        FunctionInfo{9, D<&ISystemUpdateInterface::GetSystemUpdateNotificationEventForContentDelivery>, "GetSystemUpdateNotificationEventForContentDelivery"},
        FunctionInfo{10, nullptr, "NotifySystemUpdateForContentDelivery"},
        FunctionInfo{11, nullptr, "PrepareShutdown"},
        FunctionInfo{12, nullptr, "Unknown12"},
        FunctionInfo{13, nullptr, "Unknown13"},
        FunctionInfo{14, nullptr, "Unknown14"},
        FunctionInfo{15, nullptr, "Unknown15"},
        FunctionInfo{16, nullptr, "DestroySystemUpdateTask"},
        FunctionInfo{17, nullptr, "RequestSendSystemUpdate"},
        FunctionInfo{18, nullptr, "GetSendSystemUpdateProgress"}
    );
    KernelHelpers::ServiceContext service_context;
    Event update_notification_event;
};

} // namespace Service::NS
