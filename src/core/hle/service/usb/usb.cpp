// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>

#include "common/logging.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"
#include "core/hle/service/usb/usb.h"
#include "frontend_common/firmware_manager.h"

namespace Service::USB {

class IDsInterface final : public ServiceFramework<IDsInterface> {
public:
    explicit IDsInterface(Core::System& system_) : ServiceFramework{system_, "IDsInterface"} {}

    static const auto functions = CreateStaticMap(
            FunctionInfo{0, nullptr, "AddEndpoint"},
            FunctionInfo{1, nullptr, "GetSetupEvent"},
            FunctionInfo{2, nullptr, "GetSetupPacket"},
            FunctionInfo{3, nullptr, "Enable"},
            FunctionInfo{4, nullptr, "Disable"},
            FunctionInfo{5, nullptr, "CtrlIn"},
            FunctionInfo{6, nullptr, "CtrlOut"},
            FunctionInfo{7, nullptr, "GetCtrlInCompletionEvent"},
            FunctionInfo{8, nullptr, "GetCtrlInUrbReport"},
            FunctionInfo{9, nullptr, "GetCtrlOutCompletionEvent"},
            FunctionInfo{10, nullptr, "GetCtrlOutUrbReport"},
            FunctionInfo{11, nullptr, "CtrlStall"},
            FunctionInfo{12, nullptr, "AppendConfigurationData"}
        );
    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
};

class IDsRootSession final : public ServiceFramework<IDsRootSession> {
public:
    explicit IDsRootSession(Core::System& system_) : ServiceFramework{system_, "usb:ds"} {}

    static const auto functions = CreateStaticMap(
            FunctionInfo{0, nullptr, "OpenDsService"}
        );
    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
};

class IClientEpSession final : public ServiceFramework<IClientEpSession> {
public:
    explicit IClientEpSession(Core::System& system_)
        : ServiceFramework{system_, "IClientEpSession"} {}

    static const auto functions = CreateStaticMap(
            FunctionInfo{0, nullptr, "ReOpen"},
            FunctionInfo{1, nullptr, "Close"},
            FunctionInfo{2, nullptr, "GetCompletionEvent"},
            FunctionInfo{3, nullptr, "PopulateRing"},
            FunctionInfo{4, nullptr, "PostBufferAsync"},
            FunctionInfo{5, nullptr, "GetXferReport"},
            FunctionInfo{6, nullptr, "PostBufferMultiAsync"},
            FunctionInfo{7, nullptr, "CreateSmmuSpace"},
            FunctionInfo{8, nullptr, "ShareReportRing"}
        );
    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
};

class IClientIfSession final : public ServiceFramework<IClientIfSession> {
public:
    explicit IClientIfSession(Core::System& system_)
        : ServiceFramework{system_, "IClientIfSession"} {}

    static const auto functions = CreateStaticMap(
            FunctionInfo{0, nullptr, "GetStateChangeEvent"},
            FunctionInfo{1, nullptr, "SetInterface"},
            FunctionInfo{2, nullptr, "GetInterface"},
            FunctionInfo{3, nullptr, "GetAlternateInterface"},
            FunctionInfo{4, nullptr, "GetCurrentFrame"},
            FunctionInfo{5, nullptr, "CtrlXferAsync"},
            FunctionInfo{6, nullptr, "GetCtrlXferCompletionEvent"},
            FunctionInfo{7, nullptr, "GetCtrlXferReport"},
            FunctionInfo{8, nullptr, "ResetDevice"},
            FunctionInfo{9, nullptr, "OpenUsbEp"}
        );
    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
};

class IClientRootSession final : public ServiceFramework<IClientRootSession> {
public:
    explicit IClientRootSession(Core::System& system_) : ServiceFramework{system_, "usb:hs"} {}

    static const auto functions = CreateStaticMap(
            FunctionInfo{0, nullptr, "BindClientProcess"},
            FunctionInfo{1, nullptr, "QueryAllInterfaces"},
            FunctionInfo{2, nullptr, "QueryAvailableInterfaces"},
            FunctionInfo{3, nullptr, "QueryAcquiredInterfaces"},
            FunctionInfo{4, nullptr, "CreateInterfaceAvailableEvent"},
            FunctionInfo{5, nullptr, "DestroyInterfaceAvailableEvent"},
            FunctionInfo{6, nullptr, "GetInterfaceStateChangeEvent"},
            FunctionInfo{7, nullptr, "AcquireUsbIf"},
            FunctionInfo{8, nullptr, "SetTestMode"}
        );
    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
};

class IPdSession final : public ServiceFramework<IPdSession> {
public:
    explicit IPdSession(Core::System& system_) : ServiceFramework{system_, "IPdSession"} {}

