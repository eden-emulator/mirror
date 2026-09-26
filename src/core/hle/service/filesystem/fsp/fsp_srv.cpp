// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cinttypes>
#include <cstring>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "common/assert.h"
#include "common/common_types.h"
#include "common/hex_util.h"
#include "common/logging.h"
#include "common/settings.h"
#include "common/string_util.h"
#include "core/core.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/errors.h"
#include "core/file_sys/fs_directory.h"
#include "core/file_sys/fs_filesystem.h"
#include "core/file_sys/nca_metadata.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/romfs.h"
#include "core/file_sys/romfs_factory.h"
#include "core/file_sys/savedata_factory.h"
#include "core/file_sys/system_archive/system_archive.h"
#include "core/file_sys/vfs/vfs.h"
#include "core/hle/result.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/hle/service/filesystem/fsp/fs_i_filesystem.h"
#include "core/hle/service/filesystem/fsp/fs_i_multi_commit_manager.h"
#include "core/hle/service/filesystem/fsp/fs_i_save_data_info_reader.h"
#include "core/hle/service/filesystem/fsp/fs_i_storage.h"
#include "core/hle/service/filesystem/fsp/fsp_srv.h"
#include "core/hle/service/filesystem/fsp/save_data_transfer_prohibiter.h"
#include "core/hle/service/filesystem/romfs_controller.h"
#include "core/hle/service/filesystem/save_data_controller.h"
#include "core/hle/service/hle_ipc.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/loader/loader.h"
#include "core/reporter.h"

