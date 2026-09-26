// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/settings.h"
#include "core/hle/service/acc/profile_manager.h"
#include "core/hle/service/am/applet_data_broker.h"
#include "core/hle/service/am/applet_manager.h"
#include "core/hle/service/am/frontend/applet_profile_select.h"
#include "core/hle/service/am/frontend/applets.h"
#include "core/hle/service/am/library_applet_storage.h"
#include "core/hle/service/am/service/library_applet_accessor.h"
#include "core/hle/service/am/service/storage.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::AM {

namespace {

void EnableSingleUserPlay(const std::shared_ptr<LibraryAppletStorage>& impl) {
    constexpr s64 DisplayOptionsOffset = 0x90;
    constexpr s64 IsSkipEnabledOffset = 1;
    constexpr s64 ShowSkipButtonOffset = 4;
    constexpr bool enabled = true;

    impl->Write(DisplayOptionsOffset + IsSkipEnabledOffset, &enabled, sizeof(enabled));
    impl->Write(DisplayOptionsOffset + ShowSkipButtonOffset, &enabled, sizeof(enabled));
}

void ReplaceEmptyUuidWithCurrentUser(const std::shared_ptr<LibraryAppletStorage>& impl) {
    Frontend::UiReturnArg return_arg{};
    impl->Read(0, &return_arg, sizeof(return_arg));

    if (return_arg.uuid_selected.IsValid()) {
        return;
    }

    Service::Account::ProfileManager profile_manager;
    const auto current_user_idx = Settings::values.current_user.GetValue();

    if (auto uuid = profile_manager.GetUser(current_user_idx)) {
        return_arg.result = 0;
        return_arg.uuid_selected = *uuid;
        impl->Write(0, &return_arg, sizeof(return_arg));
    }
}

} // namespace

ILibraryAppletAccessor::ILibraryAppletAccessor(Core::System& system_,
                                               std::shared_ptr<AppletDataBroker> broker,
                                               std::shared_ptr<Applet> applet)
    : ServiceFramework{system_, "ILibraryAppletAccessor"}, m_broker{std::move(broker)},
      m_applet{std::move(applet)} {
}

ILibraryAppletAccessor::~ILibraryAppletAccessor() = default;

Result ILibraryAppletAccessor::GetAppletStateChangedEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_AM, "called");
    *out_event = m_applet->state_changed_event.GetHandle();
    R_SUCCEED();
}

Result ILibraryAppletAccessor::IsCompleted(Out<bool> out_is_completed) {
    LOG_DEBUG(Service_AM, "called");
    std::scoped_lock lk{m_applet->lock};
    *out_is_completed = m_applet->is_completed;
    R_SUCCEED();
}

Result ILibraryAppletAccessor::GetResult() {
    LOG_DEBUG(Service_AM, "called");
    std::scoped_lock lk{m_applet->lock};
    R_RETURN(m_applet->terminate_result);
}

Result ILibraryAppletAccessor::PresetLibraryAppletGpuTimeSliceZero() {
    LOG_INFO(Service_AM, "(STUBBED) called");
    R_SUCCEED();
}

Result ILibraryAppletAccessor::Start() {
    LOG_DEBUG(Service_AM, "called");
    m_applet->process->Run();
    FrontendExecute();
    R_SUCCEED();
}

Result ILibraryAppletAccessor::RequestExit() {
    LOG_DEBUG(Service_AM, "called");
    {
        std::scoped_lock lk{m_applet->lock};
        m_applet->lifecycle_manager.RequestExit(system.Kernel());
    }
    FrontendRequestExit();
    R_SUCCEED();
}

Result ILibraryAppletAccessor::Terminate() {
    LOG_DEBUG(Service_AM, "called");
    m_applet->process->Terminate();
    FrontendRequestExit();
    R_SUCCEED();
}

Result ILibraryAppletAccessor::Unknown90() {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    R_SUCCEED();
}

