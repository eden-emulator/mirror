// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>

#include "common/logging.h"
#include "common/settings.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/nfc/nfc.h"
#include "core/hle/service/nfc/nfc_interface.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"

namespace Service::NFC {

class IUser final : public NfcInterface {
public:
    explicit IUser(Core::System& system_) : NfcInterface(system_, "NFC::IUser", BackendType::Nfc) {
        static const FunctionInfoTyped<IUser> functions[] = {
            FunctionInfoTyped<IUser>{0, &NfcInterface::Initialize, "InitializeOld"},
            FunctionInfoTyped<IUser>{1, &NfcInterface::Finalize, "FinalizeOld"},
            FunctionInfoTyped<IUser>{2, &NfcInterface::GetState, "GetStateOld"},
            FunctionInfoTyped<IUser>{3, &NfcInterface::IsNfcEnabled, "IsNfcEnabledOld"},
            FunctionInfoTyped<IUser>{400, &NfcInterface::Initialize, "Initialize"},
            FunctionInfoTyped<IUser>{401, &NfcInterface::Finalize, "Finalize"},
            FunctionInfoTyped<IUser>{402, &NfcInterface::GetState, "GetState"},
            FunctionInfoTyped<IUser>{403, &NfcInterface::IsNfcEnabled, "IsNfcEnabled"},
            FunctionInfoTyped<IUser>{404, &NfcInterface::ListDevices, "ListDevices"},
            FunctionInfoTyped<IUser>{405, &NfcInterface::GetDeviceState, "GetDeviceState"},
            FunctionInfoTyped<IUser>{406, &NfcInterface::GetNpadId, "GetNpadId"},
            FunctionInfoTyped<IUser>{407, &NfcInterface::AttachAvailabilityChangeEvent, "AttachAvailabilityChangeEvent"},
            FunctionInfoTyped<IUser>{408, &NfcInterface::StartDetection, "StartDetection"},
            FunctionInfoTyped<IUser>{409, &NfcInterface::StopDetection, "StopDetection"},
            FunctionInfoTyped<IUser>{410, &NfcInterface::GetTagInfo, "GetTagInfo"},
            FunctionInfoTyped<IUser>{411, &NfcInterface::AttachActivateEvent, "AttachActivateEvent"},
            FunctionInfoTyped<IUser>{412, &NfcInterface::AttachDeactivateEvent, "AttachDeactivateEvent"},
            FunctionInfoTyped<IUser>{1000, &NfcInterface::ReadMifare, "ReadMifare"},
            FunctionInfoTyped<IUser>{1001, &NfcInterface::WriteMifare ,"WriteMifare"},
            FunctionInfoTyped<IUser>{1300, &NfcInterface::SendCommandByPassThrough, "SendCommandByPassThrough"},
            FunctionInfoTyped<IUser>{1301, nullptr, "KeepPassThroughSession"},
            FunctionInfoTyped<IUser>{1302, nullptr, "ReleasePassThroughSession"}
        };
        RegisterHandlers(functions);
    }
};

class ISystem final : public NfcInterface {
public:
    explicit ISystem(Core::System& system_)
        : NfcInterface{system_, "NFC::ISystem", BackendType::Nfc} {
        static const FunctionInfoTyped<ISystem> functions[] = {
            FunctionInfoTyped<ISystem>{0, &NfcInterface::Initialize, "InitializeOld"},
            FunctionInfoTyped<ISystem>{1, &NfcInterface::Finalize, "FinalizeOld"},
            FunctionInfoTyped<ISystem>{2, &NfcInterface::GetState, "GetStateOld"},
            FunctionInfoTyped<ISystem>{3, &NfcInterface::IsNfcEnabled, "IsNfcEnabledOld"},
            FunctionInfoTyped<ISystem>{100, &NfcInterface::SetNfcEnabled, "SetNfcEnabledOld"},
            FunctionInfoTyped<ISystem>{400, &NfcInterface::Initialize, "Initialize"},
            FunctionInfoTyped<ISystem>{401, &NfcInterface::Finalize, "Finalize"},
            FunctionInfoTyped<ISystem>{402, &NfcInterface::GetState, "GetState"},
            FunctionInfoTyped<ISystem>{403, &NfcInterface::IsNfcEnabled, "IsNfcEnabled"},
            FunctionInfoTyped<ISystem>{404, &NfcInterface::ListDevices, "ListDevices"},
            FunctionInfoTyped<ISystem>{405, &NfcInterface::GetDeviceState, "GetDeviceState"},
            FunctionInfoTyped<ISystem>{406, &NfcInterface::GetNpadId, "GetNpadId"},
            FunctionInfoTyped<ISystem>{407, &NfcInterface::AttachAvailabilityChangeEvent, "AttachAvailabilityChangeEvent"},
            FunctionInfoTyped<ISystem>{408, &NfcInterface::StartDetection, "StartDetection"},
            FunctionInfoTyped<ISystem>{409, &NfcInterface::StopDetection, "StopDetection"},
            FunctionInfoTyped<ISystem>{410, &NfcInterface::GetTagInfo, "GetTagInfo"},
            FunctionInfoTyped<ISystem>{411, &NfcInterface::AttachActivateEvent, "AttachActivateEvent"},
            FunctionInfoTyped<ISystem>{412, &NfcInterface::AttachDeactivateEvent, "AttachDeactivateEvent"},
            FunctionInfoTyped<ISystem>{500, &NfcInterface::SetNfcEnabled, "SetNfcEnabled"},
            FunctionInfoTyped<ISystem>{510, nullptr, "OutputTestWave"},
            FunctionInfoTyped<ISystem>{1000, &NfcInterface::ReadMifare, "ReadMifare"},
            FunctionInfoTyped<ISystem>{1001, &NfcInterface::WriteMifare, "WriteMifare"},
            FunctionInfoTyped<ISystem>{1300, &NfcInterface::SendCommandByPassThrough, "SendCommandByPassThrough"},
            FunctionInfoTyped<ISystem>{1301, nullptr, "KeepPassThroughSession"},
            FunctionInfoTyped<ISystem>{1302, nullptr, "ReleasePassThroughSession"}
        };
        RegisterHandlers(functions);
    }
};

// MFInterface has an unique interface but it's identical to NfcInterface so we can keep the code
// simpler
using MFInterface = NfcInterface;
class MFIUser final : public MFInterface {
public:
    explicit MFIUser(Core::System& system_) : MFInterface{system_, "NFC::MFInterface", BackendType::Mifare} {
        static const FunctionInfoTyped<MFIUser> functions[] = {
            FunctionInfoTyped<MFIUser>{0, &MFIUser::Initialize, "Initialize"},
            FunctionInfoTyped<MFIUser>{1, &MFIUser::Finalize, "Finalize"},
            FunctionInfoTyped<MFIUser>{2, &MFIUser::ListDevices, "ListDevices"},
            FunctionInfoTyped<MFIUser>{3, &MFIUser::StartDetection, "StartDetection"},
            FunctionInfoTyped<MFIUser>{4, &MFIUser::StopDetection, "StopDetection"},
            FunctionInfoTyped<MFIUser>{5, &MFIUser::ReadMifare, "Read"},
            FunctionInfoTyped<MFIUser>{6, &MFIUser::WriteMifare, "Write"},
            FunctionInfoTyped<MFIUser>{7, &MFIUser::GetTagInfo, "GetTagInfo"},
            FunctionInfoTyped<MFIUser>{8, &MFIUser::AttachActivateEvent, "GetActivateEventHandle"},
            FunctionInfoTyped<MFIUser>{9, &MFIUser::AttachDeactivateEvent, "GetDeactivateEventHandle"},
            FunctionInfoTyped<MFIUser>{10, &MFIUser::GetState, "GetState"},
            FunctionInfoTyped<MFIUser>{11, &MFIUser::GetDeviceState, "GetDeviceState"},
            FunctionInfoTyped<MFIUser>{12, &MFIUser::GetNpadId, "GetNpadId"},
            FunctionInfoTyped<MFIUser>{13, &MFIUser::AttachAvailabilityChangeEvent, "GetAvailabilityChangeEventHandle"}
        };
        RegisterHandlers(functions);
    }
};

class IAm final : public ServiceFramework<IAm> {
public:
    explicit IAm(Core::System& system_) : ServiceFramework{system_, "NFC::IAm"} {}

    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, nullptr, "Initialize"},
        FunctionInfo{1, nullptr, "Finalize"},
        FunctionInfo{2, nullptr, "NotifyForegroundApplet"}
    );
    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
};

