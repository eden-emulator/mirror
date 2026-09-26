// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/file_sys/vfs/vfs.h"
#include "core/hle/service/bcat/bcat_types.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::BCAT {

class IDeliveryCacheFileService final : public ServiceFramework<IDeliveryCacheFileService> {
public:
    explicit IDeliveryCacheFileService(Core::System& system_, FileSys::VirtualDir root_);
    ~IDeliveryCacheFileService() override;

private:
    Result Open(const DirectoryName& dir_name_raw, const FileName& file_name_raw);
    Result Read(Out<u64> out_buffer_size, u64 offset,
                OutBuffer<BufferAttr_HipcMapAlias> out_buffer);
    Result GetSize(Out<u64> out_size);
    Result GetDigest(Out<BcatDigest> out_digest);

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, D<&IDeliveryCacheFileService::Open>, "Open"},
        FunctionInfo{1, D<&IDeliveryCacheFileService::Read>, "Read"},
        FunctionInfo{2, D<&IDeliveryCacheFileService::GetSize>, "GetSize"},
        FunctionInfo{3, D<&IDeliveryCacheFileService::GetDigest>, "GetDigest"}
    );
    FileSys::VirtualDir root;
    FileSys::VirtualFile current_file;
};

} // namespace Service::BCAT
