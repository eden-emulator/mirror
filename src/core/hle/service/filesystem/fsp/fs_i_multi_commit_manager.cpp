// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/filesystem/fsp/fs_i_filesystem.h"
#include "core/hle/service/filesystem/fsp/fs_i_multi_commit_manager.h"

namespace Service::FileSystem {

IMultiCommitManager::IMultiCommitManager(Core::System& system_)
    : ServiceFramework{system_, "IMultiCommitManager"} {
}

IMultiCommitManager::~IMultiCommitManager() = default;

Result IMultiCommitManager::Add(std::shared_ptr<IFileSystem> filesystem) {
    LOG_WARNING(Service_FS, "(STUBBED) called");

    R_SUCCEED();
}

Result IMultiCommitManager::Commit() {
    LOG_WARNING(Service_FS, "(STUBBED) called");

    R_SUCCEED();
}

} // namespace Service::FileSystem
