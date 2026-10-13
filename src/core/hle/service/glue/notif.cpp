// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cstring>

#include "common/assert.h"
#include "common/logging.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/glue/notif.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/kernel_helpers.h"

namespace Service::Glue {

namespace {
constexpr inline std::size_t MaxAlarms = 8;
}

std::optional<ServiceFrameworkBase::FunctionInfoBase> INotificationServicesForApplication::FindRequest(u32 key) {
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{500, D<&INotificationServicesForApplication::RegisterAlarmSetting>, "RegisterAlarmSetting"},
        FunctionInfo{510, D<&INotificationServicesForApplication::UpdateAlarmSetting>, "UpdateAlarmSetting"},
        FunctionInfo{520, D<&INotificationServicesForApplication::ListAlarmSettings>, "ListAlarmSettings"},
        FunctionInfo{530, D<&INotificationServicesForApplication::LoadApplicationParameter>, "LoadApplicationParameter"},
        FunctionInfo{540, D<&INotificationServicesForApplication::DeleteAlarmSetting>, "DeleteAlarmSetting"},
        FunctionInfo{1000, D<&INotificationServicesForApplication::Initialize>, "Initialize"}
    );
    return HandlerTableGenerateWithFind(key, functions);
}

std::optional<ServiceFrameworkBase::FunctionInfoBase> INotificationServices::FindRequest(u32 key) {
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{500, D<&INotificationServices::RegisterAlarmSetting>, "RegisterAlarmSetting"},
        FunctionInfo{510, D<&INotificationServices::UpdateAlarmSetting>, "UpdateAlarmSetting"},
        FunctionInfo{520, D<&INotificationServices::ListAlarmSettings>, "ListAlarmSettings"},
        FunctionInfo{530, D<&INotificationServices::LoadApplicationParameter>, "LoadApplicationParameter"},
        FunctionInfo{540, D<&INotificationServices::DeleteAlarmSetting>, "DeleteAlarmSetting"},
        FunctionInfo{1000, D<&INotificationServices::Initialize>, "Initialize"},
        FunctionInfo{1010, nullptr, "ListNotifications"},
        FunctionInfo{1020, nullptr, "DeleteNotification"},
        FunctionInfo{1030, nullptr, "ClearNotifications"},
        FunctionInfo{1040, D<&INotificationServices::OpenNotificationSystemEventAccessor>, "OpenNotificationSystemEventAccessor"},
        FunctionInfo{1500, nullptr, "SetNotificationPresentationSetting"},
        FunctionInfo{1510, D<&INotificationServices::GetNotificationPresentationSetting>, "GetNotificationPresentationSetting"},
        FunctionInfo{2000, nullptr, "GetAlarmSetting"},
        FunctionInfo{2001, nullptr, "GetAlarmSettingWithApplicationParameter"},
        FunctionInfo{2010, nullptr, "MuteAlarmSetting"},
        FunctionInfo{2020, nullptr, "IsAlarmSettingReady"},
        FunctionInfo{8000, nullptr, "RegisterAppletResourceUserId"},
        FunctionInfo{8010, nullptr, "UnregisterAppletResourceUserId"},
        FunctionInfo{8999, nullptr, "GetCurrentTime"},
        FunctionInfo{9000, nullptr, "GetAlarmSettingNextNotificationTime"}
    );
    return HandlerTableGenerateWithFind(key, functions);
}

Result NotificationServiceImpl::RegisterAlarmSetting(AlarmSettingId* out_alarm_setting_id,
                                                     const AlarmSetting& alarm_setting,
                                                     std::span<const u8> application_parameter) {
    if (alarms.size() > MaxAlarms) {
        LOG_ERROR(Service_NOTIF, "Alarm limit reached");
        R_THROW(ResultUnknown);
    }

    ASSERT_MSG(application_parameter.size() <= sizeof(ApplicationParameter),
               "application_parameter_size is bigger than 0x400 bytes");

    AlarmSetting new_alarm = alarm_setting;
    new_alarm.alarm_setting_id = last_alarm_setting_id++;
    alarms.push_back(new_alarm);

    // TODO: Save application parameter data

    LOG_WARNING(Service_NOTIF,
                "(STUBBED) called, application_parameter_size={}, setting_id={}, kind={}, muted={}",
                application_parameter.size(), new_alarm.alarm_setting_id, new_alarm.kind,
                new_alarm.muted);

    *out_alarm_setting_id = new_alarm.alarm_setting_id;
    R_SUCCEED();
}

