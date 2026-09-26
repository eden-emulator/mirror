// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/hex_util.h"
#include "common/string_util.h"
#include "core/core.h"
#include "core/file_sys/errors.h"
#include "core/hle/service/bcat/backend/backend.h"
#include "core/hle/service/bcat/bcat_result.h"
#include "core/hle/service/bcat/bcat_service.h"
#include "core/hle/service/bcat/bcat_util.h"
#include "core/hle/service/bcat/delivery_cache_progress_service.h"
#include "core/hle/service/bcat/delivery_cache_storage_service.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::BCAT {

static u64 GetCurrentBuildID(const Core::System::CurrentBuildProcessID& id) {
    u64 out{};
    std::memcpy(&out, id.data(), sizeof(u64));
    return out;
}

std::optional<ServiceFrameworkBase::FunctionInfoBase> IBcatService::FindRequest(u32 key) {
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{10100, D<&IBcatService::RequestSyncDeliveryCache>, "RequestSyncDeliveryCache"},
        FunctionInfo{10101, D<&IBcatService::RequestSyncDeliveryCacheWithDirectoryName>, "RequestSyncDeliveryCacheWithDirectoryName"},
        FunctionInfo{10200, nullptr, "CancelSyncDeliveryCacheRequest"},
        FunctionInfo{20100, nullptr, "RequestSyncDeliveryCacheWithApplicationId"},
        FunctionInfo{20101, nullptr, "RequestSyncDeliveryCacheWithApplicationIdAndDirectoryName"},
        FunctionInfo{20300, nullptr, "GetDeliveryCacheStorageUpdateNotifier"},
        FunctionInfo{20301, nullptr, "RequestSuspendDeliveryTask"},
        FunctionInfo{20400, nullptr, "RegisterSystemApplicationDeliveryTask"},
        FunctionInfo{20401, nullptr, "UnregisterSystemApplicationDeliveryTask"},
        FunctionInfo{20410, nullptr, "SetSystemApplicationDeliveryTaskTimer"},
        FunctionInfo{30100, D<&IBcatService::SetPassphrase>, "SetPassphrase"},
        FunctionInfo{30101, nullptr, "Unknown30101"}, //2.0.0-2.3.0
        FunctionInfo{30102, nullptr, "Unknown30102"}, //2.0.0-2.3.0
        FunctionInfo{30200, nullptr, "RegisterBackgroundDeliveryTask"},
        FunctionInfo{30201, nullptr, "UnregisterBackgroundDeliveryTask"},
        FunctionInfo{30202, nullptr, "BlockDeliveryTask"},
        FunctionInfo{30203, nullptr, "UnblockDeliveryTask"},
        FunctionInfo{30210, nullptr, "SetDeliveryTaskTimer"},
        FunctionInfo{30300, D<&IBcatService::RegisterSystemApplicationDeliveryTasks>, "RegisterSystemApplicationDeliveryTasks"},
        FunctionInfo{90100, nullptr, "GetDeliveryTaskList"},
        FunctionInfo{90101, nullptr, "GetDeliveryTaskListForSystem"}, //11.0.0+
        FunctionInfo{90200, nullptr, "GetDeliveryList"},
        FunctionInfo{90201, D<&IBcatService::ClearDeliveryCacheStorage>, "ClearDeliveryCacheStorage"},
        FunctionInfo{90202, nullptr, "ClearDeliveryTaskSubscriptionStatus"},
        FunctionInfo{90300, nullptr, "GetPushNotificationLog"},
        FunctionInfo{90301, nullptr, "GetDeliveryCacheStorageUsage"} //11.0.0+
    );
    return HandlerTableGenerateWithFind(key, functions);
}

IBcatService::IBcatService(Core::System& system_, BcatBackend& backend_, u64 program_id_)
    : ServiceFramework{system_, "IBcatService"}, backend{backend_}, program_id{program_id_}
    , progress{{
        ProgressServiceBackend{system_, "Normal"},
        ProgressServiceBackend{system_, "Directory"},
    }}
{}

IBcatService::~IBcatService() = default;

Result IBcatService::RequestSyncDeliveryCache(
    OutInterface<IDeliveryCacheProgressService> out_interface) {
    LOG_DEBUG(Service_BCAT, "called");

    auto& progress_backend{GetProgressBackend(SyncType::Normal)};
    backend.Synchronize(system.Kernel(), {program_id, GetCurrentBuildID(system.GetApplicationProcessBuildID())},
                        GetProgressBackend(SyncType::Normal));

    *out_interface = std::make_shared<IDeliveryCacheProgressService>(
        system, progress_backend.GetEvent(), progress_backend.GetImpl());
    R_SUCCEED();
}

Result IBcatService::RequestSyncDeliveryCacheWithDirectoryName(
    const DirectoryName& name_raw, OutInterface<IDeliveryCacheProgressService> out_interface) {
    const auto name = Common::StringFromFixedZeroTerminatedBuffer(name_raw.data(), name_raw.size());

    LOG_DEBUG(Service_BCAT, "called, name={}", name);

    auto& progress_backend{GetProgressBackend(SyncType::Directory)};
    backend.SynchronizeDirectory(system.Kernel(), {program_id, GetCurrentBuildID(system.GetApplicationProcessBuildID())},
        name, progress_backend);

    *out_interface = std::make_shared<IDeliveryCacheProgressService>(
        system, progress_backend.GetEvent(), progress_backend.GetImpl());
    R_SUCCEED();
}

Result IBcatService::SetPassphrase(u64 application_id,
                                   InBuffer<BufferAttr_HipcPointer> passphrase_buffer) {
    LOG_DEBUG(Service_BCAT, "called, application_id={:016X}, passphrase={}", application_id,
              Common::HexToString(passphrase_buffer));

    R_UNLESS(application_id != 0, ResultInvalidArgument);
    R_UNLESS(passphrase_buffer.size() <= 0x40, ResultInvalidArgument);

    Passphrase passphrase{};
    std::memcpy(passphrase.data(), passphrase_buffer.data(),
                (std::min)(passphrase.size(), passphrase_buffer.size()));

    backend.SetPassphrase(system.Kernel(), application_id, passphrase);
    R_SUCCEED();
}

Result IBcatService::RegisterSystemApplicationDeliveryTasks() {
    LOG_WARNING(Service_BCAT, "(STUBBED) called");
    R_SUCCEED();
}

Result IBcatService::ClearDeliveryCacheStorage(u64 application_id) {
    LOG_DEBUG(Service_BCAT, "called, title_id={:016X}", application_id);

    R_UNLESS(application_id != 0, ResultInvalidArgument);
    R_UNLESS(backend.Clear(system.Kernel(), application_id), FileSys::ResultPermissionDenied);
    R_SUCCEED();
}

ProgressServiceBackend& IBcatService::GetProgressBackend(SyncType type) {
    return progress.at(static_cast<size_t>(type));
}

const ProgressServiceBackend& IBcatService::GetProgressBackend(SyncType type) const {
    return progress.at(static_cast<size_t>(type));
}

} // namespace Service::BCAT
