// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <list>
#include <memory>

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/psc/time/common.h"
#include "core/hle/service/psc/time/manager.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Kernel {
class KReadableEvent;
}

namespace Service::PSC::Time {
class StaticService;

class ServiceManager final : public ServiceFramework<ServiceManager> {
public:
    explicit ServiceManager(Core::System& system, std::shared_ptr<TimeManager> time,
                            ServerManager* server_manager);
    ~ServiceManager() override = default;

    Result GetStaticServiceAsUser(OutInterface<StaticService> out_service);
    Result GetStaticServiceAsAdmin(OutInterface<StaticService> out_service);
    Result GetStaticServiceAsRepair(OutInterface<StaticService> out_service);
    Result GetStaticServiceAsServiceManager(OutInterface<StaticService> out_service);
    Result SetupStandardSteadyClockCore(bool is_rtc_reset_detected,
                                        const Common::UUID& clock_source_id, s64 rtc_offset,
                                        s64 internal_offset, s64 test_offset);
    Result SetupStandardLocalSystemClockCore(const SystemClockContext& context, s64 time);
    Result SetupStandardNetworkSystemClockCore(SystemClockContext context, s64 accuracy);
    Result SetupStandardUserSystemClockCore(bool automatic_correction,
                                            SteadyClockTimePoint time_point);
    Result SetupTimeZoneServiceCore(const LocationName& name, const RuleVersion& rule_version,
                                    u32 location_count, const SteadyClockTimePoint& time_point,
                                    InBuffer<BufferAttr_HipcAutoSelect> rule_buffer);
    Result SetupEphemeralNetworkSystemClockCore();
    Result GetStandardLocalClockOperationEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result GetStandardNetworkClockOperationEventForServiceManager(
        OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result GetEphemeralNetworkClockOperationEventForServiceManager(
        OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result GetStandardUserSystemClockAutomaticCorrectionUpdatedEvent(
        OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result SetStandardSteadyClockBaseTime(s64 base_time);
    Result GetClosestAlarmUpdatedEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result CheckAndSignalAlarms();
    Result GetClosestAlarmInfo(Out<bool> out_is_valid, Out<AlarmInfo> out_info, Out<s64> out_time);

private:
    void CheckAndSetupServicesSAndP();
    void SetupSAndP();
    Result GetStaticService(OutInterface<StaticService> out_service, StaticServiceSetupInfo setup_info, const char* name);

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0,   D<&ServiceManager::GetStaticServiceAsUser>, "GetStaticServiceAsUser"},
        FunctionInfo{5,   D<&ServiceManager::GetStaticServiceAsAdmin>, "GetStaticServiceAsAdmin"},
        FunctionInfo{6,   D<&ServiceManager::GetStaticServiceAsRepair>, "GetStaticServiceAsRepair"},
        FunctionInfo{9,   D<&ServiceManager::GetStaticServiceAsServiceManager>, "GetStaticServiceAsServiceManager"},
        FunctionInfo{10,  D<&ServiceManager::SetupStandardSteadyClockCore>, "SetupStandardSteadyClockCore"},
        FunctionInfo{11,  D<&ServiceManager::SetupStandardLocalSystemClockCore>, "SetupStandardLocalSystemClockCore"},
        FunctionInfo{12,  D<&ServiceManager::SetupStandardNetworkSystemClockCore>, "SetupStandardNetworkSystemClockCore"},
        FunctionInfo{13,  D<&ServiceManager::SetupStandardUserSystemClockCore>, "SetupStandardUserSystemClockCore"},
        FunctionInfo{14,  D<&ServiceManager::SetupTimeZoneServiceCore>, "SetupTimeZoneServiceCore"},
        FunctionInfo{15,  D<&ServiceManager::SetupEphemeralNetworkSystemClockCore>, "SetupEphemeralNetworkSystemClockCore"},
        FunctionInfo{50,  D<&ServiceManager::GetStandardLocalClockOperationEvent>, "GetStandardLocalClockOperationEvent"},
        FunctionInfo{51,  D<&ServiceManager::GetStandardNetworkClockOperationEventForServiceManager>, "GetStandardNetworkClockOperationEventForServiceManager"},
        FunctionInfo{52,  D<&ServiceManager::GetEphemeralNetworkClockOperationEventForServiceManager>, "GetEphemeralNetworkClockOperationEventForServiceManager"},
        FunctionInfo{60,  D<&ServiceManager::GetStandardUserSystemClockAutomaticCorrectionUpdatedEvent>, "GetStandardUserSystemClockAutomaticCorrectionUpdatedEvent"},
        FunctionInfo{100, D<&ServiceManager::SetStandardSteadyClockBaseTime>, "SetStandardSteadyClockBaseTime"},
        FunctionInfo{200, D<&ServiceManager::GetClosestAlarmUpdatedEvent>, "GetClosestAlarmUpdatedEvent"},
        FunctionInfo{201, D<&ServiceManager::CheckAndSignalAlarms>, "CheckAndSignalAlarms"},
        FunctionInfo{202, D<&ServiceManager::GetClosestAlarmInfo>, "GetClosestAlarmInfo "}
    );
    std::shared_ptr<TimeManager> m_time;
    ServerManager& m_server_manager;
    bool m_is_s_and_p_setup{};
    StandardLocalSystemClockCore& m_local_system_clock;
    StandardUserSystemClockCore& m_user_system_clock;
    StandardNetworkSystemClockCore& m_network_system_clock;
    StandardSteadyClockCore& m_steady_clock;
    TimeZone& m_time_zone;
    EphemeralNetworkSystemClockCore& m_ephemeral_network_clock;
    SharedMemory& m_shared_memory;
    Alarms& m_alarms;
    LocalSystemClockContextWriter& m_local_system_context_writer;
    NetworkSystemClockContextWriter& m_network_system_context_writer;
    EphemeralNetworkSystemClockContextWriter& m_ephemeral_system_context_writer;
    OperationEvent m_local_operation;
    OperationEvent m_network_operation;
    OperationEvent m_ephemeral_operation;
};

} // namespace Service::PSC::Time
