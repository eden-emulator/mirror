// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/uuid.h"
#include "core/hle/service/am/am_types.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::AM {

struct Applet;
class ILibraryAppletAccessor;
class IStorage;
class WindowSystem;

class IApplicationAccessor final : public ServiceFramework<IApplicationAccessor> {
public:
    explicit IApplicationAccessor(Core::System& system_, std::shared_ptr<Applet> applet,
                                  WindowSystem& window_system);
    ~IApplicationAccessor() override;

private:
    Result Start();
    Result RequestExit();
    Result Terminate();
    Result GetResult();
    Result GetAppletStateChangedEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result PushLaunchParameter(LaunchParameterKind kind, SharedPointer<IStorage> storage);
    Result GetApplicationControlProperty(OutBuffer<BufferAttr_HipcMapAlias> out_control_property);
    Result SetUsers(bool enable, InArray<Common::UUID, BufferAttr_HipcMapAlias> user_ids);
    Result GetCurrentLibraryApplet(Out<SharedPointer<ILibraryAppletAccessor>> out_accessor);
    Result RequestForApplicationToGetForeground();
    Result CheckRightsEnvironmentAvailable(Out<bool> out_is_available);
    Result GetNsRightsEnvironmentHandle(Out<u64> out_handle);
    Result ReportApplicationExitTimeout();

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, D<&IApplicationAccessor::GetAppletStateChangedEvent>, "GetAppletStateChangedEvent"},
        FunctionInfo{1, nullptr, "IsCompleted"},
        FunctionInfo{10, D<&IApplicationAccessor::Start>, "Start"},
        FunctionInfo{20, D<&IApplicationAccessor::RequestExit>, "RequestExit"},
        FunctionInfo{25, D<&IApplicationAccessor::Terminate>, "Terminate"},
        FunctionInfo{30, D<&IApplicationAccessor::GetResult>, "GetResult"},
        FunctionInfo{101, D<&IApplicationAccessor::RequestForApplicationToGetForeground>, "RequestForApplicationToGetForeground"},
        FunctionInfo{110, nullptr, "TerminateAllLibraryApplets"},
        FunctionInfo{111, nullptr, "AreAnyLibraryAppletsLeft"},
        FunctionInfo{112, D<&IApplicationAccessor::GetCurrentLibraryApplet>, "GetCurrentLibraryApplet"},
        FunctionInfo{120, nullptr, "GetApplicationId"},
        FunctionInfo{121, D<&IApplicationAccessor::PushLaunchParameter>, "PushLaunchParameter"},
        FunctionInfo{122, D<&IApplicationAccessor::GetApplicationControlProperty>, "GetApplicationControlProperty"},
        FunctionInfo{123, nullptr, "GetApplicationLaunchProperty"},
        FunctionInfo{124, nullptr, "GetApplicationLaunchRequestInfo"},
        FunctionInfo{130, D<&IApplicationAccessor::SetUsers>, "SetUsers"},
        FunctionInfo{131, D<&IApplicationAccessor::CheckRightsEnvironmentAvailable>, "CheckRightsEnvironmentAvailable"},
        FunctionInfo{132, D<&IApplicationAccessor::GetNsRightsEnvironmentHandle>, "GetNsRightsEnvironmentHandle"},
        FunctionInfo{140, nullptr, "GetDesirableUids"},
        FunctionInfo{150, D<&IApplicationAccessor::ReportApplicationExitTimeout>, "ReportApplicationExitTimeout"},
        FunctionInfo{160, nullptr, "SetApplicationAttribute"},
        FunctionInfo{170, nullptr, "HasSaveDataAccessPermission"},
        FunctionInfo{180, nullptr, "PushToFriendInvitationStorageChannel"},
        FunctionInfo{190, nullptr, "PushToNotificationStorageChannel"},
        FunctionInfo{200, nullptr, "RequestApplicationSoftReset"},
        FunctionInfo{201, nullptr, "RestartApplicationTimer"}
    );
    WindowSystem& m_window_system;
    const std::shared_ptr<Applet> m_applet;
};

} // namespace Service::AM
