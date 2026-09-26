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
enum class ShimLibraryVersion : u64;

class IAlbumControlService final : public ServiceFramework<IAlbumControlService> {
public:
    explicit IAlbumControlService(Core::System& system_,
                                  std::shared_ptr<AlbumManager> album_manager);
    ~IAlbumControlService() override;

private:
    Result SetShimLibraryVersion(ShimLibraryVersion library_version,
                                 ClientAppletResourceUserId aruid);

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
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
    std::shared_ptr<AlbumManager> manager = nullptr;
};

} // namespace Service::Capture
