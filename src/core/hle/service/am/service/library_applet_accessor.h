// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::AM {

class AppletDataBroker;
struct Applet;
class IStorage;

struct LibraryAppletInfo {
    AppletId applet_id;
    LibraryAppletMode library_applet_mode;
};
static_assert(sizeof(LibraryAppletInfo) == 0x8, "LibraryAppletInfo has incorrect size.");

struct ErrorCode {
    u32 category;
    u32 number;
};
static_assert(sizeof(ErrorCode) == 0x8, "ErrorCode has incorrect size.");

struct ErrorContext {
    u8 type;
    INSERT_PADDING_BYTES_NOINIT(0x7);
    std::array<u8, 0x1f4> data;
    Result result;
};
static_assert(sizeof(ErrorContext) == 0x200, "ErrorContext has incorrect size.");

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

    std::optional<FunctionInfoBase> FindRequest(u32 key) override;
    const std::shared_ptr<AppletDataBroker> m_broker;
    const std::shared_ptr<Applet> m_applet;
};

} // namespace Service::AM
