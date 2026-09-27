// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>

#include "core/hle/kernel/k_event.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/npns/npns.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"

namespace Service::NPNS {

class INpnsSystem final : public ServiceFramework<INpnsSystem> {
public:
    explicit INpnsSystem(Core::System& system_)
        : ServiceFramework{system_, "npns:s"}, service_context{system, "npns:s"},
          get_receive_event{service_context}, get_request_change_state_cancel_event{service_context} {}

    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }

    ~INpnsSystem() override = default;

private:
    Result ListenTo(u32 program_id) {
        LOG_WARNING(Service_NPNS, "(STUBBED) called, program_id={}", program_id);
        R_SUCCEED();
    }

    Result GetReceiveEvent(OutCopyHandle<Kernel::KReadableEvent> out_event) {
        LOG_WARNING(Service_NPNS, "(STUBBED) called");

        *out_event = get_receive_event.GetHandle();
        R_SUCCEED();
    }

    Result ListenToByName() {
        LOG_DEBUG(Service_NPNS, "(STUBBED) called.");

        // TODO (jarrodnorwell)

        R_SUCCEED();
    }

    Result GetState(Out<u32> out_state) {
        LOG_WARNING(Service_NPNS, "(STUBBED) called");
        *out_state = 0;
        R_SUCCEED();
    }

    Result GetLastNotifiedTime(Out<s64> out_last_notified_time) {
        LOG_WARNING(Service_NPNS, "(STUBBED) called");

        *out_last_notified_time = 0;
        R_SUCCEED();
    }

    Result GetRequestChangeStateCancelEvent(OutCopyHandle<Kernel::KReadableEvent> out_event) {
        LOG_DEBUG(Service_NPNS, "(STUBBED) called.");

        // TODO (jarrodnorwell)

        *out_event = get_request_change_state_cancel_event.GetHandle();

        R_SUCCEED();
    }

    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{1, nullptr, "ListenAll"},
        FunctionInfo{2, C<&INpnsSystem::ListenTo>, "ListenTo"},
        FunctionInfo{3, nullptr, "Receive"},
        FunctionInfo{4, nullptr, "ReceiveRaw"},
        FunctionInfo{5, C<&INpnsSystem::GetReceiveEvent>, "GetReceiveEvent"},
        FunctionInfo{6, nullptr, "ListenUndelivered"},
        FunctionInfo{7, nullptr, "GetStateChangeEvent"},
        FunctionInfo{8, C<&INpnsSystem::ListenToByName>, "ListenToByName"},
        FunctionInfo{11, nullptr, "SubscribeTopic"},
        FunctionInfo{12, nullptr, "UnsubscribeTopic"},
        FunctionInfo{13, nullptr, "QueryIsTopicExist"},
        FunctionInfo{14, nullptr, "SubscribeTopicByAccount", MakeVersionGate({18,0,0})},
        FunctionInfo{15, nullptr, "UnsubscribeTopicByAccount", MakeVersionGate({18,0,0})},
        FunctionInfo{16, nullptr, "DownloadSubscriptionList", MakeVersionGate({18,0,0})},
        FunctionInfo{21, nullptr, "CreateToken"},
        FunctionInfo{22, nullptr, "CreateTokenWithApplicationId"},
        FunctionInfo{23, nullptr, "DestroyToken"},
        FunctionInfo{24, nullptr, "DestroyTokenWithApplicationId"},
        FunctionInfo{25, nullptr, "QueryIsTokenValid"},
        FunctionInfo{26, nullptr, "ListenToMyApplicationId"},
        FunctionInfo{27, nullptr, "DestroyTokenAll"},
        FunctionInfo{28, nullptr, "CreateTokenWithName", MakeVersionGate({18,0,0})},
        FunctionInfo{29, nullptr, "DestroyTokenWithName", MakeVersionGate({18,0,0})},
        FunctionInfo{31, nullptr, "UploadTokenToBaaS"},
        FunctionInfo{32, nullptr, "DestroyTokenForBaaS"},
        FunctionInfo{33, nullptr, "CreateTokenForBaaS"},
        FunctionInfo{34, nullptr, "SetBaaSDeviceAccountIdList"},
        FunctionInfo{35, nullptr, "LinkNsaId", MakeVersionGate({17,0,0})},
        FunctionInfo{36, nullptr, "UnlinkNsaId", MakeVersionGate({17,0,0})},
        FunctionInfo{37, nullptr, "RelinkNsaId", MakeVersionGate({18,0,0})},
        FunctionInfo{40, nullptr, "GetNetworkServiceAccountIdTokenRequestEvent", MakeVersionGate({17,0,0})},
        FunctionInfo{41, nullptr, "TryPopNetworkServiceAccountIdTokenRequestUid", MakeVersionGate({17,0,0})},
        FunctionInfo{42, nullptr, "SetNetworkServiceAccountIdTokenSuccess", MakeVersionGate({17,0,0})},
        FunctionInfo{43, nullptr, "SetNetworkServiceAccountIdTokenFailure", MakeVersionGate({17,0,0})},
        FunctionInfo{44, nullptr, "SetUidList", MakeVersionGate({17,0,0})},
        FunctionInfo{45, nullptr, "PutDigitalTwinKeyValue", MakeVersionGate({17,0,0})},
        FunctionInfo{51, nullptr, "DeleteDigitalTwinKeyValue", MakeVersionGate({18,0,0})},
        FunctionInfo{101, nullptr, "Suspend"},
        FunctionInfo{102, nullptr, "Resume"},
        FunctionInfo{103, C<&INpnsSystem::GetState>, "GetState"},
        FunctionInfo{104, nullptr, "GetStatistics"},
        FunctionInfo{105, nullptr, "GetPlayReportRequestEvent"},
        FunctionInfo{106, C<&INpnsSystem::GetLastNotifiedTime>, "GetLastNotifiedTime", MakeVersionGate({18,0,0})},
        FunctionInfo{107, nullptr, "SetLastNotifiedTime", MakeVersionGate({18,0,0})},
        FunctionInfo{111, nullptr, "GetJid"},
        FunctionInfo{112, nullptr, "CreateJid"},
        FunctionInfo{113, nullptr, "DestroyJid"},
        FunctionInfo{114, nullptr, "AttachJid"},
        FunctionInfo{115, nullptr, "DetachJid"},
        FunctionInfo{120, nullptr, "CreateNotificationReceiver"},
        FunctionInfo{151, nullptr, "GetStateWithHandover"},
        FunctionInfo{152, nullptr, "GetStateChangeEventWithHandover"},
        FunctionInfo{153, nullptr, "GetDropEventWithHandover"},
        FunctionInfo{154, nullptr, "CreateTokenAsync"},
        FunctionInfo{155, nullptr, "CreateTokenAsyncWithApplicationId"},
        FunctionInfo{156, nullptr, "CreateTokenWithNameAsync", MakeVersionGate({18,0,0})},
        FunctionInfo{161, C<&INpnsSystem::GetRequestChangeStateCancelEvent>, "GetRequestChangeStateCancelEvent", MakeVersionGate({10,0,0})},
        FunctionInfo{162, nullptr, "RequestChangeStateForceTimedWithCancelEvent"},
        FunctionInfo{201, nullptr, "RequestChangeStateForceTimed"},
        FunctionInfo{202, nullptr, "RequestChangeStateForceAsync"},
        FunctionInfo{301, nullptr, "GetPassword", MakeVersionGate({18,0,0})},
        FunctionInfo{302, nullptr, "GetAllImmigration", MakeVersionGate({18,0,0})},
        FunctionInfo{303, nullptr, "GetNotificationHistories", MakeVersionGate({18,0,0})},
        FunctionInfo{304, nullptr, "GetPersistentConnectionSummary", MakeVersionGate({18,0,0})},
        FunctionInfo{305, nullptr, "GetDigitalTwinSummary", MakeVersionGate({18,0,0})},
        FunctionInfo{306, nullptr, "GetDigitalTwinValue", MakeVersionGate({18,0,0})}
    );
    KernelHelpers::ServiceContext service_context;
    Event get_receive_event;
    Event get_request_change_state_cancel_event;
};

