// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

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
class AlbumManager;

class IAlbumAccessorService final : public ServiceFramework<IAlbumAccessorService> {
public:
    explicit IAlbumAccessorService(Core::System& system_,
                                   std::shared_ptr<AlbumManager> album_manager);
    ~IAlbumAccessorService() override;

private:
    Result GetAlbumFileList(Out<u64> out_count, AlbumStorage storage,
                            OutArray<AlbumEntry, BufferAttr_HipcMapAlias> out_entries);

    Result DeleteAlbumFile(AlbumFileId file_id);

    Result IsAlbumMounted(Out<bool> out_is_mounted, AlbumStorage storage);

    Result Unknown18(
        Out<u32> out_buffer_size,
        OutArray<u8, BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure>
            out_buffer);

    Result GetAlbumFileListEx0(Out<u64> out_entries_size, AlbumStorage storage, u8 flags,
                               OutArray<AlbumEntry, BufferAttr_HipcMapAlias> out_entries);

    Result GetAutoSavingStorage(Out<bool> out_is_autosaving);

    Result LoadAlbumScreenShotImageEx1(
        const AlbumFileId& file_id, const ScreenShotDecodeOption& decoder_options,
        OutLargeData<LoadAlbumScreenShotImageOutput, BufferAttr_HipcMapAlias> out_image_output,
        OutArray<u8, BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_image,
        OutArray<u8, BufferAttr_HipcMapAlias> out_buffer);

    Result LoadAlbumScreenShotThumbnailImageEx1(
        const AlbumFileId& file_id, const ScreenShotDecodeOption& decoder_options,
        OutLargeData<LoadAlbumScreenShotImageOutput, BufferAttr_HipcMapAlias> out_image_output,
        OutArray<u8, BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_image,
        OutArray<u8, BufferAttr_HipcMapAlias> out_buffer);

    Result TranslateResult(Result in_result);

    Result GetAlbumAccessResultForDebug(Out<Result> out_result);

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, nullptr, "GetAlbumFileCount"},
        FunctionInfo{1, C<&IAlbumAccessorService::GetAlbumFileList>, "GetAlbumFileList"},
        FunctionInfo{2, nullptr, "LoadAlbumFile"},
        FunctionInfo{3, C<&IAlbumAccessorService::DeleteAlbumFile>, "DeleteAlbumFile"},
        FunctionInfo{4, nullptr, "StorageCopyAlbumFile"},
        FunctionInfo{5, C<&IAlbumAccessorService::IsAlbumMounted>, "IsAlbumMounted"},
        FunctionInfo{6, nullptr, "GetAlbumUsage"},
        FunctionInfo{7, nullptr, "GetAlbumFileSize"},
        FunctionInfo{8, nullptr, "LoadAlbumFileThumbnail"},
        FunctionInfo{9, nullptr, "LoadAlbumScreenShotImage"},
        FunctionInfo{10, nullptr, "LoadAlbumScreenShotThumbnailImage"},
        FunctionInfo{11, nullptr, "GetAlbumEntryFromApplicationAlbumEntry"},
        FunctionInfo{12, nullptr, "LoadAlbumScreenShotImageEx"},
        FunctionInfo{13, nullptr, "LoadAlbumScreenShotThumbnailImageEx"},
        FunctionInfo{14, nullptr, "LoadAlbumScreenShotImageEx0"},
        FunctionInfo{15, nullptr, "GetAlbumUsage3"},
        FunctionInfo{16, nullptr, "GetAlbumMountResult"},
        FunctionInfo{17, nullptr, "GetAlbumUsage16"},
        FunctionInfo{18, C<&IAlbumAccessorService::Unknown18>, "Unknown18"},
        FunctionInfo{19, nullptr, "Unknown19"},
        FunctionInfo{100, nullptr, "GetAlbumFileCountEx0"},
        FunctionInfo{101, C<&IAlbumAccessorService::GetAlbumFileListEx0>, "GetAlbumFileListEx0"},
        FunctionInfo{202, nullptr, "SaveEditedScreenShot"},
        FunctionInfo{301, nullptr, "GetLastThumbnail"},
        FunctionInfo{302, nullptr, "GetLastOverlayMovieThumbnail"},
        FunctionInfo{401,  C<&IAlbumAccessorService::GetAutoSavingStorage>, "GetAutoSavingStorage"},
        FunctionInfo{501, nullptr, "GetRequiredStorageSpaceSizeToCopyAll"},
        FunctionInfo{1001, nullptr, "LoadAlbumScreenShotThumbnailImageEx0"},
        FunctionInfo{1002, C<&IAlbumAccessorService::LoadAlbumScreenShotImageEx1>, "LoadAlbumScreenShotImageEx1"},
        FunctionInfo{1003, C<&IAlbumAccessorService::LoadAlbumScreenShotThumbnailImageEx1>, "LoadAlbumScreenShotThumbnailImageEx1"},
        FunctionInfo{8001, nullptr, "ForceAlbumUnmounted"},
        FunctionInfo{8002, nullptr, "ResetAlbumMountStatus"},
        FunctionInfo{8011, nullptr, "RefreshAlbumCache"},
        FunctionInfo{8012, nullptr, "GetAlbumCache"},
        FunctionInfo{8013, nullptr, "GetAlbumCacheEx"},
        FunctionInfo{8021, nullptr, "GetAlbumEntryFromApplicationAlbumEntryAruid"},
        FunctionInfo{10011, nullptr, "SetInternalErrorConversionEnabled"},
        FunctionInfo{50000, nullptr, "LoadMakerNoteInfoForDebug"},
        FunctionInfo{50011, C<&IAlbumAccessorService::GetAlbumAccessResultForDebug>, "GetAlbumAccessResultForDebug"},
        FunctionInfo{60002, nullptr, "OpenAccessorSession"}
    );
    std::shared_ptr<AlbumManager> manager = nullptr;
};

} // namespace Service::Capture
