// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/common_funcs.h"
#include "core/file_sys/fs_filesystem.h"
#include "core/file_sys/fsa/fs_i_filesystem.h"
#include "core/file_sys/vfs/vfs.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/hle/service/filesystem/fsp/fsp_types.h"
#include "core/hle/service/service.h"

namespace FileSys::Sf {
struct Path;
}

namespace Service::FileSystem {

class IFile;
class IDirectory;

class IFileSystem final : public ServiceFramework<IFileSystem> {
public:
    explicit IFileSystem(Core::System& system_, FileSys::VirtualDir dir_, SizeGetter size_getter_);

    Result CreateFile(const InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path, s32 option,
                      s64 size);
    Result DeleteFile(const InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path);
    Result CreateDirectory(const InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path);
    Result DeleteDirectory(const InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path);
    Result DeleteDirectoryRecursively(
        const InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path);
    Result CleanDirectoryRecursively(
        const InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path);
    Result RenameFile(const InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> old_path,
                      const InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> new_path);
    Result RenameDirectory(const InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> old_path,
                           const InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> new_path);
    Result OpenFile(OutInterface<IFile> out_interface,
                    const InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path, u32 mode);
    Result OpenDirectory(OutInterface<IDirectory> out_interface,
                         const InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path,
                         u32 mode);
    Result GetEntryType(Out<u32> out_type,
                        const InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path);
    Result Commit();
    Result GetFreeSpaceSize(Out<s64> out_size,
                            const InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path);
    Result GetTotalSpaceSize(Out<s64> out_size,
                             const InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path);
    Result GetFileTimeStampRaw(Out<FileSys::FileTimeStampRaw> out_timestamp,
                               const InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path);
    Result GetFileSystemAttribute(Out<FileSys::FileSystemAttribute> out_attribute);

private:
    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, D<&IFileSystem::CreateFile>, "CreateFile"},
        FunctionInfo{1, D<&IFileSystem::DeleteFile>, "DeleteFile"},
        FunctionInfo{2, D<&IFileSystem::CreateDirectory>, "CreateDirectory"},
        FunctionInfo{3, D<&IFileSystem::DeleteDirectory>, "DeleteDirectory"},
        FunctionInfo{4, D<&IFileSystem::DeleteDirectoryRecursively>, "DeleteDirectoryRecursively"},
        FunctionInfo{5, D<&IFileSystem::RenameFile>, "RenameFile"},
        FunctionInfo{6, D<&IFileSystem::RenameDirectory>, "RenameDirectory"},
        FunctionInfo{7, D<&IFileSystem::GetEntryType>, "GetEntryType"},
        FunctionInfo{8, D<&IFileSystem::OpenFile>, "OpenFile"},
        FunctionInfo{9, D<&IFileSystem::OpenDirectory>, "OpenDirectory"},
        FunctionInfo{10, D<&IFileSystem::Commit>, "Commit"},
        FunctionInfo{11, D<&IFileSystem::GetFreeSpaceSize>, "GetFreeSpaceSize"},
        FunctionInfo{12, D<&IFileSystem::GetTotalSpaceSize>, "GetTotalSpaceSize"},
        FunctionInfo{13, D<&IFileSystem::CleanDirectoryRecursively>, "CleanDirectoryRecursively"},
        FunctionInfo{14, D<&IFileSystem::GetFileTimeStampRaw>, "GetFileTimeStampRaw"},
        FunctionInfo{15, nullptr, "QueryEntry"},
        FunctionInfo{16, D<&IFileSystem::GetFileSystemAttribute>, "GetFileSystemAttribute"}
    );
    std::unique_ptr<FileSys::Fsa::IFileSystem> backend;
    SizeGetter size_getter;
};

} // namespace Service::FileSystem
