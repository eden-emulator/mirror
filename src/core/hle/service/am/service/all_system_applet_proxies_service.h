// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"
#include "core/hle/service/am/service/overlay_applet_proxy.h"

namespace Service {

namespace AM {

struct Applet;
struct AppletAttribute;
class IApplicationProxy;
class ILibraryAppletProxy;
class ISystemAppletProxy;
class WindowSystem;

class IAllSystemAppletProxiesService final
    : public ServiceFramework<IAllSystemAppletProxiesService> {
public:
    explicit IAllSystemAppletProxiesService(Core::System& system_, WindowSystem& window_system);
    ~IAllSystemAppletProxiesService() override;

private:
    Result OpenSystemAppletProxy(Out<SharedPointer<ISystemAppletProxy>> out_system_applet_proxy,
                                 ClientProcessId pid,
                                 InCopyHandle<Kernel::KProcess> process_handle);
    Result OpenLibraryAppletProxy(Out<SharedPointer<ILibraryAppletProxy>> out_library_applet_proxy,
                                  ClientProcessId pid,
                                  InCopyHandle<Kernel::KProcess> process_handle,
                                  InLargeData<AppletAttribute, BufferAttr_HipcMapAlias> attribute);
    Result OpenOverlayAppletProxy(Out<SharedPointer<IOverlayAppletProxy>> out_overlay_applet_proxy,
                           ClientProcessId pid, InCopyHandle<Kernel::KProcess> process_handle);
    Result OpenLibraryAppletProxyOld(
        Out<SharedPointer<ILibraryAppletProxy>> out_library_applet_proxy, ClientProcessId pid,
        InCopyHandle<Kernel::KProcess> process_handle);
    Result OpenSystemApplicationProxy(
        Out<SharedPointer<IApplicationProxy>> out_system_application_proxy, ClientProcessId pid,
        InCopyHandle<Kernel::KProcess> process_handle);
    Result GetSystemProcessCommonFunctions();
    Result GetAppletAlternativeFunctions();

private:
    std::shared_ptr<Applet> GetAppletFromProcessId(ProcessId pid);

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{100, D<&IAllSystemAppletProxiesService::OpenSystemAppletProxy>, "OpenSystemAppletProxy"},
        FunctionInfo{110, D<&IAllSystemAppletProxiesService::OpenSystemAppletProxy>, "OpenSystemAppletProxyEx"},
        FunctionInfo{200, D<&IAllSystemAppletProxiesService::OpenLibraryAppletProxyOld>, "OpenLibraryAppletProxyOld"},
        FunctionInfo{201, D<&IAllSystemAppletProxiesService::OpenLibraryAppletProxy>, "OpenLibraryAppletProxy"},
        FunctionInfo{300, D<&IAllSystemAppletProxiesService::OpenOverlayAppletProxy>, "OpenOverlayAppletProxy"},
        FunctionInfo{350, D<&IAllSystemAppletProxiesService::OpenSystemApplicationProxy>, "OpenSystemApplicationProxy"},
        FunctionInfo{400, nullptr, "CreateSelfLibraryAppletCreatorForDevelop"},
        FunctionInfo{410, nullptr, "GetSystemAppletControllerForDebug"},
        FunctionInfo{450, D<&IAllSystemAppletProxiesService::GetSystemProcessCommonFunctions>, "GetSystemProcessCommonFunctions"}, // 19.0.0+
        FunctionInfo{460, D<&IAllSystemAppletProxiesService::GetAppletAlternativeFunctions>, "GetAppletAlternativeFunctions"}, // 20.0.0+
        FunctionInfo{1000, nullptr, "GetDebugFunctions"}
    );
    WindowSystem& m_window_system;
};

} // namespace AM
} // namespace Service
