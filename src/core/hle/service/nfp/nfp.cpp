// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/logging.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/nfp/nfp.h"
#include "core/hle/service/nfp/nfp_interface.h"
#include "core/hle/service/server_manager.h"

namespace Service::NFP {

class IUser final : public Interface {
public:
    explicit IUser(Core::System& system_) : Interface(system_, "NFP:IUser") {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMapWithClass<IUser>(
        FunctionInfoTyped<IUser>{0, &IUser::Initialize, "Initialize"},
        FunctionInfoTyped<IUser>{1, &IUser::Finalize, "Finalize"},
        FunctionInfoTyped<IUser>{2, &IUser::ListDevices, "ListDevices"},
        FunctionInfoTyped<IUser>{3, &IUser::StartDetection, "StartDetection"},
        FunctionInfoTyped<IUser>{4, &IUser::StopDetection, "StopDetection"},
        FunctionInfoTyped<IUser>{5, &IUser::Mount, "Mount"},
        FunctionInfoTyped<IUser>{6, &IUser::Unmount, "Unmount"},
        FunctionInfoTyped<IUser>{7, &IUser::OpenApplicationArea, "OpenApplicationArea"},
        FunctionInfoTyped<IUser>{8, &IUser::GetApplicationArea, "GetApplicationArea"},
        FunctionInfoTyped<IUser>{9, &IUser::SetApplicationArea, "SetApplicationArea"},
        FunctionInfoTyped<IUser>{10, &IUser::Flush, "Flush"},
        FunctionInfoTyped<IUser>{11, &IUser::Restore, "Restore"},
        FunctionInfoTyped<IUser>{12, &IUser::CreateApplicationArea, "CreateApplicationArea"},
        FunctionInfoTyped<IUser>{13, &IUser::GetTagInfo, "GetTagInfo"},
        FunctionInfoTyped<IUser>{14, &IUser::GetRegisterInfo, "GetRegisterInfo"},
        FunctionInfoTyped<IUser>{15, &IUser::GetCommonInfo, "GetCommonInfo"},
        FunctionInfoTyped<IUser>{16, &IUser::GetModelInfo, "GetModelInfo"},
        FunctionInfoTyped<IUser>{17, &IUser::AttachActivateEvent, "AttachActivateEvent"},
        FunctionInfoTyped<IUser>{18, &IUser::AttachDeactivateEvent, "AttachDeactivateEvent"},
        FunctionInfoTyped<IUser>{19, &IUser::GetState, "GetState"},
        FunctionInfoTyped<IUser>{20, &IUser::GetDeviceState, "GetDeviceState"},
        FunctionInfoTyped<IUser>{21, &IUser::GetNpadId, "GetNpadId"},
        FunctionInfoTyped<IUser>{22, &IUser::GetApplicationAreaSize, "GetApplicationAreaSize"},
        FunctionInfoTyped<IUser>{23, &IUser::AttachAvailabilityChangeEvent, "AttachAvailabilityChangeEvent"},
        FunctionInfoTyped<IUser>{24, &IUser::RecreateApplicationArea, "RecreateApplicationArea"},
        FunctionInfoTyped<IUser>{25, &IUser::StartDetection, "StartDetectionWithFilter"}
    );
};

class ISystem final : public Interface {
public:
    explicit ISystem(Core::System& system_) : Interface(system_, "NFP:ISystem") {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMapWithClass<ISystem>(
        FunctionInfoTyped<ISystem>{0, &ISystem::InitializeSystem, "InitializeSystem"},
        FunctionInfoTyped<ISystem>{1, &ISystem::FinalizeSystem, "FinalizeSystem"},
        FunctionInfoTyped<ISystem>{2, &ISystem::ListDevices, "ListDevices"},
        FunctionInfoTyped<ISystem>{3, &ISystem::StartDetection, "StartDetection"},
        FunctionInfoTyped<ISystem>{4, &ISystem::StopDetection, "StopDetection"},
        FunctionInfoTyped<ISystem>{5, &ISystem::Mount, "Mount"},
        FunctionInfoTyped<ISystem>{6, &ISystem::Unmount, "Unmount"},
        FunctionInfoTyped<ISystem>{10, &ISystem::Flush, "Flush"},
        FunctionInfoTyped<ISystem>{11, &ISystem::Restore, "Restore"},
        FunctionInfoTyped<ISystem>{12, &ISystem::CreateApplicationArea, "CreateApplicationArea"},
        FunctionInfoTyped<ISystem>{13, &ISystem::GetTagInfo, "GetTagInfo"},
        FunctionInfoTyped<ISystem>{14, &ISystem::GetRegisterInfo, "GetRegisterInfo"},
        FunctionInfoTyped<ISystem>{15, &ISystem::GetCommonInfo, "GetCommonInfo"},
        FunctionInfoTyped<ISystem>{16, &ISystem::GetModelInfo, "GetModelInfo"},
        FunctionInfoTyped<ISystem>{17, &ISystem::AttachActivateEvent, "AttachActivateEvent"},
        FunctionInfoTyped<ISystem>{18, &ISystem::AttachDeactivateEvent, "AttachDeactivateEvent"},
        FunctionInfoTyped<ISystem>{19, &ISystem::GetState, "GetState"},
        FunctionInfoTyped<ISystem>{20, &ISystem::GetDeviceState, "GetDeviceState"},
        FunctionInfoTyped<ISystem>{21, &ISystem::GetNpadId, "GetNpadId"},
        FunctionInfoTyped<ISystem>{23, &ISystem::AttachAvailabilityChangeEvent, "AttachAvailabilityChangeEvent"},
        FunctionInfoTyped<ISystem>{25, &ISystem::StartDetection, "StartDetectionWithFilter"},
        FunctionInfoTyped<ISystem>{100, &ISystem::Format, "Format"},
        FunctionInfoTyped<ISystem>{101, &ISystem::GetAdminInfo, "GetAdminInfo"},
        FunctionInfoTyped<ISystem>{102, &ISystem::GetRegisterInfoPrivate, "GetRegisterInfoPrivate"},
        FunctionInfoTyped<ISystem>{103, &ISystem::SetRegisterInfoPrivate, "SetRegisterInfoPrivate"},
        FunctionInfoTyped<ISystem>{104, &ISystem::DeleteRegisterInfo, "DeleteRegisterInfo"},
        FunctionInfoTyped<ISystem>{105, &ISystem::DeleteApplicationArea, "DeleteApplicationArea"},
        FunctionInfoTyped<ISystem>{106, &ISystem::ExistsApplicationArea, "ExistsApplicationArea"}
    );
};

class IDebug final : public Interface {
public:
    explicit IDebug(Core::System& system_) : Interface(system_, "NFP:IDebug") {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMapWithClass<IDebug>(
        FunctionInfoTyped<IDebug>{0, &IDebug::InitializeDebug, "InitializeDebug"},
        FunctionInfoTyped<IDebug>{1, &IDebug::FinalizeDebug, "FinalizeDebug"},
        FunctionInfoTyped<IDebug>{2, &IDebug::ListDevices, "ListDevices"},
        FunctionInfoTyped<IDebug>{3, &IDebug::StartDetection, "StartDetection"},
        FunctionInfoTyped<IDebug>{4, &IDebug::StopDetection, "StopDetection"},
        FunctionInfoTyped<IDebug>{5, &IDebug::Mount, "Mount"},
        FunctionInfoTyped<IDebug>{6, &IDebug::Unmount, "Unmount"},
        FunctionInfoTyped<IDebug>{7, &IDebug::OpenApplicationArea, "OpenApplicationArea"},
        FunctionInfoTyped<IDebug>{8, &IDebug::GetApplicationArea, "GetApplicationArea"},
        FunctionInfoTyped<IDebug>{9, &IDebug::SetApplicationArea, "SetApplicationArea"},
        FunctionInfoTyped<IDebug>{10, &IDebug::Flush, "Flush"},
        FunctionInfoTyped<IDebug>{11, &IDebug::Restore, "Restore"},
        FunctionInfoTyped<IDebug>{12, &IDebug::CreateApplicationArea, "CreateApplicationArea"},
        FunctionInfoTyped<IDebug>{13, &IDebug::GetTagInfo, "GetTagInfo"},
        FunctionInfoTyped<IDebug>{14, &IDebug::GetRegisterInfo, "GetRegisterInfo"},
        FunctionInfoTyped<IDebug>{15, &IDebug::GetCommonInfo, "GetCommonInfo"},
        FunctionInfoTyped<IDebug>{16, &IDebug::GetModelInfo, "GetModelInfo"},
        FunctionInfoTyped<IDebug>{17, &IDebug::AttachActivateEvent, "AttachActivateEvent"},
        FunctionInfoTyped<IDebug>{18, &IDebug::AttachDeactivateEvent, "AttachDeactivateEvent"},
        FunctionInfoTyped<IDebug>{19, &IDebug::GetState, "GetState"},
        FunctionInfoTyped<IDebug>{20, &IDebug::GetDeviceState, "GetDeviceState"},
        FunctionInfoTyped<IDebug>{21, &IDebug::GetNpadId, "GetNpadId"},
        FunctionInfoTyped<IDebug>{22, &IDebug::GetApplicationAreaSize, "GetApplicationAreaSize"},
        FunctionInfoTyped<IDebug>{23, &IDebug::AttachAvailabilityChangeEvent, "AttachAvailabilityChangeEvent"},
        FunctionInfoTyped<IDebug>{24, &IDebug::RecreateApplicationArea, "RecreateApplicationArea"},
        FunctionInfoTyped<IDebug>{25, &IDebug::StartDetection, "StartDetectionWithFilter"},
        FunctionInfoTyped<IDebug>{100, &IDebug::Format, "Format"},
        FunctionInfoTyped<IDebug>{101, &IDebug::GetAdminInfo, "GetAdminInfo"},
        FunctionInfoTyped<IDebug>{102, &IDebug::GetRegisterInfoPrivate, "GetRegisterInfoPrivate"},
        FunctionInfoTyped<IDebug>{103, &IDebug::SetRegisterInfoPrivate, "SetRegisterInfoPrivate"},
        FunctionInfoTyped<IDebug>{104, &IDebug::DeleteRegisterInfo, "DeleteRegisterInfo"},
        FunctionInfoTyped<IDebug>{105, &IDebug::DeleteApplicationArea, "DeleteApplicationArea"},
        FunctionInfoTyped<IDebug>{106, &IDebug::ExistsApplicationArea, "ExistsApplicationArea"},
        FunctionInfoTyped<IDebug>{200, &IDebug::GetAll, "GetAll"},
        FunctionInfoTyped<IDebug>{201, &IDebug::SetAll, "SetAll"},
        FunctionInfoTyped<IDebug>{202, &IDebug::FlushDebug, "FlushDebug"},
        FunctionInfoTyped<IDebug>{203, &IDebug::BreakTag, "BreakTag"},
        FunctionInfoTyped<IDebug>{204, &IDebug::ReadBackupData, "ReadBackupData"},
        FunctionInfoTyped<IDebug>{205, &IDebug::WriteBackupData, "WriteBackupData"},
        FunctionInfoTyped<IDebug>{206, &IDebug::WriteNtf, "WriteNtf"}
    );
};

class IUserManager final : public ServiceFramework<IUserManager> {
public:
    explicit IUserManager(Core::System& system_) : ServiceFramework{system_, "nfp:user"} {}

private:
    void CreateUserInterface(HLERequestContext& ctx) {
        LOG_DEBUG(Service_NFP, "called");

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface<IUser>(ctx, system);
    }

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, &IUserManager::CreateUserInterface, "CreateUserInterface"}
    );
};

class ISystemManager final : public ServiceFramework<ISystemManager> {
public:
    explicit ISystemManager(Core::System& system_) : ServiceFramework{system_, "nfp:sys"} {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }

private:
    void CreateSystemInterface(HLERequestContext& ctx) {
        LOG_DEBUG(Service_NFP, "called");

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface<ISystem>(ctx, system);
    }

    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, &ISystemManager::CreateSystemInterface, "CreateSystemInterface"}
    );
};

class IDebugManager final : public ServiceFramework<IDebugManager> {
public:
    explicit IDebugManager(Core::System& system_) : ServiceFramework{system_, "nfp:dbg"} {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }

private:
    void CreateDebugInterface(HLERequestContext& ctx) {
        LOG_DEBUG(Service_NFP, "called");

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface<IDebug>(ctx, system);
    }

    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, &IDebugManager::CreateDebugInterface, "CreateDebugInterface"}
    );
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("nfp:user", std::make_shared<IUserManager>(system));
    server_manager->RegisterNamedService("nfp:sys", std::make_shared<ISystemManager>(system));
    server_manager->RegisterNamedService("nfp:dbg", std::make_shared<IDebugManager>(system));
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::NFP
