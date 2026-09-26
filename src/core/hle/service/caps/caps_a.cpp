// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/logging.h"
#include "core/hle/service/caps/caps_a.h"
#include "core/hle/service/caps/caps_manager.h"
#include "core/hle/service/caps/caps_result.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ipc_helpers.h"

namespace Service::Capture {

ServiceFrameworkBase::FunctionInfoBase const* IAlbumAccessorService::FindRequest(u32 key) {
    static const auto functions = CreateStaticMap(
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
    return HandlerTableGenerateWithFind(key, functions);
}

IAlbumAccessorService::IAlbumAccessorService(Core::System& system_,
                                             std::shared_ptr<AlbumManager> album_manager)
    : ServiceFramework{system_, "caps:a"}, manager{album_manager} {
}

IAlbumAccessorService::~IAlbumAccessorService() = default;

Result IAlbumAccessorService::GetAlbumFileList(
    Out<u64> out_count, AlbumStorage storage,
    OutArray<AlbumEntry, BufferAttr_HipcMapAlias> out_entries) {
    LOG_INFO(Service_Capture, "called, storage={}", storage);

    const Result result = manager->GetAlbumFileList(out_entries, *out_count, storage, 0);
    R_RETURN(TranslateResult(result));
}

Result IAlbumAccessorService::DeleteAlbumFile(AlbumFileId file_id) {
    LOG_INFO(Service_Capture, "called, application_id={:#0x}, storage={}, type={}",
             file_id.application_id, file_id.storage, file_id.type);

    const Result result = manager->DeleteAlbumFile(file_id);
    R_RETURN(TranslateResult(result));
}

Result IAlbumAccessorService::IsAlbumMounted(Out<bool> out_is_mounted, AlbumStorage storage) {
    LOG_INFO(Service_Capture, "called, storage={}", storage);

    const Result result = manager->IsAlbumMounted(storage);
    *out_is_mounted = result.IsSuccess();
    R_RETURN(TranslateResult(result));
}

Result IAlbumAccessorService::Unknown18(
    Out<u32> out_buffer_size,
    OutArray<u8, BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_buffer) {
    LOG_WARNING(Service_Capture, "(STUBBED) called");
    *out_buffer_size = 0;
    R_SUCCEED();
}

Result IAlbumAccessorService::GetAlbumFileListEx0(
    Out<u64> out_entries_size, AlbumStorage storage, u8 flags,
    OutArray<AlbumEntry, BufferAttr_HipcMapAlias> out_entries) {
    LOG_INFO(Service_Capture, "called, storage={}, flags={}", storage, flags);

    const Result result = manager->GetAlbumFileList(out_entries, *out_entries_size, storage, flags);
    R_RETURN(TranslateResult(result));
}

Result IAlbumAccessorService::GetAutoSavingStorage(Out<bool> out_is_autosaving) {
    LOG_WARNING(Service_Capture, "(STUBBED) called");

    const Result result = manager->GetAutoSavingStorage(*out_is_autosaving);
    R_RETURN(TranslateResult(result));
}

Result IAlbumAccessorService::LoadAlbumScreenShotImageEx1(
    const AlbumFileId& file_id, const ScreenShotDecodeOption& decoder_options,
    OutLargeData<LoadAlbumScreenShotImageOutput, BufferAttr_HipcMapAlias> out_image_output,
    OutArray<u8, BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_image,
    OutArray<u8, BufferAttr_HipcMapAlias> out_buffer) {
    LOG_INFO(Service_Capture, "called, application_id={:#0x}, storage={}, type={}, flags={}",
             file_id.application_id, file_id.storage, file_id.type, decoder_options.flags);

    const Result result =
        manager->LoadAlbumScreenShotImage(*out_image_output, out_image, file_id, decoder_options);
    R_RETURN(TranslateResult(result));
}

Result IAlbumAccessorService::LoadAlbumScreenShotThumbnailImageEx1(
    const AlbumFileId& file_id, const ScreenShotDecodeOption& decoder_options,
    OutLargeData<LoadAlbumScreenShotImageOutput, BufferAttr_HipcMapAlias> out_image_output,
    OutArray<u8, BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_image,
    OutArray<u8, BufferAttr_HipcMapAlias> out_buffer) {
    LOG_INFO(Service_Capture, "called, application_id={:#0x}, storage={}, type={}, flags={}",
             file_id.application_id, file_id.storage, file_id.type, decoder_options.flags);

    const Result result = manager->LoadAlbumScreenShotThumbnail(*out_image_output, out_image,
                                                                file_id, decoder_options);
    R_RETURN(TranslateResult(result));
}

Result IAlbumAccessorService::TranslateResult(Result in_result) {
    if (in_result.IsSuccess()) {
        return in_result;
    }

    if ((in_result.raw & 0x3801ff) == ResultUnknown1024.raw) {
        if (in_result.GetDescription() - 0x514 < 100) {
            return ResultInvalidFileData;
        }
        if (in_result.GetDescription() - 0x5dc < 100) {
            return ResultInvalidFileData;
        }

        if (in_result.GetDescription() - 0x578 < 100) {
            if (in_result == ResultFileCountLimit) {
                return ResultUnknown22;
            }
            return ResultUnknown25;
        }

        if (in_result.raw < ResultUnknown1801.raw) {
            if (in_result == ResultUnknown1202) {
                return ResultUnknown810;
            }
            if (in_result == ResultUnknown1203) {
                return ResultUnknown810;
            }
            if (in_result == ResultUnknown1701) {
                return ResultUnknown5;
            }
        } else if (in_result.raw < ResultUnknown1803.raw) {
            if (in_result == ResultUnknown1801) {
                return ResultUnknown5;
            }
            if (in_result == ResultUnknown1802) {
                return ResultUnknown6;
            }
        } else {
            if (in_result == ResultUnknown1803) {
                return ResultUnknown7;
            }
            if (in_result == ResultUnknown1804) {
                return ResultOutOfRange;
            }
        }
        return ResultUnknown1024;
    }

    if (in_result.GetModule() == ErrorModule::FS) {
        if ((in_result.GetDescription() >> 0xc < 0x7d) ||
            (in_result.GetDescription() - 1000 < 2000) ||
            (((in_result.GetDescription() - 3000) >> 3) < 0x271)) {
            // TODO: Translate FS error
            return in_result;
        }
    }

    return in_result;
}


Result IAlbumAccessorService::GetAlbumAccessResultForDebug(Out<Result> out_result) {
    LOG_WARNING(Service_Capture, "(STUBBED) called");
    *out_result = ResultSuccess;
    R_SUCCEED();
}

} // namespace Service::Capture
