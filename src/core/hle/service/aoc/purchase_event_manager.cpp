// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/aoc/purchase_event_manager.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::AOC {

constexpr Result ResultNoPurchasedProductInfoAvailable{ErrorModule::NIMShop, 400};

ServiceFrameworkBase::FunctionInfoBase const* IPurchaseEventManager::FindRequest(u32 key) {
    static const auto functions = CreateStaticMap(
        FunctionInfo{0, D<&IPurchaseEventManager::SetDefaultDeliveryTarget>, "SetDefaultDeliveryTarget"},
        FunctionInfo{1, D<&IPurchaseEventManager::SetDeliveryTarget>, "SetDeliveryTarget"},
        FunctionInfo{2, D<&IPurchaseEventManager::GetPurchasedEvent>, "GetPurchasedEvent"},
        FunctionInfo{3, D<&IPurchaseEventManager::PopPurchasedProductInfo>, "PopPurchasedProductInfo"},
        FunctionInfo{4, D<&IPurchaseEventManager::PopPurchasedProductInfoWithUid>, "PopPurchasedProductInfoWithUid"}
    );
    return HandlerTableGenerateWithFind(key, functions);
}

IPurchaseEventManager::IPurchaseEventManager(Core::System& system_)
    : ServiceFramework{system_, "IPurchaseEventManager"}
    , service_context{system, "IPurchaseEventManager"} {
    purchased_event = service_context.CreateEvent("IPurchaseEventManager:PurchasedEvent");
}

IPurchaseEventManager::~IPurchaseEventManager() {
    service_context.CloseEvent(purchased_event);
}

Result IPurchaseEventManager::SetDefaultDeliveryTarget(
    ClientProcessId process_id, InBuffer<BufferAttr_HipcMapAlias> in_buffer) {
    LOG_WARNING(Service_AOC, "(STUBBED) called, process_id={}", process_id.pid);

    R_SUCCEED();
}

Result IPurchaseEventManager::SetDeliveryTarget(u64 unknown,
                                                InBuffer<BufferAttr_HipcMapAlias> in_buffer) {
    LOG_WARNING(Service_AOC, "(STUBBED) called, unknown={}", unknown);

    R_SUCCEED();
}

Result IPurchaseEventManager::GetPurchasedEvent(OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Service_AOC, "called");

    *out_event = &purchased_event->GetReadableEvent();

    R_SUCCEED();
}

Result IPurchaseEventManager::PopPurchasedProductInfo() {
    LOG_DEBUG(Service_AOC, "(STUBBED) called");

    R_RETURN(ResultNoPurchasedProductInfoAvailable);
}

Result IPurchaseEventManager::PopPurchasedProductInfoWithUid() {
    LOG_DEBUG(Service_AOC, "(STUBBED) called");

    R_RETURN(ResultNoPurchasedProductInfoAvailable);
}

} // namespace Service::AOC
