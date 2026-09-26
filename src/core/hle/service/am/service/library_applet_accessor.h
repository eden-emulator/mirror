// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/am/service/library_applet_self_accessor.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::AM {

class AppletDataBroker;
struct Applet;
class IStorage;

class ILibraryAppletAccessor final : public ServiceFramework<ILibraryAppletAccessor> {
public:
    explicit ILibraryAppletAccessor(Core::System& system_, std::shared_ptr<AppletDataBroker> broker,
                                    std::shared_ptr<Applet> applet);
    ~ILibraryAppletAccessor();

    std::shared_ptr<Applet> GetApplet() const {
        return m_applet;
    }

private:
    Result GetAppletStateChangedEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result IsCompleted(Out<bool> out_is_completed);
    Result GetResult();
    Result PresetLibraryAppletGpuTimeSliceZero();
    Result Start();
    Result RequestExit();
    Result Terminate();
    Result Unknown90();
    Result PushInData(SharedPointer<IStorage> storage);
    Result PopOutData(Out<SharedPointer<IStorage>> out_storage);
    Result PushInteractiveInData(SharedPointer<IStorage> storage);
    Result PopInteractiveOutData(Out<SharedPointer<IStorage>> out_storage);
    Result GetPopOutDataEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result GetPopInteractiveOutDataEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result GetIndirectLayerConsumerHandle(Out<u64> out_handle);
    Result GetLibraryAppletInfo(Out<LibraryAppletInfo> out_library_applet_info);
    Result Unknown170(OutCopyHandle<Kernel::KReadableEvent> out_event);

    void FrontendExecute();
    void FrontendExecuteInteractive();
    void FrontendRequestExit();

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, D<&ILibraryAppletAccessor::GetAppletStateChangedEvent>, "GetAppletStateChangedEvent"},
        FunctionInfo{1, D<&ILibraryAppletAccessor::IsCompleted>, "IsCompleted"},
        FunctionInfo{10, D<&ILibraryAppletAccessor::Start>, "Start"},
        FunctionInfo{20, D<&ILibraryAppletAccessor::RequestExit>, "RequestExit"},
        FunctionInfo{25, D<&ILibraryAppletAccessor::Terminate>, "Terminate"},
        FunctionInfo{30, D<&ILibraryAppletAccessor::GetResult>, "GetResult"},
        FunctionInfo{50, nullptr, "SetOutOfFocusApplicationSuspendingEnabled"},
        FunctionInfo{60, D<&ILibraryAppletAccessor::PresetLibraryAppletGpuTimeSliceZero>, "PresetLibraryAppletGpuTimeSliceZero"}, //10.0.0+
        FunctionInfo{80, nullptr, "RequestForLibraryAppletToGetForeground"}, //19.0.0+
        FunctionInfo{81, nullptr, "GetCurrentChildLibraryApplet"}, //19.0.0+
        FunctionInfo{90, D<&ILibraryAppletAccessor::Unknown90>, "Unknown90"}, //20.0.0+
        FunctionInfo{100, D<&ILibraryAppletAccessor::PushInData>, "PushInData"},
        FunctionInfo{101, D<&ILibraryAppletAccessor::PopOutData>, "PopOutData"},
        FunctionInfo{102, nullptr, "PushExtraStorage"},
        FunctionInfo{103, D<&ILibraryAppletAccessor::PushInteractiveInData>, "PushInteractiveInData"},
        FunctionInfo{104, D<&ILibraryAppletAccessor::PopInteractiveOutData>, "PopInteractiveOutData"},
        FunctionInfo{105, D<&ILibraryAppletAccessor::GetPopOutDataEvent>, "GetPopOutDataEvent"},
        FunctionInfo{106, D<&ILibraryAppletAccessor::GetPopInteractiveOutDataEvent>, "GetPopInteractiveOutDataEvent"},
        FunctionInfo{110, nullptr, "NeedsToExitProcess"},
        FunctionInfo{120, D<&ILibraryAppletAccessor::GetLibraryAppletInfo>, "GetLibraryAppletInfo"},
        FunctionInfo{150, nullptr, "RequestForAppletToGetForeground"},
        FunctionInfo{160, D<&ILibraryAppletAccessor::GetIndirectLayerConsumerHandle>, "GetIndirectLayerConsumerHandle"}, //2.0.0+
        FunctionInfo{170, D<&ILibraryAppletAccessor::Unknown170>, "Unknown170"}, //22.0.0+
    );
    const std::shared_ptr<AppletDataBroker> m_broker;
    const std::shared_ptr<Applet> m_applet;
};

} // namespace Service::AM
