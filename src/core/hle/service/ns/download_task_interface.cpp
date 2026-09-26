// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ns/download_task_interface.h"

namespace Service::NS {

IDownloadTaskInterface::IDownloadTaskInterface(Core::System& system_)
    : ServiceFramework{system_, "IDownloadTaskInterface"} {
}

IDownloadTaskInterface::~IDownloadTaskInterface() = default;

Result IDownloadTaskInterface::EnableAutoCommit() {
    LOG_WARNING(Service_NS, "(STUBBED) called");
    R_SUCCEED();
}
Result IDownloadTaskInterface::DisableAutoCommit() {
    LOG_WARNING(Service_NS, "(STUBBED) called");
    R_SUCCEED();
}

} // namespace Service::NS
