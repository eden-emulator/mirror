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

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key,
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
            FunctionInfo{14, nullptr, "SubscribeTopicByAccount"}, // 18.0.0+
            FunctionInfo{15, nullptr, "UnsubscribeTopicByAccount"}, // 18.0.0+
            FunctionInfo{16, nullptr, "DownloadSubscriptionList"}, // 18.0.0+
            FunctionInfo{21, nullptr, "CreateToken"},
            FunctionInfo{22, nullptr, "CreateTokenWithApplicationId"},
            FunctionInfo{23, nullptr, "DestroyToken"},
            FunctionInfo{24, nullptr, "DestroyTokenWithApplicationId"},
            FunctionInfo{25, nullptr, "QueryIsTokenValid"},
            FunctionInfo{26, nullptr, "ListenToMyApplicationId"},
            FunctionInfo{27, nullptr, "DestroyTokenAll"},
            FunctionInfo{28, nullptr, "CreateTokenWithName"}, // 18.0.0+
            FunctionInfo{29, nullptr, "DestroyTokenWithName"}, // 18.0.0+
            FunctionInfo{31, nullptr, "UploadTokenToBaaS"},
            FunctionInfo{32, nullptr, "DestroyTokenForBaaS"},
            FunctionInfo{33, nullptr, "CreateTokenForBaaS"},
            FunctionInfo{34, nullptr, "SetBaaSDeviceAccountIdList"},
            FunctionInfo{35, nullptr, "LinkNsaId"}, // 17.0.0+
            FunctionInfo{36, nullptr, "UnlinkNsaId"}, // 17.0.0+
            FunctionInfo{37, nullptr, "RelinkNsaId"}, // 18.0.0+
            FunctionInfo{40, nullptr, "GetNetworkServiceAccountIdTokenRequestEvent"}, // 17.0.0+
            FunctionInfo{41, nullptr, "TryPopNetworkServiceAccountIdTokenRequestUid"}, // 17.0.0+
            FunctionInfo{42, nullptr, "SetNetworkServiceAccountIdTokenSuccess"}, // 17.0.0+
            FunctionInfo{43, nullptr, "SetNetworkServiceAccountIdTokenFailure"}, // 17.0.0+
            FunctionInfo{44, nullptr, "SetUidList"}, // 17.0.0+
            FunctionInfo{45, nullptr, "PutDigitalTwinKeyValue"}, // 17.0.0+
            FunctionInfo{51, nullptr, "DeleteDigitalTwinKeyValue"}, // 18.0.0+
            FunctionInfo{101, nullptr, "Suspend"},
            FunctionInfo{102, nullptr, "Resume"},
            FunctionInfo{103, C<&INpnsSystem::GetState>, "GetState"},
            FunctionInfo{104, nullptr, "GetStatistics"},
            FunctionInfo{105, nullptr, "GetPlayReportRequestEvent"},
            FunctionInfo{106, C<&INpnsSystem::GetLastNotifiedTime>, "GetLastNotifiedTime"}, // 18.0.0+
            FunctionInfo{107, nullptr, "SetLastNotifiedTime"}, // 18.0.0+
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
            FunctionInfo{156, nullptr, "CreateTokenWithNameAsync"}, // 18.0.0+
            FunctionInfo{161, C<&INpnsSystem::GetRequestChangeStateCancelEvent>, "GetRequestChangeStateCancelEvent"}, // 10.0.0+
            FunctionInfo{162, nullptr, "RequestChangeStateForceTimedWithCancelEvent"},
            FunctionInfo{201, nullptr, "RequestChangeStateForceTimed"},
            FunctionInfo{202, nullptr, "RequestChangeStateForceAsync"},
            FunctionInfo{301, nullptr, "GetPassword"}, // 18.0.0+
            FunctionInfo{302, nullptr, "GetAllImmigration"}, // 18.0.0+
            FunctionInfo{303, nullptr, "GetNotificationHistories"}, // 18.0.0+
            FunctionInfo{304, nullptr, "GetPersistentConnectionSummary"}, // 18.0.0+
            FunctionInfo{305, nullptr, "GetDigitalTwinSummary"}, // 18.0.0+
            FunctionInfo{306, nullptr, "GetDigitalTwinValue"}, // 18.0.0+
        );
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

    KernelHelpers::ServiceContext service_context;
    Event get_receive_event;
    Event get_request_change_state_cancel_event;
};

class INpnsUser final : public ServiceFramework<INpnsUser> {
public:
    explicit INpnsUser(Core::System& system_)
        : ServiceFramework{system_, "npns:u"}, service_context{system, "npns:u"}, get_receive_event{service_context} {}

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key,
            FunctionInfo{1, nullptr, "ListenAll"},
            FunctionInfo{2, nullptr, "ListenTo"},
            FunctionInfo{3, nullptr, "Receive"},
            FunctionInfo{4, nullptr, "ReceiveRaw"},
            FunctionInfo{5, C<&INpnsUser::GetReceiveEvent>, "GetReceiveEvent"},
            FunctionInfo{7, nullptr, "GetStateChangeEvent"},
            FunctionInfo{8, C<&INpnsUser::ListenToByName>, "ListenToByName"}, // 18.0.0+
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
