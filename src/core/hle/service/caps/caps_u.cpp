// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/logging.h"
#include "core/hle/service/caps/caps_manager.h"
#include "core/hle/service/caps/caps_types.h"
#include "core/hle/service/caps/caps_u.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ipc_helpers.h"

namespace Service::Capture {

IAlbumApplicationService::IAlbumApplicationService(Core::System& system_,
                                                   std::shared_ptr<AlbumManager> album_manager)
    : ServiceFramework{system_, "caps:u"}, manager{album_manager} {
}

IAlbumApplicationService::~IAlbumApplicationService() = default;

Result IAlbumApplicationService::SetShimLibraryVersion(ShimLibraryVersion library_version,
                                                       ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_Capture, "(STUBBED) called. library_version={}, applet_resource_user_id={}",
                library_version, aruid.pid);
    R_SUCCEED();
}

Result IAlbumApplicationService::GetAlbumFileList0AafeAruidDeprecated(
    Out<u64> out_entries_count, ContentType content_type, s64 start_posix_time, s64 end_posix_time,
    ClientAppletResourceUserId aruid,
    OutArray<ApplicationAlbumFileEntry, BufferAttr_HipcMapAlias> out_entries) {
    LOG_WARNING(Service_Capture,
                "(STUBBED) called. content_type={}, start_posix_time={}, end_posix_time={}, "
                "applet_resource_user_id={}",
                content_type, start_posix_time, end_posix_time, aruid.pid);

    R_TRY(manager->IsAlbumMounted(AlbumStorage::Sd));
    R_RETURN(manager->GetAlbumFileList(out_entries, *out_entries_count, content_type,
                                       start_posix_time, end_posix_time, aruid.pid));
}

Result IAlbumApplicationService::GetAlbumFileList3AaeAruid(
    Out<u64> out_entries_count, ContentType content_type, AlbumFileDateTime start_date_time,
    AlbumFileDateTime end_date_time, ClientAppletResourceUserId aruid,
    OutArray<ApplicationAlbumEntry, BufferAttr_HipcMapAlias> out_entries) {
    LOG_WARNING(Service_Capture,
                "(STUBBED) called. content_type={}, start_date={}/{}/{}, "
                "end_date={}/{}/{}, applet_resource_user_id={}",
                content_type, start_date_time.year, start_date_time.month, start_date_time.day,
                end_date_time.year, end_date_time.month, end_date_time.day, aruid.pid);

    R_TRY(manager->IsAlbumMounted(AlbumStorage::Sd));
    R_RETURN(manager->GetAlbumFileList(out_entries, *out_entries_count, content_type,
                                       start_date_time, end_date_time, aruid.pid));
}

} // namespace Service::Capture
