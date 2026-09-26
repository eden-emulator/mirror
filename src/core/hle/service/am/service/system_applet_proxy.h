// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::AM {

struct Applet;
class IAppletCommonFunctions;
class IApplicationCreator;
class IAudioController;
class ICommonStateGetter;
class IDebugFunctions;
class IDisplayController;
class IHomeMenuFunctions;
class IGlobalStateController;
class ILibraryAppletCreator;
class IProcessWindingController;
class ISelfController;
class IWindowController;
class WindowSystem;

class ISystemAppletProxy final : public ServiceFramework<ISystemAppletProxy> {
public:
    explicit ISystemAppletProxy(Core::System& system, std::shared_ptr<Applet> applet,
                                Kernel::KProcess* process, WindowSystem& window_system);
    ~ISystemAppletProxy();

private:
    Result GetCommonStateGetter(Out<SharedPointer<ICommonStateGetter>> out_common_state_getter);
    Result GetSelfController(Out<SharedPointer<ISelfController>> out_self_controller);
    Result GetWindowController(Out<SharedPointer<IWindowController>> out_window_controller);
    Result GetAudioController(Out<SharedPointer<IAudioController>> out_audio_controller);
    Result GetDisplayController(Out<SharedPointer<IDisplayController>> out_display_controller);
    Result GetProcessWindingController(
        Out<SharedPointer<IProcessWindingController>> out_process_winding_controller);
    Result GetDebugFunctions(Out<SharedPointer<IDebugFunctions>> out_debug_functions);
    Result GetLibraryAppletCreator(
        Out<SharedPointer<ILibraryAppletCreator>> out_library_applet_creator);
    Result GetApplicationCreator(Out<SharedPointer<IApplicationCreator>> out_application_creator);
    Result GetAppletCommonFunctions(
        Out<SharedPointer<IAppletCommonFunctions>> out_applet_common_functions);
    Result GetHomeMenuFunctions(Out<SharedPointer<IHomeMenuFunctions>> out_home_menu_functions);
    Result GetGlobalStateController(
        Out<SharedPointer<IGlobalStateController>> out_global_state_controller);

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, D<&ISystemAppletProxy::GetCommonStateGetter>, "GetCommonStateGetter"},
        FunctionInfo{1, D<&ISystemAppletProxy::GetSelfController>, "GetSelfController"},
        FunctionInfo{2, D<&ISystemAppletProxy::GetWindowController>, "GetWindowController"},
        FunctionInfo{3, D<&ISystemAppletProxy::GetAudioController>, "GetAudioController"},
        FunctionInfo{4, D<&ISystemAppletProxy::GetDisplayController>, "GetDisplayController"},
        FunctionInfo{10, D<&ISystemAppletProxy::GetProcessWindingController>, "GetProcessWindingController"},
        FunctionInfo{11, D<&ISystemAppletProxy::GetLibraryAppletCreator>, "GetLibraryAppletCreator"},
        FunctionInfo{20, D<&ISystemAppletProxy::GetHomeMenuFunctions>, "GetHomeMenuFunctions"},
        FunctionInfo{21, D<&ISystemAppletProxy::GetGlobalStateController>, "GetGlobalStateController"},
        FunctionInfo{22, D<&ISystemAppletProxy::GetApplicationCreator>, "GetApplicationCreator"},
        FunctionInfo{23, D<&ISystemAppletProxy::GetAppletCommonFunctions>, "GetAppletCommonFunctions"},
        FunctionInfo{1000, D<&ISystemAppletProxy::GetDebugFunctions>, "GetDebugFunctions"}
    );
    WindowSystem& m_window_system;
    Kernel::KProcess* const m_process;
    const std::shared_ptr<Applet> m_applet;
};

} // namespace Service::AM
