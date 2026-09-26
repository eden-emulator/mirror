// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/am/service/storage.h"
#include "core/hle/service/am/am_types.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::AM {

struct Applet;
class ILibraryAppletAccessor;

class IProcessWindingController final : public ServiceFramework<IProcessWindingController> {
public:
    explicit IProcessWindingController(Core::System& system_, std::shared_ptr<Applet> applet_);
    ~IProcessWindingController() override;

private:
    Result GetLaunchReason(Out<AppletProcessLaunchReason> out_launch_reason);
    Result OpenCallingLibraryApplet(
        Out<SharedPointer<ILibraryAppletAccessor>> out_calling_library_applet);
    Result PushContext(SharedPointer<IStorage> in_context);
    Result PopContext(Out<SharedPointer<IStorage>> out_context);
    Result CancelWindingReservation();
    Result WindAndDoReserved();
    Result ReserveToStartAndWaitAndUnwindThis(
        SharedPointer<ILibraryAppletAccessor> reserved_applet_accessor);
    Result ReserveToStartAndWait(SharedPointer<ILibraryAppletAccessor> reserved_applet_accessor);

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, D<&IProcessWindingController::GetLaunchReason>, "GetLaunchReason"},
        FunctionInfo{11, D<&IProcessWindingController::OpenCallingLibraryApplet>, "OpenCallingLibraryApplet"},
        FunctionInfo{21, D<&IProcessWindingController::PushContext>, "PushContext"},
        FunctionInfo{22, D<&IProcessWindingController::PopContext>, "PopContext"},
        FunctionInfo{23, D<&IProcessWindingController::CancelWindingReservation>, "CancelWindingReservation"},
        FunctionInfo{30, D<&IProcessWindingController::WindAndDoReserved>, "WindAndDoReserved"},
        FunctionInfo{40, D<&IProcessWindingController::ReserveToStartAndWaitAndUnwindThis>, "ReserveToStartAndWaitAndUnwindThis"},
        FunctionInfo{41, D<&IProcessWindingController::ReserveToStartAndWait>, "ReserveToStartAndWait"}
    );
    const std::shared_ptr<Applet> m_applet;
};

} // namespace Service::AM
