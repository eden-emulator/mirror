// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/logging.h"
#include "core/hle/service/caps/caps_c.h"
#include "core/hle/service/caps/caps_manager.h"
#include "core/hle/service/caps/caps_result.h"
#include "core/hle/service/caps/caps_types.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ipc_helpers.h"

namespace Service::Capture {

IAlbumControlService::IAlbumControlService(Core::System& system_,
                                           std::shared_ptr<AlbumManager> album_manager)
    : ServiceFramework{system_, "caps:c"}, manager{album_manager} {
}

IAlbumControlService::~IAlbumControlService() = default;

Result IAlbumControlService::SetShimLibraryVersion(ShimLibraryVersion library_version,
                                                   ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_Capture, "(STUBBED) called. library_version={}, applet_resource_user_id={}",
                library_version, aruid.pid);
    R_SUCCEED();
}

} // namespace Service::Capture
