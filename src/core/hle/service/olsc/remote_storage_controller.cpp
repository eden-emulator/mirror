// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/olsc/remote_storage_controller.h"

namespace Service::OLSC {

IRemoteStorageController::IRemoteStorageController(Core::System& system_)
    : ServiceFramework{system_, "IRemoteStorageController"} {
}

IRemoteStorageController::~IRemoteStorageController() = default;

Result IRemoteStorageController::GetSecondarySave(Out<bool> out_has_secondary_save,
                                                  Out<std::array<u64, 3>> out_unknown,
                                                  u64 application_id) {
    LOG_ERROR(Service_OLSC, "(STUBBED) called, application_id={:016X}", application_id);
    *out_has_secondary_save = false;
    *out_unknown = {};
    R_SUCCEED();
}

Result IRemoteStorageController::GetDataNewnessByApplicationId(Out<u8> out_newness,
                                                              u64 application_id) {
    LOG_WARNING(Service_OLSC, "(STUBBED) called, application_id={:016X}", application_id);
    *out_newness = 0;
    R_SUCCEED();
}

Result IRemoteStorageController::GetDataInfo(Out<std::array<u8, 0x38>> out_data, u64 application_id) {
    LOG_WARNING(Service_OLSC, "(STUBBED) called, application_id={:016X}", application_id);
    out_data->fill(0);
    R_SUCCEED();
}

} // namespace Service::OLSC
