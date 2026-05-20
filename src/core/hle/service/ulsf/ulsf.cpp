// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <memory>
#include "core/hle/result.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/ulsf/ulsf.h"
#include "core/hle/service/server_manager.h"

namespace Service::ULSF {

ULSF_U::ULSF_U(Core::System& system_) : ServiceFramework{system_, "ulsf:u"} {
    static const FunctionInfo functions[] = {
        {0, &ULSF_U::GetVersion, "GetVersion"},
    };
    RegisterHandlers(functions);
}
ULSF_U::~ULSF_U() = default;

// Result ULSF_U::GetVersion(Out<ULauncherVersion> out_version) {
//     LOG_WARNING(Service_SM, "(stubbed)");
//     *out_version = {};
//     R_SUCCEED();
// }

void ULSF_U::GetVersion(HLERequestContext& ctx) {
    LOG_WARNING(Service_SM, "(stubbed)");
    ULauncherVersion version{1, 6, 7};
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
    rb.PushRaw(version);
}

enum class MenuMessage : u32 {
    Invalid,
    HomeRequest,
    SdCardEjected,
    GameCardMountFailure,
    PreviousLaunchFailure,
    ChosenHomebrew,
    FinishedSleep,
    ApplicationRecordsChanged,
    ApplicationVerifyProgress,
    ApplicationVerifyResult
};
struct MenuMessageContext {
    MenuMessage msg;
    union {
        struct {
            Result mount_rc;
        } gc_mount_failure;
        struct {
            char nro_path[0x301];
        } chosen_hb;
        struct {
            bool records_added_or_deleted;
        } app_records_changed;
        struct {
            u64 app_id;
            u64 done;
            u64 total;
        } app_verify_progress;
        struct {
            u64 app_id;
            Result rc;
            Result detail_rc;
        } app_verify_rc;
    };
};

class ULSF_P final : public ServiceFramework<ULSF_P> {
public:
    explicit ULSF_P(Core::System& system_) : ServiceFramework{system_, "ulsf:p"} {
        static const FunctionInfo functions[] = {
            {0, C<&ULSF_P::Initialize>, "Initialize"},
            {1, C<&ULSF_P::TryPopMessageContext>, "TryPopMessageContext"},
        };
        RegisterHandlers(functions);
    }
    ~ULSF_P() = default;

    Result Initialize(u64 pid) {
        LOG_WARNING(Service_SM, "(stubbed) pid={}", pid);
        R_SUCCEED();
    }
    Result TryPopMessageContext(OutLargeData<MenuMessageContext, BufferAttr_HipcMapAlias> out_menu_message) {
        //LOG_WARNING(Service_SM, "(stubbed)");
//        *out_menu_message = {};
//        R_SUCCEED();
        R_THROW(Kernel::ResultInvalidAddress);
    }
};

class AVM final : public ServiceFramework<AVM> {
public:
    explicit AVM(Core::System& system_): ServiceFramework{system_, "avm"} {
        static const FunctionInfo functions[] = {
            {0, &AVM::Cmd0, "Cmd0"},
        };
        RegisterHandlers(functions);
    }
    ~AVM() = default;
    void Cmd0(HLERequestContext& ctx) {
        LOG_WARNING(Service_SM, "(stubbed)");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }
};
class WLAN final : public ServiceFramework<WLAN> {
public:
    explicit WLAN(Core::System& system_): ServiceFramework{system_, "wlan"} {
        static const FunctionInfo functions[] = {
            {0, &WLAN::Cmd0, "Cmd0"},
        };
        RegisterHandlers(functions);
    }
    ~WLAN() = default;
    void Cmd0(HLERequestContext& ctx) {
        LOG_WARNING(Service_SM, "(stubbed)");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);
    server_manager->RegisterNamedService("ulsf:u", std::make_shared<ULSF_U>(system));
    server_manager->RegisterNamedService("ulsf:p", std::make_shared<ULSF_P>(system));

    server_manager->RegisterNamedService("avm", std::make_shared<AVM>(system));
    server_manager->RegisterNamedService("wlan", std::make_shared<WLAN>(system));
    ServerManager::RunServer(std::move(server_manager));
}

}
