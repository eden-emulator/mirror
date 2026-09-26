// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::AM {

struct Applet;
class WindowSystem;

class IWindowController final : public ServiceFramework<IWindowController> {
public:
    explicit IWindowController(Core::System& system_, std::shared_ptr<Applet> applet,
                               WindowSystem& window_system);
    ~IWindowController() override;

private:
    Result GetAppletResourceUserId(Out<AppletResourceUserId> out_aruid);
    Result GetAppletResourceUserIdOfCallerApplet(Out<AppletResourceUserId> out_aruid);
    Result AcquireForegroundRights();
    Result ReleaseForegroundRights();
    Result RejectToChangeIntoBackground();
    Result SetAppletWindowVisibility(bool visible);
    Result SetAppletGpuTimeSlice(s64 time_slice);

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, nullptr, "CreateWindow"},
        FunctionInfo{1,  D<&IWindowController::GetAppletResourceUserId>, "GetAppletResourceUserId"},
        FunctionInfo{2,  D<&IWindowController::GetAppletResourceUserIdOfCallerApplet>, "GetAppletResourceUserIdOfCallerApplet"},
        FunctionInfo{10, D<&IWindowController::AcquireForegroundRights>, "AcquireForegroundRights"},
        FunctionInfo{11, D<&IWindowController::ReleaseForegroundRights>, "ReleaseForegroundRights"},
        FunctionInfo{12, D<&IWindowController::RejectToChangeIntoBackground>, "RejectToChangeIntoBackground"},
        FunctionInfo{20, D<&IWindowController::SetAppletWindowVisibility>, "SetAppletWindowVisibility"},
        FunctionInfo{21, D<&IWindowController::SetAppletGpuTimeSlice>, "SetAppletGpuTimeSlice"}
    );
    WindowSystem& m_window_system;
    const std::shared_ptr<Applet> m_applet;
};

} // namespace Service::AM