    static const auto functions = CreateStaticMap(
            FunctionInfo{0, nullptr, "BindNoticeEvent"},
            FunctionInfo{1, nullptr, "UnbindNoticeEvent"},
            FunctionInfo{2, nullptr, "GetStatus"},
            FunctionInfo{3, nullptr, "GetNotice"},
            FunctionInfo{4, nullptr, "EnablePowerRequestNotice"},
            FunctionInfo{5, nullptr, "DisablePowerRequestNotice"},
            FunctionInfo{6, nullptr, "ReplyPowerRequest"}
        );
    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
};

class IPdManager final : public ServiceFramework<IPdManager> {
public:
    explicit IPdManager(Core::System& system_) : ServiceFramework{system_, "usb:pd"} {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }

private:
    void OpenSession(HLERequestContext& ctx) {
        LOG_DEBUG(Service_USB, "called");

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface<IPdSession>(ctx, system);
    }

    static const auto functions = CreateStaticMap(
        FunctionInfo{0, &IPdManager::OpenSession, "OpenSession"}
    );
};

class IPdCradleSession final : public ServiceFramework<IPdCradleSession> {
public:
    explicit IPdCradleSession(Core::System& system_)
        : ServiceFramework{system_, "IPdCradleSession"} {}

    static const auto functions = CreateStaticMap(
            FunctionInfo{0, nullptr, "SetCradleVdo"},
            FunctionInfo{1, nullptr, "GetCradleVdo"},
            FunctionInfo{2, nullptr, "ResetCradleUsbHub"},
            FunctionInfo{3, nullptr, "GetHostPdcFirmwareType"},
            FunctionInfo{4, nullptr, "GetHostPdcFirmwareRevision"},
            FunctionInfo{5, nullptr, "GetHostPdcManufactureId"},
            FunctionInfo{6, nullptr, "GetHostPdcDeviceId"},
            FunctionInfo{7, nullptr, "EnableCradleRecovery"},
            FunctionInfo{8, nullptr, "DisableCradleRecovery"}
        );
    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
};

class IPdCradleManager final : public ServiceFramework<IPdCradleManager> {
public:
    explicit IPdCradleManager(Core::System& system_) : ServiceFramework{system_, "usb:pd:c"} {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }

private:
    void OpenCradleSession(HLERequestContext& ctx) {
        LOG_DEBUG(Service_USB, "called");

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface<IPdCradleSession>(ctx, system);
    }

    static const auto functions = CreateStaticMap(
        FunctionInfo{0, &IPdCradleManager::OpenCradleSession, "OpenCradleSession"}
    );
};

class IPmMainService final : public ServiceFramework<IPmMainService> {
public:
    explicit IPmMainService(Core::System& system_) : ServiceFramework{system_, "usb:pm"} {}

    static const auto functions = CreateStaticMap(
            FunctionInfo{0, nullptr, "GetPowerEvent"},
            FunctionInfo{1, nullptr, "GetPowerState"},
            FunctionInfo{2, nullptr, "GetDataEvent"},
            FunctionInfo{3, nullptr, "GetDataRole"},
            FunctionInfo{4, nullptr, "SetDiagData"},
            FunctionInfo{5, nullptr, "GetDiagData"}
        );
    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
};

class IPdManufactureManager final : public ServiceFramework<IPdManufactureManager> {
public:
    explicit IPdManufactureManager(Core::System& system_) : ServiceFramework{system_, "usb:pd:m"} {}

    static const auto functions = CreateStaticMap(
            FunctionInfo{0, nullptr, "OpenManufactureSession"}
        );
    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
};

class IQdbManager final : public ServiceFramework<IQdbManager> {
public:
    explicit IQdbManager(Core::System& system_) : ServiceFramework{system_, "usb:qdb"} {}

    static const auto functions = CreateStaticMap(
            FunctionInfo{0, nullptr, "ImportQuirkDevices"},
            FunctionInfo{1, nullptr, "HasQuirk"}
        );
    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
};

class IPmObserverService final : public ServiceFramework<IPmObserverService> {
public:
    explicit IPmObserverService(Core::System& system_) : ServiceFramework{system_, "usb:obsv"} {}

    static const auto functions = CreateStaticMap(
            FunctionInfo{0, nullptr, "GetTopologyChangeEvent"},
            FunctionInfo{1, nullptr, "GetFlattenedTopology"}
        );
    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("usb:ds", std::make_shared<IDsRootSession>(system));
    server_manager->RegisterNamedService("usb:hs", std::make_shared<IClientRootSession>(system));
    server_manager->RegisterNamedService("usb:pd", std::make_shared<IPdManager>(system), 6);
    server_manager->RegisterNamedService("usb:pd:c", std::make_shared<IPdCradleManager>(system), 4);
    server_manager->RegisterNamedService("usb:pd:m", std::make_shared<IPdManufactureManager>(system));
    server_manager->RegisterNamedService("usb:pm", std::make_shared<IPmMainService>(system), 5);
    // +7.0.0
    if (FirmwareManager::GetFirmwareVersion(system).first.major >= 7) {
        server_manager->RegisterNamedService("usb:qdb", std::make_shared<IQdbManager>(system));
    }
    // +8.0.0
    if (FirmwareManager::GetFirmwareVersion(system).first.major >= 8) {
        server_manager->RegisterNamedService("usb:obsv", std::make_shared<IPmObserverService>(system), 2);
    }
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::USB