class INpnsUser final : public ServiceFramework<INpnsUser> {
public:
    explicit INpnsUser(Core::System& system_)
        : ServiceFramework{system_, "npns:u"}, service_context{system, "npns:u"}, get_receive_event{service_context} {}

    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }

private:
    Result ListenToByName(InBuffer<BufferAttr_HipcMapAlias> name_buffer) {
        const std::string name(reinterpret_cast<const char*>(name_buffer.data()), name_buffer.size());
        LOG_DEBUG(Service_NPNS, "called, name={}", name);

        // Store the name for future use if needed
        // For now, just acknowledge the registration
        R_SUCCEED();
    }

    Result GetReceiveEvent(OutCopyHandle<Kernel::KReadableEvent> out_event) {
        LOG_DEBUG(Service_NPNS, "called");

        *out_event = get_receive_event.GetHandle();
        R_SUCCEED();
    }

    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{1, nullptr, "ListenAll"},
        FunctionInfo{2, nullptr, "ListenTo"},
        FunctionInfo{3, nullptr, "Receive"},
        FunctionInfo{4, nullptr, "ReceiveRaw"},
        FunctionInfo{5, C<&INpnsUser::GetReceiveEvent>, "GetReceiveEvent"},
        FunctionInfo{7, nullptr, "GetStateChangeEvent"},
        FunctionInfo{8, C<&INpnsUser::ListenToByName>, "ListenToByName", MakeVersionGate({18,0,0})},
        FunctionInfo{21, nullptr, "CreateToken"},
        FunctionInfo{23, nullptr, "DestroyToken"},
        FunctionInfo{25, nullptr, "QueryIsTokenValid"},
        FunctionInfo{26, nullptr, "ListenToMyApplicationId"},
        FunctionInfo{101, nullptr, "Suspend"},
        FunctionInfo{102, nullptr, "Resume"},
        FunctionInfo{103, nullptr, "GetState"},
        FunctionInfo{104, nullptr, "GetStatistics"},
        FunctionInfo{111, nullptr, "GetJid"},
        FunctionInfo{120, nullptr, "CreateNotificationReceiver"},
        FunctionInfo{151, nullptr, "GetStateWithHandover"},
        FunctionInfo{152, nullptr, "GetStateChangeEventWithHandover"},
        FunctionInfo{153, nullptr, "GetDropEventWithHandover"},
        FunctionInfo{154, nullptr, "CreateTokenAsync"}
    );
    KernelHelpers::ServiceContext service_context;
    Event get_receive_event;
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("npns:s", std::make_shared<INpnsSystem>(system));
    server_manager->RegisterNamedService("npns:u", std::make_shared<INpnsUser>(system));
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::NPNS
