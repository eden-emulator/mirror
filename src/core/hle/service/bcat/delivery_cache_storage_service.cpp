// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/hle/service/bcat/bcat_result.h"
#include "core/hle/service/bcat/delivery_cache_directory_service.h"
#include "core/hle/service/bcat/delivery_cache_file_service.h"
#include "core/hle/service/bcat/delivery_cache_storage_service.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::BCAT {

ServiceFrameworkBase::FunctionInfoBase const* IDeliveryCacheStorageService::FindRequest(u32 key) {
    static const auto functions = CreateStaticMap(
        FunctionInfo{0, D<&IDeliveryCacheStorageService::CreateFileService>, "CreateFileService"},
        FunctionInfo{1, D<&IDeliveryCacheStorageService::CreateDirectoryService>, "CreateDirectoryService"},
        FunctionInfo{10, D<&IDeliveryCacheStorageService::EnumerateDeliveryCacheDirectory>, "EnumerateDeliveryCacheDirectory"}
    );
    return HandlerTableGenerateWithFind(key, functions);
}

IDeliveryCacheStorageService::IDeliveryCacheStorageService(Core::System& system_,
                                                           FileSys::VirtualDir root_)
    : ServiceFramework{system_, "IDeliveryCacheStorageService"}, root(std::move(root_)) {
}

IDeliveryCacheStorageService::~IDeliveryCacheStorageService() = default;

Result IDeliveryCacheStorageService::CreateFileService(
    OutInterface<IDeliveryCacheFileService> out_interface) {
    LOG_DEBUG(Service_BCAT, "called");

    *out_interface = std::make_shared<IDeliveryCacheFileService>(system, root);
    R_SUCCEED();
}

Result IDeliveryCacheStorageService::CreateDirectoryService(
    OutInterface<IDeliveryCacheDirectoryService> out_interface) {
    LOG_DEBUG(Service_BCAT, "called");

    *out_interface = std::make_shared<IDeliveryCacheDirectoryService>(system, root);
    R_SUCCEED();
}

Result IDeliveryCacheStorageService::EnumerateDeliveryCacheDirectory(
    Out<s32> out_directory_count,
    OutArray<DirectoryName, BufferAttr_HipcMapAlias> out_directories) {
    LOG_DEBUG(Service_BCAT, "called, size={:016X}", out_directories.size());

    *out_directory_count =
        static_cast<s32>((std::min)(out_directories.size(), entries.size() - next_read_index));
    memcpy(out_directories.data(), entries.data() + next_read_index,
           *out_directory_count * sizeof(DirectoryName));
    next_read_index += *out_directory_count;
    R_SUCCEED();
}

} // namespace Service::BCAT
