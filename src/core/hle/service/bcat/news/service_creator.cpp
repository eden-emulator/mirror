// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/hle/service/bcat/news/newly_arrived_event_holder.h"
#include "core/hle/service/bcat/news/news_data_service.h"
#include "core/hle/service/bcat/news/news_database_service.h"
#include "core/hle/service/bcat/news/news_service.h"
#include "core/hle/service/bcat/news/overwrite_event_holder.h"
#include "core/hle/service/bcat/news/service_creator.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::News {

ServiceFrameworkBase::FunctionInfoBase const* IServiceCreator::FindRequest(u32 key) {
    static constexpr auto functions = CreateStaticMap(
        FunctionInfo{0, D<&IServiceCreator::CreateNewsService>, "CreateNewsService"},
        FunctionInfo{1, D<&IServiceCreator::CreateNewlyArrivedEventHolder>, "CreateNewlyArrivedEventHolder"},
        FunctionInfo{2, D<&IServiceCreator::CreateNewsDataService>, "CreateNewsDataService"},
        FunctionInfo{3, D<&IServiceCreator::CreateNewsDatabaseService>, "CreateNewsDatabaseService"},
        FunctionInfo{4, D<&IServiceCreator::CreateOverwriteEventHolder>, "CreateOverwriteEventHolder"}
    );
    return HandlerTableGenerateWithFind(key, functions);
}

IServiceCreator::IServiceCreator(Core::System& system_, u32 permissions_, const char* name_)
    : ServiceFramework{system_, name_}, permissions{permissions_} {
}

IServiceCreator::~IServiceCreator() = default;

Result IServiceCreator::CreateNewsService(OutInterface<INewsService> out_interface) {
    LOG_INFO(Service_BCAT, "called");
    *out_interface = std::make_shared<INewsService>(system);
    R_SUCCEED();
}

Result IServiceCreator::CreateNewlyArrivedEventHolder(
    OutInterface<INewlyArrivedEventHolder> out_interface) {
    LOG_INFO(Service_BCAT, "called");
    *out_interface = std::make_shared<INewlyArrivedEventHolder>(system);
    R_SUCCEED();
}

Result IServiceCreator::CreateNewsDataService(OutInterface<INewsDataService> out_interface) {
    LOG_INFO(Service_BCAT, "called");
    *out_interface = std::make_shared<INewsDataService>(system);
    R_SUCCEED();
}

Result IServiceCreator::CreateNewsDatabaseService(
    OutInterface<INewsDatabaseService> out_interface) {
    LOG_INFO(Service_BCAT, "called");
    *out_interface = std::make_shared<INewsDatabaseService>(system);
    R_SUCCEED();
}

Result IServiceCreator::CreateOverwriteEventHolder(
    OutInterface<IOverwriteEventHolder> out_interface) {
    LOG_INFO(Service_BCAT, "called");
    *out_interface = std::make_shared<IOverwriteEventHolder>(system);
    R_SUCCEED();
}

} // namespace Service::News
