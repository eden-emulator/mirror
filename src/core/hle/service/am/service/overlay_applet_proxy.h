// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"
#include "core/hle/service/am/service/overlay_functions.h"

namespace Service::AM {

struct Applet;
class IAppletCommonFunctions;
class IAudioController;
class ICommonStateGetter;
class IDebugFunctions;
class IDisplayController;
class IHomeMenuFunctions;
class IGlobalStateController;
class ILibraryAppletCreator;
class ILibraryAppletSelfAccessor;
class IProcessWindingController;
class ISelfController;
class IWindowController;
class WindowSystem;

class IOverlayAppletProxy final : public ServiceFramework<IOverlayAppletProxy> {
public:
    explicit IOverlayAppletProxy(Core::System& system_, std::shared_ptr<Applet> applet,
                                 Kernel::KProcess* process, WindowSystem& window_system);
    ~IOverlayAppletProxy();

private:
    Result GetCommonStateGetter(Out<SharedPointer<ICommonStateGetter>> out_common_state_getter);
    Result GetSelfController(Out<SharedPointer<ISelfController>> out_self_controller);
    Result GetWindowController(Out<SharedPointer<IWindowController>> out_window_controller);
    Result GetAudioController(Out<SharedPointer<IAudioController>> out_audio_controller);
    Result GetDisplayController(Out<SharedPointer<IDisplayController>> out_display_controller);
    Result GetProcessWindingController(
        Out<SharedPointer<IProcessWindingController>> out_process_winding_controller);
    Result GetLibraryAppletCreator(
        Out<SharedPointer<ILibraryAppletCreator>> out_library_applet_creator);
    Result GetOverlayFunctions(Out<SharedPointer<IOverlayFunctions>> out_overlay_functions);
    Result GetAppletCommonFunctions(
        Out<SharedPointer<IAppletCommonFunctions>> out_applet_common_functions);
    Result GetGlobalStateController(
        Out<SharedPointer<IGlobalStateController>> out_global_state_controller);
    Result GetDebugFunctions(Out<SharedPointer<IDebugFunctions>> out_debug_functions);

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, D<&IOverlayAppletProxy::GetCommonStateGetter>, "GetCommonStateGetter"},
        FunctionInfo{1, D<&IOverlayAppletProxy::GetSelfController>, "GetSelfController"},
        FunctionInfo{2, D<&IOverlayAppletProxy::GetWindowController>, "GetWindowController"},
        FunctionInfo{3, D<&IOverlayAppletProxy::GetAudioController>, "GetAudioController"},
        FunctionInfo{4, D<&IOverlayAppletProxy::GetDisplayController>, "GetDisplayController"},
        FunctionInfo{10, D<&IOverlayAppletProxy::GetProcessWindingController>, "GetProcessWindingController"},
        FunctionInfo{11, D<&IOverlayAppletProxy::GetLibraryAppletCreator>, "GetLibraryAppletCreator"},
        FunctionInfo{20, D<&IOverlayAppletProxy::GetOverlayFunctions>, "GetOverlayFunctions"},
        FunctionInfo{21, D<&IOverlayAppletProxy::GetAppletCommonFunctions>, "GetAppletCommonFunctions"},
        FunctionInfo{23, D<&IOverlayAppletProxy::GetGlobalStateController>, "GetGlobalStateController"},
        FunctionInfo{1000, D<&IOverlayAppletProxy::GetDebugFunctions>, "GetDebugFunctions"}
    );
    WindowSystem& m_window_system;
    Kernel::KProcess* const m_process;
    const std::shared_ptr<Applet> m_applet;
};

} // namespace Service::AM