Result NotificationServiceImpl::UpdateAlarmSetting(const AlarmSetting& alarm_setting,
                                                   std::span<const u8> application_parameter) {
    ASSERT_MSG(application_parameter.size() <= sizeof(ApplicationParameter),
               "application_parameter_size is bigger than 0x400 bytes");

    const auto alarm_it = GetAlarmFromId(alarm_setting.alarm_setting_id);
    if (alarm_it != alarms.end()) {
        LOG_DEBUG(Service_NOTIF, "Alarm updated");
        *alarm_it = alarm_setting;
    }

    LOG_WARNING(Service_NOTIF,
                "(STUBBED) called, application_parameter_size={}, setting_id={}, kind={}, muted={}",
                application_parameter.size(), alarm_setting.alarm_setting_id, alarm_setting.kind,
                alarm_setting.muted);
    R_SUCCEED();
}

Result NotificationServiceImpl::ListAlarmSettings(s32* out_count,
                                                  std::span<AlarmSetting> out_alarms) {
    LOG_INFO(Service_NOTIF, "called, alarm_count={}", alarms.size());

    const auto count = (std::min)(out_alarms.size(), alarms.size());
    for (size_t i = 0; i < count; i++) {
        out_alarms[i] = alarms[i];
    }

    *out_count = static_cast<s32>(count);
    R_SUCCEED();
}

Result NotificationServiceImpl::LoadApplicationParameter(u32* out_size,
                                                         std::span<u8> out_application_parameter,
                                                         AlarmSettingId alarm_setting_id) {
    const auto alarm_it = GetAlarmFromId(alarm_setting_id);
    if (alarm_it == alarms.end()) {
        LOG_ERROR(Service_NOTIF, "Invalid alarm setting id={}", alarm_setting_id);
        R_THROW(ResultUnknown);
    }

    // TODO: Read application parameter related to this setting id
    ApplicationParameter application_parameter{};

    LOG_WARNING(Service_NOTIF, "(STUBBED) called, alarm_setting_id={}", alarm_setting_id);
    std::memcpy(out_application_parameter.data(), application_parameter.data(),
                (std::min)(sizeof(application_parameter), out_application_parameter.size()));

    *out_size = static_cast<u32>(application_parameter.size());
    R_SUCCEED();
}

Result NotificationServiceImpl::DeleteAlarmSetting(AlarmSettingId alarm_setting_id) {
    std::erase_if(alarms, [alarm_setting_id](const AlarmSetting& alarm) {
        return alarm.alarm_setting_id == alarm_setting_id;
    });

    LOG_INFO(Service_NOTIF, "called, alarm_setting_id={}", alarm_setting_id);

    R_SUCCEED();
}

Result NotificationServiceImpl::Initialize(u64 aruid) {
    // TODO: Load previous alarms from config

    LOG_WARNING(Service_NOTIF, "(STUBBED) called");
    R_SUCCEED();
}

std::vector<AlarmSetting>::iterator NotificationServiceImpl::GetAlarmFromId(AlarmSettingId alarm_setting_id) {
    return std::find_if(alarms.begin(), alarms.end(), [alarm_setting_id](const AlarmSetting& alarm) {
        return alarm.alarm_setting_id == alarm_setting_id;
    });
}

INotificationServicesForApplication::INotificationServicesForApplication(Core::System& system_)
    : ServiceFramework{system_, "notif:a"} {
}

INotificationServicesForApplication::~INotificationServicesForApplication() = default;

Result INotificationServicesForApplication::RegisterAlarmSetting(
    Out<AlarmSettingId> out_alarm_setting_id,
    InLargeData<AlarmSetting, BufferAttr_HipcMapAlias> alarm_setting,
    InBuffer<BufferAttr_HipcMapAlias> application_parameter) {
    R_RETURN(impl.RegisterAlarmSetting(out_alarm_setting_id.Get(), *alarm_setting,
                                       application_parameter));
}

Result INotificationServicesForApplication::UpdateAlarmSetting(
    InLargeData<AlarmSetting, BufferAttr_HipcMapAlias> alarm_setting,
    InBuffer<BufferAttr_HipcMapAlias> application_parameter) {
    R_RETURN(impl.UpdateAlarmSetting(*alarm_setting, application_parameter));
}

Result INotificationServicesForApplication::ListAlarmSettings(
    Out<s32> out_count, OutArray<AlarmSetting, BufferAttr_HipcMapAlias> out_alarms) {
    R_RETURN(impl.ListAlarmSettings(out_count.Get(), out_alarms));
}

