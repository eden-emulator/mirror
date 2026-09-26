// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::FileSystem {
class FileSystemController;
}

namespace Service::BCAT {
class BcatBackend;
class IBcatService;
class IDeliveryCacheStorageService;

class IServiceCreator final : public ServiceFramework<IServiceCreator> {
public:
    explicit IServiceCreator(Core::System& system_, const char* name_);
    ~IServiceCreator() override;

private:
    Result CreateBcatService(ClientProcessId process_id, OutInterface<IBcatService> out_interface);

    Result CreateDeliveryCacheStorageService(
        ClientProcessId process_id, OutInterface<IDeliveryCacheStorageService> out_interface);

    Result CreateDeliveryCacheStorageServiceWithApplicationId(
        u64 application_id, OutInterface<IDeliveryCacheStorageService> out_interface);

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, D<&IServiceCreator::CreateBcatService>, "CreateBcatService"},
        FunctionInfo{1, D<&IServiceCreator::CreateDeliveryCacheStorageService>, "CreateDeliveryCacheStorageService"},
        FunctionInfo{2, D<&IServiceCreator::CreateDeliveryCacheStorageServiceWithApplicationId>, "CreateDeliveryCacheStorageServiceWithApplicationId"},
        FunctionInfo{3, nullptr, "CreateDeliveryCacheProgressService"},
        FunctionInfo{4, nullptr, "CreateDeliveryCacheProgressServiceWithApplicationId"}
    );
    std::unique_ptr<BcatBackend> backend;
    Service::FileSystem::FileSystemController& fsc;
};

} // namespace Service::BCAT
