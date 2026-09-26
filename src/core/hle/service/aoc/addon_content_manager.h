// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Kernel {
class KEvent;
}

namespace Service::AOC {

class IPurchaseEventManager;

class IAddOnContentManager final : public ServiceFramework<IAddOnContentManager> {
public:
    explicit IAddOnContentManager(Core::System& system);
    ~IAddOnContentManager() override;

    Result CountAddOnContent(Out<u32> out_count, ClientProcessId process_id);
    Result ListAddOnContent(Out<u32> out_count, OutBuffer<BufferAttr_HipcMapAlias> out_addons,
                            u32 offset, u32 count, ClientProcessId process_id);
    Result GetAddOnContentBaseId(Out<u64> out_title_id, ClientProcessId process_id);
    Result PrepareAddOnContent(s32 addon_index, ClientProcessId process_id);
    Result GetAddOnContentListChangedEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result GetAddOnContentListChangedEventWithProcessId(
        OutCopyHandle<Kernel::KReadableEvent> out_event, ClientProcessId process_id);
    Result NotifyMountAddOnContent();
    Result NotifyUnmountAddOnContent();
    Result CheckAddOnContentMountStatus();
    Result CreateEcPurchasedEventManager(OutInterface<IPurchaseEventManager> out_interface);
    Result CreatePermanentEcPurchasedEventManager(
        OutInterface<IPurchaseEventManager> out_interface);

private:
    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, nullptr, "CountAddOnContentByApplicationId"},
        FunctionInfo{1, nullptr, "ListAddOnContentByApplicationId"},
        FunctionInfo{2, D<&IAddOnContentManager::CountAddOnContent>, "CountAddOnContent"},
        FunctionInfo{3, D<&IAddOnContentManager::ListAddOnContent>, "ListAddOnContent"},
        FunctionInfo{4, nullptr, "GetAddOnContentBaseIdByApplicationId"},
        FunctionInfo{5, D<&IAddOnContentManager::GetAddOnContentBaseId>, "GetAddOnContentBaseId"},
        FunctionInfo{6, nullptr, "PrepareAddOnContentByApplicationId"},
        FunctionInfo{7, D<&IAddOnContentManager::PrepareAddOnContent>, "PrepareAddOnContent"},
        FunctionInfo{8, D<&IAddOnContentManager::GetAddOnContentListChangedEvent>, "GetAddOnContentListChangedEvent"},
        FunctionInfo{9, nullptr, "GetAddOnContentLostErrorCode"},
        FunctionInfo{10, D<&IAddOnContentManager::GetAddOnContentListChangedEventWithProcessId>, "GetAddOnContentListChangedEventWithProcessId"},
        FunctionInfo{11, D<&IAddOnContentManager::NotifyMountAddOnContent>, "NotifyMountAddOnContent"},
        FunctionInfo{12, D<&IAddOnContentManager::NotifyUnmountAddOnContent>, "NotifyUnmountAddOnContent"},
        FunctionInfo{13, nullptr, "IsAddOnContentMountedForDebug"},
        FunctionInfo{50, D<&IAddOnContentManager::CheckAddOnContentMountStatus>, "CheckAddOnContentMountStatus"},
        FunctionInfo{100, D<&IAddOnContentManager::CreateEcPurchasedEventManager>, "CreateEcPurchasedEventManager"},
        FunctionInfo{101, D<&IAddOnContentManager::CreatePermanentEcPurchasedEventManager>, "CreatePermanentEcPurchasedEventManager"},
        FunctionInfo{110, nullptr, "CreateContentsServiceManager"},
        FunctionInfo{200, nullptr, "SetRequiredAddOnContentsOnContentsAvailabilityTransition"},
        FunctionInfo{300, nullptr, "SetupHostAddOnContent"},
        FunctionInfo{301, nullptr, "GetRegisteredAddOnContentPath"},
        FunctionInfo{302, nullptr, "UpdateCachedList"}
    );
    std::vector<u64> add_on_content;
    KernelHelpers::ServiceContext service_context;
    Kernel::KEvent* aoc_change_event;
};

void LoopProcess(Core::System& system);

} // namespace Service::AOC
