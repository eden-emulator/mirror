// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::Capture {
class AlbumManager;

class IAlbumApplicationService final : public ServiceFramework<IAlbumApplicationService> {
public:
    explicit IAlbumApplicationService(Core::System& system_,
                                      std::shared_ptr<AlbumManager> album_manager);
    ~IAlbumApplicationService() override;

private:
    Result SetShimLibraryVersion(ShimLibraryVersion library_version,
                                 ClientAppletResourceUserId aruid);

    Result GetAlbumFileList0AafeAruidDeprecated(
        Out<u64> out_entries_count, ContentType content_type, s64 start_posix_time,
        s64 end_posix_time, ClientAppletResourceUserId aruid,
        OutArray<ApplicationAlbumFileEntry, BufferAttr_HipcMapAlias> out_entries);

    Result GetAlbumFileList3AaeAruid(
        Out<u64> out_entries_count, ContentType content_type, AlbumFileDateTime start_date_time,
        AlbumFileDateTime end_date_time, ClientAppletResourceUserId aruid,
        OutArray<ApplicationAlbumEntry, BufferAttr_HipcMapAlias> out_entries);

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{32, C<&IAlbumApplicationService::SetShimLibraryVersion>, "SetShimLibraryVersion"},
        FunctionInfo{102, C<&IAlbumApplicationService::GetAlbumFileList0AafeAruidDeprecated>, "GetAlbumFileList0AafeAruidDeprecated"},
        FunctionInfo{103, nullptr, "DeleteAlbumFileByAruid"},
        FunctionInfo{104, nullptr, "GetAlbumFileSizeByAruid"},
        FunctionInfo{105, nullptr, "DeleteAlbumFileByAruidForDebug"},
        FunctionInfo{110, nullptr, "LoadAlbumScreenShotImageByAruid"},
        FunctionInfo{120, nullptr, "LoadAlbumScreenShotThumbnailImageByAruid"},
        FunctionInfo{130, nullptr, "PrecheckToCreateContentsByAruid"},
        FunctionInfo{140, nullptr, "GetAlbumFileList1AafeAruidDeprecated"},
        FunctionInfo{141, nullptr, "GetAlbumFileList2AafeUidAruidDeprecated"},
        FunctionInfo{142, C<&IAlbumApplicationService::GetAlbumFileList3AaeAruid>, "GetAlbumFileList3AaeAruid"},
        FunctionInfo{143, nullptr, "GetAlbumFileList4AaeUidAruid"},
        FunctionInfo{144, nullptr, "GetAllAlbumFileList3AaeAruid"},
        FunctionInfo{60002, nullptr, "OpenAccessorSessionForApplication"}
    );
    std::shared_ptr<AlbumManager> manager = nullptr;
};

} // namespace Service::Capture