namespace Service::FileSystem {

FSP_SRV::FSP_SRV(Core::System& system_)
    : ServiceFramework{system_, "fsp-srv"}, fsc{system.GetFileSystemController()},
      content_provider{system.GetContentProvider()}, reporter{system.GetReporter()} {
    // clang-format off
    static const FunctionInfo functions[] = {
        FunctionInfo{0, nullptr, "OpenFileSystem"},
        FunctionInfo{1, D<&FSP_SRV::SetCurrentProcess>, "SetCurrentProcess"},
        FunctionInfo{2, D<&FSP_SRV::OpenDataFileSystemByCurrentProcess>, "OpenDataFileSystemByCurrentProcess"},
        FunctionInfo{7, D<&FSP_SRV::OpenFileSystemWithPatch>, "OpenFileSystemWithPatch"},
        FunctionInfo{8, nullptr, "OpenFileSystemWithId"},
        FunctionInfo{9, nullptr, "OpenDataFileSystemByApplicationId"},
        FunctionInfo{11, nullptr, "OpenBisFileSystem"},
        FunctionInfo{12, nullptr, "OpenBisStorage"},
        FunctionInfo{13, nullptr, "InvalidateBisCache"},
        FunctionInfo{17, nullptr, "OpenHostFileSystem"},
        FunctionInfo{18, D<&FSP_SRV::OpenSdCardFileSystem>, "OpenSdCardFileSystem"},
        FunctionInfo{19, nullptr, "FormatSdCardFileSystem"},
        FunctionInfo{21, nullptr, "DeleteSaveDataFileSystem"},
        FunctionInfo{22, D<&FSP_SRV::CreateSaveDataFileSystem>, "CreateSaveDataFileSystem"},
        FunctionInfo{23, D<&FSP_SRV::CreateSaveDataFileSystemBySystemSaveDataId>, "CreateSaveDataFileSystemBySystemSaveDataId"},
        FunctionInfo{24, nullptr, "RegisterSaveDataFileSystemAtomicDeletion"},
        FunctionInfo{25, nullptr, "DeleteSaveDataFileSystemBySaveDataSpaceId"},
        FunctionInfo{26, nullptr, "FormatSdCardDryRun"},
        FunctionInfo{27, D<&FSP_SRV::IsExFatSupported>, "IsExFatSupported"},
        FunctionInfo{28, nullptr, "DeleteSaveDataFileSystemBySaveDataAttribute"},
        FunctionInfo{30, nullptr, "OpenGameCardStorage"},
        FunctionInfo{31, nullptr, "OpenGameCardFileSystem"},
        FunctionInfo{32, D<&FSP_SRV::ExtendSaveDataFileSystem>, "ExtendSaveDataFileSystem"},
        FunctionInfo{33, nullptr, "DeleteCacheStorage"},
        FunctionInfo{34, D<&FSP_SRV::GetCacheStorageSize>, "GetCacheStorageSize"},
        FunctionInfo{35, nullptr, "CreateSaveDataFileSystemByHashSalt"},
        FunctionInfo{36, nullptr, "OpenHostFileSystemWithOption"},
        FunctionInfo{37, D<&FSP_SRV::CreateSaveDataFileSystemWithCreationInfo2>, "CreateSaveDataFileSystemWithCreationInfo2"},
        FunctionInfo{51, D<&FSP_SRV::OpenSaveDataFileSystem>, "OpenSaveDataFileSystem"},
        FunctionInfo{52, D<&FSP_SRV::OpenSaveDataFileSystemBySystemSaveDataId>, "OpenSaveDataFileSystemBySystemSaveDataId"},
        FunctionInfo{53, D<&FSP_SRV::OpenReadOnlySaveDataFileSystem>, "OpenReadOnlySaveDataFileSystem"},
        FunctionInfo{57, D<&FSP_SRV::ReadSaveDataFileSystemExtraDataBySaveDataSpaceId>, "ReadSaveDataFileSystemExtraDataBySaveDataSpaceId"},
        FunctionInfo{58, D<&FSP_SRV::ReadSaveDataFileSystemExtraData>, "ReadSaveDataFileSystemExtraData"},
        FunctionInfo{59, D<&FSP_SRV::WriteSaveDataFileSystemExtraData>, "WriteSaveDataFileSystemExtraData"},
        FunctionInfo{60, nullptr, "OpenSaveDataInfoReader"},
        FunctionInfo{61, D<&FSP_SRV::OpenSaveDataInfoReaderBySaveDataSpaceId>, "OpenSaveDataInfoReaderBySaveDataSpaceId"},
        FunctionInfo{62, D<&FSP_SRV::OpenSaveDataInfoReaderOnlyCacheStorage>, "OpenSaveDataInfoReaderOnlyCacheStorage"},
        FunctionInfo{64, nullptr, "OpenSaveDataInternalStorageFileSystem"},
        FunctionInfo{65, nullptr, "UpdateSaveDataMacForDebug"},
        FunctionInfo{66, nullptr, "WriteSaveDataFileSystemExtraData2"},
        FunctionInfo{67, D<&FSP_SRV::FindSaveDataWithFilter>, "FindSaveDataWithFilter"},
        FunctionInfo{68, nullptr, "OpenSaveDataInfoReaderBySaveDataFilter"},
        FunctionInfo{69, D<&FSP_SRV::ReadSaveDataFileSystemExtraDataBySaveDataAttribute>, "ReadSaveDataFileSystemExtraDataBySaveDataAttribute"},
        FunctionInfo{70, D<&FSP_SRV::WriteSaveDataFileSystemExtraDataWithMaskBySaveDataAttribute>, "WriteSaveDataFileSystemExtraDataWithMaskBySaveDataAttribute"},
        FunctionInfo{71, D<&FSP_SRV::ReadSaveDataFileSystemExtraDataWithMaskBySaveDataAttribute>, "ReadSaveDataFileSystemExtraDataWithMaskBySaveDataAttribute"},
        FunctionInfo{80, nullptr, "OpenSaveDataMetaFile"},
        FunctionInfo{81, nullptr, "OpenSaveDataTransferManager"},
        FunctionInfo{82, nullptr, "OpenSaveDataTransferManagerVersion2"},
        FunctionInfo{83, D<&FSP_SRV::OpenSaveDataTransferProhibiter>, "OpenSaveDataTransferProhibiter"},
        FunctionInfo{84, nullptr, "ListApplicationAccessibleSaveDataOwnerId"},
        FunctionInfo{85, nullptr, "OpenSaveDataTransferManagerForSaveDataRepair"},
        FunctionInfo{86, nullptr, "OpenSaveDataMover"},
        FunctionInfo{87, nullptr, "OpenSaveDataTransferManagerForRepair"},
        FunctionInfo{100, nullptr, "OpenImageDirectoryFileSystem"},
        FunctionInfo{101, nullptr, "OpenBaseFileSystem"},
        FunctionInfo{102, nullptr, "FormatBaseFileSystem"},
        FunctionInfo{110, nullptr, "OpenContentStorageFileSystem"},
        FunctionInfo{120, nullptr, "OpenCloudBackupWorkStorageFileSystem"},
        FunctionInfo{130, nullptr, "OpenCustomStorageFileSystem"},
        FunctionInfo{200, D<&FSP_SRV::OpenDataStorageByCurrentProcess>, "OpenDataStorageByCurrentProcess"},
        FunctionInfo{201, nullptr, "OpenDataStorageByProgramId"},
        FunctionInfo{202, D<&FSP_SRV::OpenDataStorageByDataId>, "OpenDataStorageByDataId"},
        FunctionInfo{203, D<&FSP_SRV::OpenPatchDataStorageByCurrentProcess>, "OpenPatchDataStorageByCurrentProcess"},
        FunctionInfo{204, nullptr, "OpenDataFileSystemByProgramIndex"},
        FunctionInfo{205, D<&FSP_SRV::OpenDataStorageWithProgramIndex>, "OpenDataStorageWithProgramIndex"},
        FunctionInfo{206, nullptr, "OpenDataStorageByPath"},
        FunctionInfo{210, D<&FSP_SRV::SetCurrentProcess>, "SetCurrentProcess"},
        FunctionInfo{400, nullptr, "OpenDeviceOperator"},
        FunctionInfo{500, nullptr, "OpenSdCardDetectionEventNotifier"},
        FunctionInfo{501, nullptr, "OpenGameCardDetectionEventNotifier"},
        FunctionInfo{510, nullptr, "OpenSystemDataUpdateEventNotifier"},
        FunctionInfo{511, nullptr, "NotifySystemDataUpdateEvent"},
        FunctionInfo{520, nullptr, "SimulateGameCardDetectionEvent"},
        FunctionInfo{600, nullptr, "SetCurrentPosixTime"},
        FunctionInfo{601, nullptr, "QuerySaveDataTotalSize"},
        FunctionInfo{602, nullptr, "VerifySaveDataFileSystem"},
        FunctionInfo{603, nullptr, "CorruptSaveDataFileSystem"},
        FunctionInfo{604, nullptr, "CreatePaddingFile"},
        FunctionInfo{605, nullptr, "DeleteAllPaddingFiles"},
        FunctionInfo{606, nullptr, "GetRightsId"},
        FunctionInfo{607, nullptr, "RegisterExternalKey"},
        FunctionInfo{608, nullptr, "UnregisterAllExternalKey"},
        FunctionInfo{609, nullptr, "GetRightsIdByPath"},
        FunctionInfo{610, nullptr, "GetRightsIdAndKeyGenerationByPath"},
        FunctionInfo{611, nullptr, "SetCurrentPosixTimeWithTimeDifference"},
        FunctionInfo{612, nullptr, "GetFreeSpaceSizeForSaveData"},
        FunctionInfo{613, nullptr, "VerifySaveDataFileSystemBySaveDataSpaceId"},
        FunctionInfo{614, nullptr, "CorruptSaveDataFileSystemBySaveDataSpaceId"},
        FunctionInfo{615, nullptr, "QuerySaveDataInternalStorageTotalSize"},
        FunctionInfo{616, nullptr, "GetSaveDataCommitId"},
        FunctionInfo{617, nullptr, "UnregisterExternalKey"},
        FunctionInfo{620, nullptr, "SetSdCardEncryptionSeed"},
        FunctionInfo{630, nullptr, "SetSdCardAccessibility"},
        FunctionInfo{631, D<&FSP_SRV::IsSdCardAccessible>, "IsSdCardAccessible"},
        FunctionInfo{640, nullptr, "IsSignedSystemPartitionOnSdCardValid"},
        FunctionInfo{700, nullptr, "OpenAccessFailureResolver"},
        FunctionInfo{701, nullptr, "GetAccessFailureDetectionEvent"},
        FunctionInfo{702, nullptr, "IsAccessFailureDetected"},
        FunctionInfo{710, nullptr, "ResolveAccessFailure"},
        FunctionInfo{720, nullptr, "AbandonAccessFailure"},
        FunctionInfo{800, nullptr, "GetAndClearFileSystemProxyErrorInfo"},
        FunctionInfo{810, nullptr, "RegisterProgramIndexMapInfo"},
        FunctionInfo{820, nullptr, "GetContentStorageInfoIndex"},
        FunctionInfo{830, nullptr, "EncryptStreamPlaySaveData"},
        FunctionInfo{831, nullptr, "DecryptStreamPlaySaveData"},
        FunctionInfo{1000, nullptr, "SetBisRootForHost"},
        FunctionInfo{1001, nullptr, "SetSaveDataSize"},
        FunctionInfo{1002, nullptr, "SetSaveDataRootPath"},
        FunctionInfo{1003, D<&FSP_SRV::DisableAutoSaveDataCreation>, "DisableAutoSaveDataCreation"},
        FunctionInfo{1004, D<&FSP_SRV::SetGlobalAccessLogMode>, "SetGlobalAccessLogMode"},
        FunctionInfo{1005, D<&FSP_SRV::GetGlobalAccessLogMode>, "GetGlobalAccessLogMode"},
        FunctionInfo{1006, D<&FSP_SRV::OutputAccessLogToSdCard>, "OutputAccessLogToSdCard"},
        FunctionInfo{1007, nullptr, "RegisterUpdatePartition"},
        FunctionInfo{1008, nullptr, "OpenRegisteredUpdatePartition"},
        FunctionInfo{1009, nullptr, "GetAndClearMemoryReportInfo"},
        FunctionInfo{1010, nullptr, "SetDataStorageRedirectTarget"},
        FunctionInfo{1011, D<&FSP_SRV::GetProgramIndexForAccessLog>, "GetProgramIndexForAccessLog"},
        FunctionInfo{1012, nullptr, "GetFsStackUsage"},
        FunctionInfo{1013, nullptr, "UnsetSaveDataRootPath"},
        FunctionInfo{1014, nullptr, "OutputMultiProgramTagAccessLog"},
        FunctionInfo{1016, D<&FSP_SRV::FlushAccessLogOnSdCard>, "FlushAccessLogOnSdCard"},
        FunctionInfo{1017, nullptr, "OutputApplicationInfoAccessLog"},
        FunctionInfo{1018, nullptr, "SetDebugOption"},
        FunctionInfo{1019, nullptr, "UnsetDebugOption"},
        FunctionInfo{1100, nullptr, "OverrideSaveDataTransferTokenSignVerificationKey"},
        FunctionInfo{1110, nullptr, "CorruptSaveDataFileSystemBySaveDataSpaceId2"},
        FunctionInfo{1200, D<&FSP_SRV::OpenMultiCommitManager>, "OpenMultiCommitManager"},
        FunctionInfo{1300, nullptr, "OpenBisWiper"},
    };
    // clang-format on
    RegisterHandlers(functions);

    if (Settings::values.enable_fs_access_log) {
        access_log_mode = AccessLogMode::SdCard;
    }
}

FSP_SRV::~FSP_SRV() = default;

Result FSP_SRV::SetCurrentProcess(ClientProcessId pid) {
    current_process_id = *pid;

    LOG_DEBUG(Service_FS, "called. current_process_id={:#016x}", current_process_id);

    R_RETURN(
        fsc.OpenProcess(&program_id, &save_data_controller, &romfs_controller, current_process_id));
}

Result FSP_SRV::OpenFileSystemWithPatch(OutInterface<IFileSystem> out_interface,
                                        FileSystemProxyType type, u64 open_program_id) {
    LOG_ERROR(Service_FS, "(STUBBED) called with type={}, program_id={:016X}", type,
              open_program_id);

    // FIXME: many issues with this
    ASSERT(type == FileSystemProxyType::Manual);
    const auto manual_romfs = romfs_controller->OpenPatchedRomFS(
        open_program_id, FileSys::ContentRecordType::HtmlDocument);

    ASSERT(manual_romfs != nullptr);

    const auto extracted_romfs = FileSys::ExtractRomFS(manual_romfs);
    ASSERT(extracted_romfs != nullptr);

    *out_interface = std::make_shared<IFileSystem>(
        system, extracted_romfs, SizeGetter::FromStorageId(fsc, FileSys::StorageId::NandUser));

    R_SUCCEED();
}

Result FSP_SRV::OpenSdCardFileSystem(OutInterface<IFileSystem> out_interface) {
    LOG_DEBUG(Service_FS, "called");

    FileSys::VirtualDir sdmc_dir{};
    fsc.OpenSDMC(&sdmc_dir);

    *out_interface = std::make_shared<IFileSystem>(
        system, sdmc_dir, SizeGetter::FromStorageId(fsc, FileSys::StorageId::SdCard));

    R_SUCCEED();
}

Result FSP_SRV::CreateSaveDataFileSystem(FileSys::SaveDataCreationInfo save_create_struct,
                                         FileSys::SaveDataAttribute save_struct, u128 uid) {
    LOG_DEBUG(Service_FS, "called save_struct = {}, uid = {:016X}{:016X}", save_struct.DebugInfo(),
              uid[1], uid[0]);

    FileSys::VirtualDir save_data_dir{};
    R_RETURN(save_data_controller->CreateSaveData(&save_data_dir, FileSys::SaveDataSpaceId::User,
                                                  save_struct));
}

Result FSP_SRV::CreateSaveDataFileSystemBySystemSaveDataId(
    FileSys::SaveDataAttribute save_struct, FileSys::SaveDataCreationInfo save_create_struct) {
    LOG_DEBUG(Service_FS, "called save_struct = {}", save_struct.DebugInfo());

    FileSys::VirtualDir save_data_dir{};
    R_RETURN(save_data_controller->CreateSaveData(&save_data_dir, FileSys::SaveDataSpaceId::System,
                                                  save_struct));
}

Result FSP_SRV::CreateSaveDataFileSystemWithCreationInfo2(
    FileSys::SaveDataCreationInfo2 save_data_creation_info) {
    FileSys::VirtualDir save_data_dir{};
    R_RETURN(save_data_controller->CreateSaveData(&save_data_dir, save_data_creation_info.space_id,
                                                  save_data_creation_info.attribute));
}

Result FSP_SRV::IsExFatSupported(Out<bool> out_is_supported) {
    LOG_WARNING(Service_FS, "(STUBBED) called");

    *out_is_supported = true;

    R_SUCCEED();
}

Result FSP_SRV::OpenSaveDataFileSystem(OutInterface<IFileSystem> out_interface,
                                       FileSys::SaveDataSpaceId space_id,
                                       FileSys::SaveDataAttribute attribute) {
    LOG_INFO(Service_FS, "called.");

    FileSys::VirtualDir dir{};
    R_TRY(save_data_controller->OpenSaveData(&dir, space_id, attribute));

    FileSys::StorageId id{};
    switch (space_id) {
    case FileSys::SaveDataSpaceId::User:
        id = FileSys::StorageId::NandUser;
        break;
    case FileSys::SaveDataSpaceId::SdSystem:
    case FileSys::SaveDataSpaceId::SdUser:
        id = FileSys::StorageId::SdCard;
        break;
    case FileSys::SaveDataSpaceId::System:
        id = FileSys::StorageId::NandSystem;
        break;
    case FileSys::SaveDataSpaceId::Temporary:
        // ok this is definitely wrong. ASSERT(false) here just kills the whole game the first
        // time it opens cache storage, and plenty of games do that (TOTK for one). there is
        // user-space scratch storage so it belongs on user nand. map it, do not crash.
        id = FileSys::StorageId::NandUser;
        break;
    case FileSys::SaveDataSpaceId::ProperSystem:
    case FileSys::SaveDataSpaceId::SafeMode:
        // same deal for these two. they are system-level spaces so they go on system nand.
        // way better than nuking the title over a save-space id we just did not list out.
        id = FileSys::StorageId::NandSystem;
        break;
    }

    *out_interface =
        std::make_shared<IFileSystem>(system, std::move(dir), SizeGetter::FromStorageId(fsc, id));

    R_SUCCEED();
}

Result FSP_SRV::OpenSaveDataFileSystemBySystemSaveDataId(OutInterface<IFileSystem> out_interface,
                                                         FileSys::SaveDataSpaceId space_id,
                                                         FileSys::SaveDataAttribute attribute) {
    LOG_INFO(Service_FS, "called, space_id={}, {}",
             space_id, attribute.DebugInfo());

    R_UNLESS(attribute.system_save_data_id != FileSys::InvalidSystemSaveDataId,
             FileSys::ResultInvalidArgument);

    if (attribute.program_id == 0) {
        attribute.program_id = program_id;
    }

    FileSys::VirtualDir dir{};
    R_TRY(save_data_controller->OpenSaveData(&dir, space_id, attribute));

    FileSys::StorageId id{};
    switch (space_id) {
    case FileSys::SaveDataSpaceId::User:
        id = FileSys::StorageId::NandUser;
        break;
    case FileSys::SaveDataSpaceId::SdSystem:
    case FileSys::SaveDataSpaceId::SdUser:
        id = FileSys::StorageId::SdCard;
        break;
    case FileSys::SaveDataSpaceId::System:
        id = FileSys::StorageId::NandSystem;
        break;
    case FileSys::SaveDataSpaceId::Temporary:
        // same broken switch as OpenSaveDataFileSystem above. do not ASSERT(false) and kill the
        // game over a save-space id, just map Temporary to user nand like it should be.
        id = FileSys::StorageId::NandUser;
        break;
    case FileSys::SaveDataSpaceId::ProperSystem:
    case FileSys::SaveDataSpaceId::SafeMode:
        // system spaces -> system nand. handled, not crashed.
        id = FileSys::StorageId::NandSystem;
        break;
    }

    *out_interface =
        std::make_shared<IFileSystem>(system, std::move(dir), SizeGetter::FromStorageId(fsc, id));

    R_SUCCEED();
}

Result FSP_SRV::OpenReadOnlySaveDataFileSystem(OutInterface<IFileSystem> out_interface,
                                               FileSys::SaveDataSpaceId space_id,
                                               FileSys::SaveDataAttribute attribute) {
    LOG_WARNING(Service_FS, "(STUBBED) called, delegating to 51 OpenSaveDataFilesystem");
    R_RETURN(OpenSaveDataFileSystem(out_interface, space_id, attribute));
}

Result FSP_SRV::OpenSaveDataInfoReaderBySaveDataSpaceId(
    OutInterface<ISaveDataInfoReader> out_interface, FileSys::SaveDataSpaceId space) {
    LOG_INFO(Service_FS, "called, space={}", space);

    *out_interface = std::make_shared<ISaveDataInfoReader>(system, save_data_controller, space);

    R_SUCCEED();
}

Result FSP_SRV::OpenSaveDataInfoReaderOnlyCacheStorage(
    OutInterface<ISaveDataInfoReader> out_interface) {
    LOG_WARNING(Service_FS, "(STUBBED) called");

    *out_interface = std::make_shared<ISaveDataInfoReader>(system, save_data_controller,
                                                           FileSys::SaveDataSpaceId::Temporary);

    R_SUCCEED();
}

Result FSP_SRV::FindSaveDataWithFilter(Out<s64> out_count,
                                       OutBuffer<BufferAttr_HipcMapAlias> out_buffer,
                                       FileSys::SaveDataSpaceId space_id,
                                       FileSys::SaveDataFilter filter) {
    LOG_WARNING(Service_FS, "(STUBBED) called");
    R_THROW(FileSys::ResultTargetNotFound);
}

Result FSP_SRV::WriteSaveDataFileSystemExtraData(InBuffer<BufferAttr_HipcMapAlias> buffer,
                                                 FileSys::SaveDataSpaceId space_id,
                                                 u64 save_data_id) {
    LOG_WARNING(Service_FS, "(STUBBED) called, space_id={}, save_data_id={:016X}", space_id,
                save_data_id);
    R_SUCCEED();
}

Result FSP_SRV::WriteSaveDataFileSystemExtraDataWithMaskBySaveDataAttribute(
    InBuffer<BufferAttr_HipcMapAlias> buffer, InBuffer<BufferAttr_HipcMapAlias> mask_buffer,
    FileSys::SaveDataSpaceId space_id, FileSys::SaveDataAttribute attribute) {
    LOG_WARNING(Service_FS,
                "(STUBBED) called, space_id={}, attribute.program_id={:016X}\n"
                "attribute.user_id={:016X}{:016X}, attribute.save_id={:016X}\n"
                "attribute.type={}, attribute.rank={}, attribute.index={}",
                space_id, attribute.program_id, attribute.user_id[1], attribute.user_id[0],
                attribute.system_save_data_id, attribute.type, attribute.rank, attribute.index);
    R_SUCCEED();
}

Result FSP_SRV::ReadSaveDataFileSystemExtraDataWithMaskBySaveDataAttribute(
    FileSys::SaveDataSpaceId space_id, FileSys::SaveDataAttribute attribute,
    InBuffer<BufferAttr_HipcMapAlias> mask_buffer, OutBuffer<BufferAttr_HipcMapAlias> out_buffer) {
    // Stub this to None for now, backend needs an impl to read/write the SaveDataExtraData
    // In an earlier version of the code, this was returned as an out argument, but this is not
    // correct
    [[maybe_unused]] constexpr auto flags = static_cast<u32>(FileSys::SaveDataFlags::None);

    LOG_WARNING(Service_FS,
                "(STUBBED) called, flags={}, space_id={}, attribute.program_id={:016X}\n"
                "attribute.user_id={:016X}{:016X}, attribute.save_id={:016X}\n"
                "attribute.type={}, attribute.rank={}, attribute.index={}",
                flags, space_id, attribute.program_id, attribute.user_id[1], attribute.user_id[0],
                attribute.system_save_data_id, attribute.type, attribute.rank, attribute.index);

    R_SUCCEED();
}

Result FSP_SRV::ReadSaveDataFileSystemExtraData(OutBuffer<BufferAttr_HipcMapAlias> out_buffer,
                                                u64 save_data_id) {
    // Stub, backend needs an impl to read/write the SaveDataExtraData
    LOG_WARNING(Service_FS, "(STUBBED) called, save_data_id={:016X}", save_data_id);
    std::memset(out_buffer.data(), 0, out_buffer.size());
    R_SUCCEED();
}

Result FSP_SRV::ReadSaveDataFileSystemExtraDataBySaveDataAttribute(
    OutBuffer<BufferAttr_HipcMapAlias> out_buffer, FileSys::SaveDataSpaceId space_id,
    FileSys::SaveDataAttribute attribute) {
    // Stub, backend needs an impl to read/write the SaveDataExtraData
    LOG_WARNING(Service_FS,
                "(STUBBED) called, space_id={}, attribute.program_id={:016X}\n"
                "attribute.user_id={:016X}{:016X}, attribute.save_id={:016X}\n"
                "attribute.type={}, attribute.rank={}, attribute.index={}",
                space_id, attribute.program_id, attribute.user_id[1], attribute.user_id[0],
                attribute.system_save_data_id, attribute.type, attribute.rank, attribute.index);
    std::memset(out_buffer.data(), 0, out_buffer.size());
    R_SUCCEED();
}

Result FSP_SRV::ReadSaveDataFileSystemExtraDataBySaveDataSpaceId(
    OutBuffer<BufferAttr_HipcMapAlias> out_buffer, FileSys::SaveDataSpaceId space_id,
    u64 save_data_id) {
    // Stub, backend needs an impl to read/write the SaveDataExtraData
    LOG_WARNING(Service_FS, "(STUBBED) called, space_id={}, save_data_id={:016X}", space_id,
                save_data_id);
    std::memset(out_buffer.data(), 0, out_buffer.size());
    R_SUCCEED();
}

Result FSP_SRV::OpenSaveDataTransferProhibiter(
    OutInterface<ISaveDataTransferProhibiter> out_prohibiter, u64 id) {
    LOG_WARNING(Service_FS, "(STUBBED) called, id={:016X}", id);
    *out_prohibiter = std::make_shared<ISaveDataTransferProhibiter>(system);
    R_SUCCEED();
}

Result FSP_SRV::OpenDataFileSystemByCurrentProcess(OutInterface<IFileSystem> out_interface) {
    LOG_DEBUG(Service_FS, "called");

    if (!romfs) {
        auto current_romfs = romfs_controller->OpenRomFSCurrentProcess();
        if (!current_romfs) {
            LOG_CRITICAL(Service_FS, "No file system interface available!");
            R_RETURN(ResultUnknown);
        }

        romfs = current_romfs;
    }

    auto extracted_romfs = FileSys::ExtractRomFS(romfs);
    if (!extracted_romfs) {
        LOG_CRITICAL(Service_FS, "Failed to extract RomFS for the current process!");
        R_RETURN(ResultUnknown);
    }

    *out_interface = std::make_shared<IFileSystem>(
        system, extracted_romfs, SizeGetter::FromStorageId(fsc, FileSys::StorageId::NandUser));

    R_SUCCEED();
}

Result FSP_SRV::OpenDataStorageByCurrentProcess(OutInterface<IStorage> out_interface) {
    LOG_DEBUG(Service_FS, "called");

    if (!romfs) {
        auto current_romfs = romfs_controller->OpenRomFSCurrentProcess();
        if (!current_romfs) {
            // TODO (bunnei): Find the right error code to use here
            LOG_CRITICAL(Service_FS, "No file system interface available!");
            R_RETURN(ResultUnknown);
        }

        romfs = current_romfs;
    }

    *out_interface = std::make_shared<IStorage>(system, romfs);

    R_SUCCEED();
}

Result FSP_SRV::OpenDataStorageByDataId(OutInterface<IStorage> out_interface,
                                        FileSys::StorageId storage_id, u32 unknown, u64 title_id) {
    LOG_DEBUG(Service_FS, "called with storage_id={:02X}, unknown={:08X}, title_id={:016X}",
              storage_id, unknown, title_id);

    auto data = romfs_controller->OpenRomFS(title_id, storage_id, FileSys::ContentRecordType::Data);

    if (!data) {
        const auto archive = FileSys::SystemArchive::SynthesizeSystemArchive(title_id);

        if (archive != nullptr) {
            *out_interface = std::make_shared<IStorage>(system, archive);
            R_SUCCEED();
        }

        // TODO(DarkLordZach): Find the right error code to use here
        LOG_ERROR(Service_FS,
                  "Could not open data storage with title_id={:016X}, storage_id={:02X}", title_id,
                  storage_id);
        R_RETURN(ResultUnknown);
    }

    const FileSys::PatchManager pm{title_id, fsc, content_provider};

    auto base =
        romfs_controller->OpenBaseNca(title_id, storage_id, FileSys::ContentRecordType::Data);
    auto storage = std::make_shared<IStorage>(
        system, pm.PatchRomFS(base.get(), std::move(data), FileSys::ContentRecordType::Data));

    *out_interface = std::move(storage);
    R_SUCCEED();
}

Result FSP_SRV::OpenPatchDataStorageByCurrentProcess(OutInterface<IStorage> out_interface,
                                                     FileSys::StorageId storage_id, u64 title_id) {
    LOG_WARNING(Service_FS, "(STUBBED) called with storage_id={:02X}, title_id={:016X}", storage_id,
                title_id);

    R_RETURN(FileSys::ResultTargetNotFound);
}

Result FSP_SRV::OpenDataStorageWithProgramIndex(OutInterface<IStorage> out_interface,
                                                u8 program_index) {
    LOG_DEBUG(Service_FS, "called, program_index={}", program_index);

    auto patched_romfs = romfs_controller->OpenPatchedRomFSWithProgramIndex(
        program_id, program_index, FileSys::ContentRecordType::Program);

    if (!patched_romfs) {
        // TODO: Find the right error code to use here
        LOG_ERROR(Service_FS, "Could not open storage with program_index={}", program_index);
        R_RETURN(ResultUnknown);
    }

    *out_interface = std::make_shared<IStorage>(system, std::move(patched_romfs));

    R_SUCCEED();
}

Result FSP_SRV::IsSdCardAccessible(Out<bool> out_is_accessible) {
    LOG_DEBUG(Service_FS, "(STUBBED) called");

    *out_is_accessible = true;

    R_SUCCEED();
}

Result FSP_SRV::DisableAutoSaveDataCreation() {
    LOG_DEBUG(Service_FS, "called");

    save_data_controller->SetAutoCreate(false);

    R_SUCCEED();
}

Result FSP_SRV::SetGlobalAccessLogMode(AccessLogMode access_log_mode_) {
    LOG_DEBUG(Service_FS, "called, access_log_mode={}", access_log_mode_);

    access_log_mode = access_log_mode_;

    R_SUCCEED();
}

Result FSP_SRV::GetGlobalAccessLogMode(Out<AccessLogMode> out_access_log_mode) {
    LOG_DEBUG(Service_FS, "called");

    *out_access_log_mode = access_log_mode;

    R_SUCCEED();
}

Result FSP_SRV::OutputAccessLogToSdCard(InBuffer<BufferAttr_HipcMapAlias> log_message_buffer) {
    LOG_DEBUG(Service_FS, "called");

    auto log = Common::StringFromFixedZeroTerminatedBuffer(
        reinterpret_cast<const char*>(log_message_buffer.data()), log_message_buffer.size());
    reporter.SaveFSAccessLog(log);

    R_SUCCEED();
}

Result FSP_SRV::GetProgramIndexForAccessLog(Out<AccessLogVersion> out_access_log_version,
                                            Out<u32> out_access_log_program_index) {
    LOG_DEBUG(Service_FS, "(STUBBED) called");

    *out_access_log_version = AccessLogVersion::Latest;
    *out_access_log_program_index = access_log_program_index;

    R_SUCCEED();
}

Result FSP_SRV::FlushAccessLogOnSdCard() {
    LOG_DEBUG(Service_FS, "(STUBBED) called");

    R_SUCCEED();
}

Result FSP_SRV::ExtendSaveDataFileSystem(FileSys::SaveDataSpaceId space_id, u64 save_data_id,
                                         s64 available_size, s64 journal_size) {
    // We don't have an index of save data ids, so we can't implement this.
    LOG_WARNING(Service_FS,
                "(STUBBED) called, space_id={}, save_data_id={:016X}, available_size={:#x}, "
                "journal_size={:#x}",
                space_id, save_data_id, available_size, journal_size);
    R_SUCCEED();
}

Result FSP_SRV::GetCacheStorageSize(s32 index, Out<s64> out_data_size, Out<s64> out_journal_size) {
    LOG_WARNING(Service_FS, "(STUBBED) called with index={}", index);

    *out_data_size = 0;
    *out_journal_size = 0;

    R_SUCCEED();
}

Result FSP_SRV::OpenMultiCommitManager(OutInterface<IMultiCommitManager> out_interface) {
    LOG_DEBUG(Service_FS, "called");

    *out_interface = std::make_shared<IMultiCommitManager>(system);

    R_SUCCEED();
}

} // namespace Service::FileSystem
