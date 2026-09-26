// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/caps/caps_types.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::Capture {

class IScreenShotService final : public ServiceFramework<IScreenShotService> {
public:
    explicit IScreenShotService(Core::System& system_, std::shared_ptr<AlbumManager> album_manager);
    ~IScreenShotService() override;

private:
    Result SaveScreenShotEx0(
        Out<ApplicationAlbumEntry> out_entry, const ScreenShotAttribute& attribute,
        AlbumReportOption report_option, ClientAppletResourceUserId aruid,
        InBuffer<BufferAttr_HipcMapTransferAllowsNonSecure | BufferAttr_HipcMapAlias>
            image_data_buffer);

    Result SaveEditedScreenShotEx1(
        Out<ApplicationAlbumEntry> out_entry, const ScreenShotAttribute& attribute, u64 width,
        u64 height, u64 thumbnail_width, u64 thumbnail_height, const AlbumFileId& file_id,
        const InLargeData<std::array<u8, 0x400>, BufferAttr_HipcMapAlias> application_data_buffer,
        const InBuffer<BufferAttr_HipcMapTransferAllowsNonSecure | BufferAttr_HipcMapAlias>
            image_data_buffer,
        const InBuffer<BufferAttr_HipcMapTransferAllowsNonSecure | BufferAttr_HipcMapAlias>
            thumbnail_image_data_buffer);

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{201, nullptr, "SaveScreenShot"},
        FunctionInfo{202, nullptr, "SaveEditedScreenShot"},
        FunctionInfo{203, C<&IScreenShotService::SaveScreenShotEx0>, "SaveScreenShotEx0"},
        FunctionInfo{204, nullptr, "SaveEditedScreenShotEx0"},
        FunctionInfo{206, C<&IScreenShotService::SaveEditedScreenShotEx1>, "SaveEditedScreenShotEx1"},
        FunctionInfo{208, nullptr, "SaveScreenShotOfMovieEx1"},
        FunctionInfo{1000, nullptr, "Unknown1000"}
    );
    std::shared_ptr<AlbumManager> manager;
};

} // namespace Service::Capture