Result INotificationServicesForApplication::LoadApplicationParameter(
    Out<u32> out_size, OutBuffer<BufferAttr_HipcMapAlias> out_application_parameter,
    AlarmSettingId alarm_setting_id) {
    R_RETURN(
        impl.LoadApplicationParameter(out_size.Get(), out_application_parameter, alarm_setting_id));
}

Result INotificationServicesForApplication::DeleteAlarmSetting(AlarmSettingId alarm_setting_id) {
    R_RETURN(impl.DeleteAlarmSetting(alarm_setting_id));
}

Result INotificationServicesForApplication::Initialize(ClientAppletResourceUserId aruid) {
    R_RETURN(impl.Initialize(*aruid));
}

class INotificationSystemEventAccessor final
    : public ServiceFramework<INotificationSystemEventAccessor> {
public:
    explicit INotificationSystemEventAccessor(Core::System& system_)
        : ServiceFramework{system_, "INotificationSystemEventAccessor"}
        , service_context{system_, "INotificationSystemEventAccessor"} {
        notification_event = service_context.CreateEvent("INotificationSystemEventAccessor:NotificationEvent");
    }

    std::optional<FunctionInfoBase> FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }

    ~INotificationSystemEventAccessor() {
        service_context.CloseEvent(notification_event);
    }

private:
    Result GetSystemEvent(OutCopyHandle<Kernel::KReadableEvent> out_readable_event) {
        LOG_WARNING(Service_NOTIF, "(STUBBED) called");

        *out_readable_event = &notification_event->GetReadableEvent();
        R_SUCCEED();
    }

    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, D<&INotificationSystemEventAccessor::GetSystemEvent>, "GetSystemEvent"}
    );
    KernelHelpers::ServiceContext service_context;
    Kernel::KEvent* notification_event;
};

INotificationServices::INotificationServices(Core::System& system_)
    : ServiceFramework{system_, "notif:s"} {
}

INotificationServices::~INotificationServices() = default;

Result INotificationServices::RegisterAlarmSetting(
    Out<AlarmSettingId> out_alarm_setting_id,
    InLargeData<AlarmSetting, BufferAttr_HipcMapAlias> alarm_setting,
    InBuffer<BufferAttr_HipcMapAlias> application_parameter) {
    R_RETURN(impl.RegisterAlarmSetting(out_alarm_setting_id.Get(), *alarm_setting,
                                       application_parameter));
}

Result INotificationServices::UpdateAlarmSetting(
    InLargeData<AlarmSetting, BufferAttr_HipcMapAlias> alarm_setting,
    InBuffer<BufferAttr_HipcMapAlias> application_parameter) {
    R_RETURN(impl.UpdateAlarmSetting(*alarm_setting, application_parameter));
}

Result INotificationServices::ListAlarmSettings(
    Out<s32> out_count, OutArray<AlarmSetting, BufferAttr_HipcMapAlias> out_alarms) {
    R_RETURN(impl.ListAlarmSettings(out_count.Get(), out_alarms));
}

Result INotificationServices::LoadApplicationParameter(
    Out<u32> out_size, OutBuffer<BufferAttr_HipcMapAlias> out_application_parameter,
    AlarmSettingId alarm_setting_id) {
    R_RETURN(
        impl.LoadApplicationParameter(out_size.Get(), out_application_parameter, alarm_setting_id));
}

Result INotificationServices::DeleteAlarmSetting(AlarmSettingId alarm_setting_id) {
    R_RETURN(impl.DeleteAlarmSetting(alarm_setting_id));
}

Result INotificationServices::Initialize(ClientAppletResourceUserId aruid) {
    R_RETURN(impl.Initialize(*aruid));
}

Result INotificationServices::OpenNotificationSystemEventAccessor(
    Out<SharedPointer<INotificationSystemEventAccessor>> out_notification_system_event_accessor) {
    LOG_WARNING(Service_NOTIF, "(STUBBED) called");

    *out_notification_system_event_accessor =
        std::make_shared<INotificationSystemEventAccessor>(system);
    R_SUCCEED();
}

Result INotificationServices::GetNotificationPresentationSetting(
    Out<NotificationPresentationSetting> out_notification_presentation_setting,
    NotificationChannel notification_channel) {
    LOG_WARNING(Service_NOTIF, "(STUBBED) called");

    *out_notification_presentation_setting = {};
    R_SUCCEED();
}

} // namespace Service::Glue