Result ILibraryAppletAccessor::PushInData(SharedPointer<IStorage> storage) {
    LOG_DEBUG(Service_AM, "called");

    // Special case for ProfileSelect applet, to enable single user play as
    // somehow some games want an additional user and not let you continue...
    if (m_applet->applet_id == AppletId::ProfileSelect) {
        auto impl = storage->GetImpl();
        const s64 size = impl->GetSize();

        const bool is_ui_settings = size == sizeof(Frontend::UiSettings) ||
                                    size == sizeof(Frontend::UiSettingsV1);
        if (is_ui_settings) {
            EnableSingleUserPlay(impl);
        }
    }

    m_broker->GetInData().Push(system.Kernel(), storage);
    R_SUCCEED();
}

Result ILibraryAppletAccessor::PopOutData(Out<SharedPointer<IStorage>> out_storage) {
    LOG_DEBUG(Service_AM, "called");

    R_TRY(m_broker->GetOutData().Pop(system.Kernel(), out_storage.Get()));
    if (auto caller_applet = m_applet->caller_applet.lock(); caller_applet) {
        std::scoped_lock lk{caller_applet->lock};
        const bool focus_state_changed = caller_applet->lifecycle_manager.UpdateRequestedFocusState();
        const bool is_front_app = m_applet->frontend && caller_applet->lifecycle_manager.IsApplication();
        if (focus_state_changed) caller_applet->lifecycle_manager.SignalSystemEventIfNeeded(system.Kernel());
        else if (is_front_app) caller_applet->lifecycle_manager.RequestFocusStateChangedNotification(system.Kernel());
    }

    if (m_applet->applet_id == AppletId::ProfileSelect && *out_storage) {
        auto impl = (*out_storage)->GetImpl();

        if (impl->GetSize() == sizeof(Frontend::UiReturnArg)) {
            ReplaceEmptyUuidWithCurrentUser(impl);
        }
    }

    R_SUCCEED();
}

Result ILibraryAppletAccessor::PushInteractiveInData(SharedPointer<IStorage> storage) {
    LOG_DEBUG(Service_AM, "called");
    m_broker->GetInteractiveInData().Push(system.Kernel(), storage);
    FrontendExecuteInteractive();
    R_SUCCEED();
}

Result ILibraryAppletAccessor::PopInteractiveOutData(Out<SharedPointer<IStorage>> out_storage) {
    LOG_DEBUG(Service_AM, "called");
    R_RETURN(m_broker->GetInteractiveOutData().Pop(system.Kernel(), out_storage.Get()));
}

Result ILibraryAppletAccessor::GetPopOutDataEvent(OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_AM, "called");
    *out_event = m_broker->GetOutData().GetEvent();
    R_SUCCEED();
}

Result ILibraryAppletAccessor::GetPopInteractiveOutDataEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_AM, "called");
    *out_event = m_broker->GetInteractiveOutData().GetEvent();
    R_SUCCEED();
}

Result ILibraryAppletAccessor::GetIndirectLayerConsumerHandle(Out<u64> out_handle) {
    LOG_WARNING(Service_AM, "(STUBBED) called");

    // We require a non-zero handle to be valid. Using 0xdeadbeef allows us to trace if this is
    // actually used anywhere
    *out_handle = 0xdeadbeef;
    R_SUCCEED();
}

Result ILibraryAppletAccessor::GetLibraryAppletInfo(
    Out<LibraryAppletInfo> out_library_applet_info) {
    LOG_INFO(Service_AM, "called");
    *out_library_applet_info = {
        .applet_id = m_applet->applet_id,
        .library_applet_mode = m_applet->library_applet_mode,
    };
    R_SUCCEED();
}

Result ILibraryAppletAccessor::Unknown170(OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    *out_event = m_applet->unknown_event.GetHandle();
    R_SUCCEED();
}

void ILibraryAppletAccessor::FrontendExecute() {
    if (m_applet->frontend) {
        m_applet->frontend->Initialize();
        m_applet->frontend->Execute();
    }
}

void ILibraryAppletAccessor::FrontendExecuteInteractive() {
    if (m_applet->frontend) {
        m_applet->frontend->ExecuteInteractive();
        m_applet->frontend->Execute();
    }
}

void ILibraryAppletAccessor::FrontendRequestExit() {
    if (m_applet->frontend) {
        m_applet->frontend->RequestExit();
    }
}

} // namespace Service::AM