class NFC_AM final : public ServiceFramework<NFC_AM> {
public:
    explicit NFC_AM(Core::System& system_) : ServiceFramework{system_, "nfc:am"} {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }

private:
    void CreateAmNfcInterface(HLERequestContext& ctx) {
        LOG_DEBUG(Service_NFC, "called");

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface<IAm>(ctx, system);
    }

    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, &NFC_AM::CreateAmNfcInterface, "CreateAmNfcInterface"}
    );
};

class NFC_MF_U final : public ServiceFramework<NFC_MF_U> {
public:
    explicit NFC_MF_U(Core::System& system_) : ServiceFramework{system_, "nfc:mf:u"} {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }

private:
    void CreateUserNfcInterface(HLERequestContext& ctx) {
        LOG_DEBUG(Service_NFC, "called");

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface<MFIUser>(ctx, system);
    }

    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, &NFC_MF_U::CreateUserNfcInterface, "CreateUserNfcInterface"}
    );
};

class NFC_U final : public ServiceFramework<NFC_U> {
public:
    explicit NFC_U(Core::System& system_) : ServiceFramework{system_, "nfc:user"} {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }

private:
    void CreateUserNfcInterface(HLERequestContext& ctx) {
        LOG_DEBUG(Service_NFC, "called");

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface<IUser>(ctx, system);
    }

    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, &NFC_U::CreateUserNfcInterface, "CreateUserNfcInterface"}
    );
};

class NFC_SYS final : public ServiceFramework<NFC_SYS> {
public:
    explicit NFC_SYS(Core::System& system_) : ServiceFramework{system_, "nfc:sys"} {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }

private:
    void CreateSystemNfcInterface(HLERequestContext& ctx) {
        LOG_DEBUG(Service_NFC, "called");

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface<ISystem>(ctx, system);
    }

    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, &NFC_SYS::CreateSystemNfcInterface, "CreateSystemNfcInterface"}
    );
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("nfc:am", std::make_shared<NFC_AM>(system));
    server_manager->RegisterNamedService("nfc:mf:u", std::make_shared<NFC_MF_U>(system));
    server_manager->RegisterNamedService("nfc:user", std::make_shared<NFC_U>(system));
    server_manager->RegisterNamedService("nfc:sys", std::make_shared<NFC_SYS>(system));

    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::NFC
