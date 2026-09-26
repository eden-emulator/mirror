// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/uuid.h"
#include "core/hle/service/am/am_types.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace FileSys {
enum class StorageId : u8;
}

namespace Kernel {
class KReadableEvent;
}

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

class ILibraryAppletSelfAccessor final : public ServiceFramework<ILibraryAppletSelfAccessor> {
public:
    explicit ILibraryAppletSelfAccessor(Core::System& system_, std::shared_ptr<Applet> applet);
    ~ILibraryAppletSelfAccessor() override;

private:
    Result PopInData(Out<SharedPointer<IStorage>> out_storage);
    Result PushOutData(SharedPointer<IStorage> storage);
    Result PopInteractiveInData(Out<SharedPointer<IStorage>> out_storage);
    Result PushInteractiveOutData(SharedPointer<IStorage> storage);
    Result GetPopInDataEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result GetPopInteractiveInDataEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result GetLibraryAppletInfo(Out<LibraryAppletInfo> out_library_applet_info);
    Result GetMainAppletIdentityInfo(Out<AppletIdentityInfo> out_identity_info);
    Result CanUseApplicationCore(Out<bool> out_can_use_application_core);
    Result GetMainAppletApplicationControlProperty(
        OutLargeData<std::array<u8, 0x4000>, BufferAttr_HipcMapAlias> out_nacp);
    Result GetMainAppletStorageId(Out<FileSys::StorageId> out_storage_id);
    Result ExitProcessAndReturn();
    Result GetCallerAppletIdentityInfo(Out<AppletIdentityInfo> out_identity_info);
    Result GetCallerAppletIdentityInfoStack(
        Out<s32> out_count,
        OutArray<AppletIdentityInfo, BufferAttr_HipcMapAlias> out_identity_info);
    Result GetDesirableKeyboardLayout(Out<u32> out_desirable_layout);
    Result ReportVisibleError(ErrorCode error_code);
    Result ReportVisibleErrorWithErrorContext(
        ErrorCode error_code, InLargeData<ErrorContext, BufferAttr_HipcMapAlias> error_context);
    Result UnpopInData(SharedPointer<IStorage> storage);
    Result GetMainAppletApplicationDesiredLanguage(Out<u64> out_desired_language);
    Result GetCurrentApplicationId(Out<u64> out_application_id);
    Result GetMainAppletAvailableUsers(Out<bool> out_can_select_any_user, Out<s32> out_users_count,
                                       OutArray<Common::UUID, BufferAttr_HipcMapAlias> out_users);
    Result ShouldSetGpuTimeSliceManually(Out<bool> out_should_set_gpu_time_slice_manually);
    Result GetLibraryAppletInfoEx(Out<LibraryAppletInfo> out_library_applet_info);

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, D<&ILibraryAppletSelfAccessor::PopInData>, "PopInData"},
        FunctionInfo{1, D<&ILibraryAppletSelfAccessor::PushOutData>, "PushOutData"},
        FunctionInfo{2, D<&ILibraryAppletSelfAccessor::PopInteractiveInData>, "PopInteractiveInData"},
        FunctionInfo{3, D<&ILibraryAppletSelfAccessor::PushInteractiveOutData>, "PushInteractiveOutData"},
        FunctionInfo{5, D<&ILibraryAppletSelfAccessor::GetPopInDataEvent>, "GetPopInDataEvent"},
        FunctionInfo{6, D<&ILibraryAppletSelfAccessor::GetPopInteractiveInDataEvent>, "GetPopInteractiveInDataEvent"},
        FunctionInfo{10, D<&ILibraryAppletSelfAccessor::ExitProcessAndReturn>, "ExitProcessAndReturn"},
        FunctionInfo{11, D<&ILibraryAppletSelfAccessor::GetLibraryAppletInfo>, "GetLibraryAppletInfo"},
        FunctionInfo{12, D<&ILibraryAppletSelfAccessor::GetMainAppletIdentityInfo>, "GetMainAppletIdentityInfo"},
        FunctionInfo{13, D<&ILibraryAppletSelfAccessor::CanUseApplicationCore>, "CanUseApplicationCore"},
        FunctionInfo{14, D<&ILibraryAppletSelfAccessor::GetCallerAppletIdentityInfo>, "GetCallerAppletIdentityInfo"},
        FunctionInfo{15, D<&ILibraryAppletSelfAccessor::GetMainAppletApplicationControlProperty>, "GetMainAppletApplicationControlProperty"},
        FunctionInfo{16, D<&ILibraryAppletSelfAccessor::GetMainAppletStorageId>, "GetMainAppletStorageId"},
        FunctionInfo{17, D<&ILibraryAppletSelfAccessor::GetCallerAppletIdentityInfoStack>, "GetCallerAppletIdentityInfoStack"},
        FunctionInfo{18, nullptr, "GetNextReturnDestinationAppletIdentityInfo"},
        FunctionInfo{19, D<&ILibraryAppletSelfAccessor::GetDesirableKeyboardLayout>, "GetDesirableKeyboardLayout"},
        FunctionInfo{20, nullptr, "PopExtraStorage"},
        FunctionInfo{25, nullptr, "GetPopExtraStorageEvent"},
        FunctionInfo{30, D<&ILibraryAppletSelfAccessor::UnpopInData>, "UnpopInData"},
        FunctionInfo{31, nullptr, "UnpopExtraStorage"},
        FunctionInfo{40, nullptr, "GetIndirectLayerProducerHandle"},
        FunctionInfo{50, D<&ILibraryAppletSelfAccessor::ReportVisibleError>, "ReportVisibleError"},
        FunctionInfo{51, D<&ILibraryAppletSelfAccessor::ReportVisibleErrorWithErrorContext>, "ReportVisibleErrorWithErrorContext"},
        FunctionInfo{60, D<&ILibraryAppletSelfAccessor::GetMainAppletApplicationDesiredLanguage>, "GetMainAppletApplicationDesiredLanguage"},
        FunctionInfo{70, D<&ILibraryAppletSelfAccessor::GetCurrentApplicationId>, "GetCurrentApplicationId"},
        FunctionInfo{80, nullptr, "RequestExitToSelf"},
        FunctionInfo{90, nullptr, "CreateApplicationAndPushAndRequestToLaunch"},
        FunctionInfo{100, nullptr, "CreateGameMovieTrimmer"},
        FunctionInfo{101, nullptr, "ReserveResourceForMovieOperation"},
        FunctionInfo{102, nullptr, "UnreserveResourceForMovieOperation"},
        FunctionInfo{110, D<&ILibraryAppletSelfAccessor::GetMainAppletAvailableUsers>, "GetMainAppletAvailableUsers"},
        FunctionInfo{120, nullptr, "GetLaunchStorageInfoForDebug"},
        FunctionInfo{130, nullptr, "GetGpuErrorDetectedSystemEvent"},
        FunctionInfo{140, nullptr, "SetApplicationMemoryReservation"},
        FunctionInfo{150, D<&ILibraryAppletSelfAccessor::ShouldSetGpuTimeSliceManually>, "ShouldSetGpuTimeSliceManually"},
        FunctionInfo{160, D<&ILibraryAppletSelfAccessor::GetLibraryAppletInfoEx>, "GetLibraryAppletInfoEx"}
    );
    const std::shared_ptr<Applet> m_applet;
    const std::shared_ptr<AppletDataBroker> m_broker;
};

} // namespace Service::AM
