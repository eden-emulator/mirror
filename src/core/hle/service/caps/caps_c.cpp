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

ServiceFrameworkBase::FunctionInfoBase const* IAlbumControlService::FindRequest(u32 key) {
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{1, nullptr, "CaptureRawImage"},
        FunctionInfo{2, nullptr, "CaptureRawImageWithTimeout"},
        FunctionInfo{33, C<&IAlbumControlService::SetShimLibraryVersion>, "SetShimLibraryVersion"},
        FunctionInfo{1001, nullptr, "RequestTakingScreenShot"},
        FunctionInfo{1002, nullptr, "RequestTakingScreenShotWithTimeout"},
        FunctionInfo{1011, nullptr, "NotifyTakingScreenShotRefused"},
        FunctionInfo{2001, nullptr, "NotifyAlbumStorageIsAvailable"},
        FunctionInfo{2002, nullptr, "NotifyAlbumStorageIsUnavailable"},
        FunctionInfo{2011, nullptr, "RegisterAppletResourceUserId"},
        FunctionInfo{2012, nullptr, "UnregisterAppletResourceUserId"},
        FunctionInfo{2013, nullptr, "GetApplicationIdFromAruid"},
        FunctionInfo{2014, nullptr, "CheckApplicationIdRegistered"},
        FunctionInfo{2101, nullptr, "GenerateCurrentAlbumFileId"},
        FunctionInfo{2102, nullptr, "GenerateApplicationAlbumEntry"},
        FunctionInfo{2201, nullptr, "SaveAlbumScreenShotFile"},
        FunctionInfo{2202, nullptr, "SaveAlbumScreenShotFileEx"},
        FunctionInfo{2301, nullptr, "SetOverlayScreenShotThumbnailData"},
        FunctionInfo{2302, nullptr, "SetOverlayMovieThumbnailData"},
        FunctionInfo{60001, nullptr, "OpenControlSession"}
    );
    return HandlerTableGenerateWithFind(key, functions);
}

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
