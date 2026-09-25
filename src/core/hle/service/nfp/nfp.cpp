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
    explicit IUser(Core::System& system_) : Interface(system_, "NFP:IUser") {
        // clang-format off
        static const FunctionInfoTyped<IUser> functions[] = {
            FunctionInfo{0, &IUser::Initialize, "Initialize"},
            FunctionInfo{1, &IUser::Finalize, "Finalize"},
            FunctionInfo{2, &IUser::ListDevices, "ListDevices"},
            FunctionInfo{3, &IUser::StartDetection, "StartDetection"},
            FunctionInfo{4, &IUser::StopDetection, "StopDetection"},
            FunctionInfo{5, &IUser::Mount, "Mount"},
            FunctionInfo{6, &IUser::Unmount, "Unmount"},
            FunctionInfo{7, &IUser::OpenApplicationArea, "OpenApplicationArea"},
            FunctionInfo{8, &IUser::GetApplicationArea, "GetApplicationArea"},
            FunctionInfo{9, &IUser::SetApplicationArea, "SetApplicationArea"},
            FunctionInfo{10, &IUser::Flush, "Flush"},
            FunctionInfo{11, &IUser::Restore, "Restore"},
            FunctionInfo{12, &IUser::CreateApplicationArea, "CreateApplicationArea"},
            FunctionInfo{13, &IUser::GetTagInfo, "GetTagInfo"},
            FunctionInfo{14, &IUser::GetRegisterInfo, "GetRegisterInfo"},
            FunctionInfo{15, &IUser::GetCommonInfo, "GetCommonInfo"},
            FunctionInfo{16, &IUser::GetModelInfo, "GetModelInfo"},
            FunctionInfo{17, &IUser::AttachActivateEvent, "AttachActivateEvent"},
            FunctionInfo{18, &IUser::AttachDeactivateEvent, "AttachDeactivateEvent"},
            FunctionInfo{19, &IUser::GetState, "GetState"},
            FunctionInfo{20, &IUser::GetDeviceState, "GetDeviceState"},
            FunctionInfo{21, &IUser::GetNpadId, "GetNpadId"},
            FunctionInfo{22, &IUser::GetApplicationAreaSize, "GetApplicationAreaSize"},
            FunctionInfo{23, &IUser::AttachAvailabilityChangeEvent, "AttachAvailabilityChangeEvent"},
            FunctionInfo{24, &IUser::RecreateApplicationArea, "RecreateApplicationArea"},
            FunctionInfo{25, &IUser::StartDetection, "StartDetectionWithFilter"}
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

class ISystem final : public Interface {
public:
    explicit ISystem(Core::System& system_) : Interface(system_, "NFP:ISystem") {
        // clang-format off
        static const FunctionInfoTyped<ISystem> functions[] = {
            FunctionInfo{0, &ISystem::InitializeSystem, "InitializeSystem"},
            FunctionInfo{1, &ISystem::FinalizeSystem, "FinalizeSystem"},
            FunctionInfo{2, &ISystem::ListDevices, "ListDevices"},
            FunctionInfo{3, &ISystem::StartDetection, "StartDetection"},
            FunctionInfo{4, &ISystem::StopDetection, "StopDetection"},
            FunctionInfo{5, &ISystem::Mount, "Mount"},
            FunctionInfo{6, &ISystem::Unmount, "Unmount"},
            FunctionInfo{10, &ISystem::Flush, "Flush"},
            FunctionInfo{11, &ISystem::Restore, "Restore"},
            FunctionInfo{12, &ISystem::CreateApplicationArea, "CreateApplicationArea"},
            FunctionInfo{13, &ISystem::GetTagInfo, "GetTagInfo"},
            FunctionInfo{14, &ISystem::GetRegisterInfo, "GetRegisterInfo"},
            FunctionInfo{15, &ISystem::GetCommonInfo, "GetCommonInfo"},
            FunctionInfo{16, &ISystem::GetModelInfo, "GetModelInfo"},
            FunctionInfo{17, &ISystem::AttachActivateEvent, "AttachActivateEvent"},
            FunctionInfo{18, &ISystem::AttachDeactivateEvent, "AttachDeactivateEvent"},
            FunctionInfo{19, &ISystem::GetState, "GetState"},
            FunctionInfo{20, &ISystem::GetDeviceState, "GetDeviceState"},
            FunctionInfo{21, &ISystem::GetNpadId, "GetNpadId"},
            FunctionInfo{23, &ISystem::AttachAvailabilityChangeEvent, "AttachAvailabilityChangeEvent"},
            FunctionInfo{25, &ISystem::StartDetection, "StartDetectionWithFilter"},
            FunctionInfo{100, &ISystem::Format, "Format"},
            FunctionInfo{101, &ISystem::GetAdminInfo, "GetAdminInfo"},
            FunctionInfo{102, &ISystem::GetRegisterInfoPrivate, "GetRegisterInfoPrivate"},
            FunctionInfo{103, &ISystem::SetRegisterInfoPrivate, "SetRegisterInfoPrivate"},
            FunctionInfo{104, &ISystem::DeleteRegisterInfo, "DeleteRegisterInfo"},
            FunctionInfo{105, &ISystem::DeleteApplicationArea, "DeleteApplicationArea"},
            FunctionInfo{106, &ISystem::ExistsApplicationArea, "ExistsApplicationArea"}
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

class IDebug final : public Interface {
public:
    explicit IDebug(Core::System& system_) : Interface(system_, "NFP:IDebug") {
        // clang-format off
        static const FunctionInfoTyped<IDebug> functions[] = {
            FunctionInfo{0, &IDebug::InitializeDebug, "InitializeDebug"},
            FunctionInfo{1, &IDebug::FinalizeDebug, "FinalizeDebug"},
            FunctionInfo{2, &IDebug::ListDevices, "ListDevices"},
            FunctionInfo{3, &IDebug::StartDetection, "StartDetection"},
            FunctionInfo{4, &IDebug::StopDetection, "StopDetection"},
            FunctionInfo{5, &IDebug::Mount, "Mount"},
            FunctionInfo{6, &IDebug::Unmount, "Unmount"},
            FunctionInfo{7, &IDebug::OpenApplicationArea, "OpenApplicationArea"},
            FunctionInfo{8, &IDebug::GetApplicationArea, "GetApplicationArea"},
            FunctionInfo{9, &IDebug::SetApplicationArea, "SetApplicationArea"},
            FunctionInfo{10, &IDebug::Flush, "Flush"},
            FunctionInfo{11, &IDebug::Restore, "Restore"},
            FunctionInfo{12, &IDebug::CreateApplicationArea, "CreateApplicationArea"},
            FunctionInfo{13, &IDebug::GetTagInfo, "GetTagInfo"},
            FunctionInfo{14, &IDebug::GetRegisterInfo, "GetRegisterInfo"},
            FunctionInfo{15, &IDebug::GetCommonInfo, "GetCommonInfo"},
            FunctionInfo{16, &IDebug::GetModelInfo, "GetModelInfo"},
            FunctionInfo{17, &IDebug::AttachActivateEvent, "AttachActivateEvent"},
            FunctionInfo{18, &IDebug::AttachDeactivateEvent, "AttachDeactivateEvent"},
            FunctionInfo{19, &IDebug::GetState, "GetState"},
            FunctionInfo{20, &IDebug::GetDeviceState, "GetDeviceState"},
            FunctionInfo{21, &IDebug::GetNpadId, "GetNpadId"},
            FunctionInfo{22, &IDebug::GetApplicationAreaSize, "GetApplicationAreaSize"},
            FunctionInfo{23, &IDebug::AttachAvailabilityChangeEvent, "AttachAvailabilityChangeEvent"},
            FunctionInfo{24, &IDebug::RecreateApplicationArea, "RecreateApplicationArea"},
            FunctionInfo{25, &IDebug::StartDetection, "StartDetectionWithFilter"},
            FunctionInfo{100, &IDebug::Format, "Format"},
            FunctionInfo{101, &IDebug::GetAdminInfo, "GetAdminInfo"},
            FunctionInfo{102, &IDebug::GetRegisterInfoPrivate, "GetRegisterInfoPrivate"},
            FunctionInfo{103, &IDebug::SetRegisterInfoPrivate, "SetRegisterInfoPrivate"},
            FunctionInfo{104, &IDebug::DeleteRegisterInfo, "DeleteRegisterInfo"},
            FunctionInfo{105, &IDebug::DeleteApplicationArea, "DeleteApplicationArea"},
            FunctionInfo{106, &IDebug::ExistsApplicationArea, "ExistsApplicationArea"},
            FunctionInfo{200, &IDebug::GetAll, "GetAll"},
            FunctionInfo{201, &IDebug::SetAll, "SetAll"},
            FunctionInfo{202, &IDebug::FlushDebug, "FlushDebug"},
            FunctionInfo{203, &IDebug::BreakTag, "BreakTag"},
            FunctionInfo{204, &IDebug::ReadBackupData, "ReadBackupData"},
            FunctionInfo{205, &IDebug::WriteBackupData, "WriteBackupData"},
            FunctionInfo{206, &IDebug::WriteNtf, "WriteNtf"}
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

class IUserManager final : public ServiceFramework<IUserManager> {
public:
    explicit IUserManager(Core::System& system_) : ServiceFramework{system_, "nfp:user"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            FunctionInfo{0, &IUserManager::CreateUserInterface, "CreateUserInterface"}
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void CreateUserInterface(HLERequestContext& ctx) {
        LOG_DEBUG(Service_NFP, "called");

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface<IUser>(ctx, system);
    }
};

class ISystemManager final : public ServiceFramework<ISystemManager> {
public:
    explicit ISystemManager(Core::System& system_) : ServiceFramework{system_, "nfp:sys"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            FunctionInfo{0, &ISystemManager::CreateSystemInterface, "CreateSystemInterface"}
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void CreateSystemInterface(HLERequestContext& ctx) {
        LOG_DEBUG(Service_NFP, "called");

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface<ISystem>(ctx, system);
    }
};

class IDebugManager final : public ServiceFramework<IDebugManager> {
public:
    explicit IDebugManager(Core::System& system_) : ServiceFramework{system_, "nfp:dbg"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            FunctionInfo{0, &IDebugManager::CreateDebugInterface, "CreateDebugInterface"}
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void CreateDebugInterface(HLERequestContext& ctx) {
        LOG_DEBUG(Service_NFP, "called");

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface<IDebug>(ctx, system);
    }
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("nfp:user", std::make_shared<IUserManager>(system));
    server_manager->RegisterNamedService("nfp:sys", std::make_shared<ISystemManager>(system));
    server_manager->RegisterNamedService("nfp:dbg", std::make_shared<IDebugManager>(system));
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::NFP
