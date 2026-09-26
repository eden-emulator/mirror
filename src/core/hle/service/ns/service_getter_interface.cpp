// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ns/account_proxy_interface.h"
#include "core/hle/service/ns/application_manager_interface.h"
#include "core/hle/service/ns/application_version_interface.h"
#include "core/hle/service/ns/content_management_interface.h"
#include "core/hle/service/ns/document_interface.h"
#include "core/hle/service/ns/download_task_interface.h"
#include "core/hle/service/ns/dynamic_rights_interface.h"
#include "core/hle/service/ns/ecommerce_interface.h"
#include "core/hle/service/ns/factory_reset_interface.h"
#include "core/hle/service/ns/read_only_application_control_data_interface.h"
#include "core/hle/service/ns/read_only_application_record_interface.h"
#include "core/hle/service/ns/service_getter_interface.h"

namespace Service::NS {

ServiceFrameworkBase::FunctionInfoBase const* IServiceGetterInterface::FindRequest(u32 key) {
    static const auto functions = CreateStaticMap(
        FunctionInfo{7988, D<&IServiceGetterInterface::GetDynamicRightsInterface>, "GetDynamicRightsInterface"},
        FunctionInfo{7989, D<&IServiceGetterInterface::GetReadOnlyApplicationControlDataInterface>, "GetReadOnlyApplicationControlDataInterface"},
        FunctionInfo{7991, D<&IServiceGetterInterface::GetReadOnlyApplicationRecordInterface>, "GetReadOnlyApplicationRecordInterface"},
        FunctionInfo{7992, D<&IServiceGetterInterface::GetECommerceInterface>, "GetECommerceInterface"},
        FunctionInfo{7993, D<&IServiceGetterInterface::GetApplicationVersionInterface>, "GetApplicationVersionInterface"},
        FunctionInfo{7994, D<&IServiceGetterInterface::GetFactoryResetInterface>, "GetFactoryResetInterface"},
        FunctionInfo{7995, D<&IServiceGetterInterface::GetAccountProxyInterface>, "GetAccountProxyInterface"},
        FunctionInfo{7996, D<&IServiceGetterInterface::GetApplicationManagerInterface>, "GetApplicationManagerInterface"},
        FunctionInfo{7997, D<&IServiceGetterInterface::GetDownloadTaskInterface>, "GetDownloadTaskInterface"},
        FunctionInfo{7998, D<&IServiceGetterInterface::GetContentManagementInterface>, "GetContentManagementInterface"},
        FunctionInfo{7999, D<&IServiceGetterInterface::GetDocumentInterface>, "GetDocumentInterface"}
    );
    return HandlerTableGenerateWithFind(key, functions);
}

IServiceGetterInterface::IServiceGetterInterface(Core::System& system_, const char* name)
    : ServiceFramework{system_, name} {
}

IServiceGetterInterface::~IServiceGetterInterface() = default;

Result IServiceGetterInterface::GetDynamicRightsInterface(
    Out<SharedPointer<IDynamicRightsInterface>> out_interface) {
    LOG_DEBUG(Service_NS, "called");
    *out_interface = std::make_shared<IDynamicRightsInterface>(system);
    R_SUCCEED();
}

Result IServiceGetterInterface::GetReadOnlyApplicationControlDataInterface(
    Out<SharedPointer<IReadOnlyApplicationControlDataInterface>> out_interface) {
    LOG_DEBUG(Service_NS, "called");
    *out_interface = std::make_shared<IReadOnlyApplicationControlDataInterface>(system);
    R_SUCCEED();
}

Result IServiceGetterInterface::GetReadOnlyApplicationRecordInterface(
    Out<SharedPointer<IReadOnlyApplicationRecordInterface>> out_interface) {
    LOG_DEBUG(Service_NS, "called");
    *out_interface = std::make_shared<IReadOnlyApplicationRecordInterface>(system);
    R_SUCCEED();
}

Result IServiceGetterInterface::GetECommerceInterface(
    Out<SharedPointer<IECommerceInterface>> out_interface) {
    LOG_DEBUG(Service_NS, "called");
    *out_interface = std::make_shared<IECommerceInterface>(system);
    R_SUCCEED();
}

Result IServiceGetterInterface::GetApplicationVersionInterface(
    Out<SharedPointer<IApplicationVersionInterface>> out_interface) {
    LOG_DEBUG(Service_NS, "called");
    *out_interface = std::make_shared<IApplicationVersionInterface>(system);
    R_SUCCEED();
}

Result IServiceGetterInterface::GetFactoryResetInterface(
    Out<SharedPointer<IFactoryResetInterface>> out_interface) {
    LOG_DEBUG(Service_NS, "called");
    *out_interface = std::make_shared<IFactoryResetInterface>(system);
    R_SUCCEED();
}

Result IServiceGetterInterface::GetAccountProxyInterface(
    Out<SharedPointer<IAccountProxyInterface>> out_interface) {
    LOG_DEBUG(Service_NS, "called");
    *out_interface = std::make_shared<IAccountProxyInterface>(system);
    R_SUCCEED();
}

Result IServiceGetterInterface::GetApplicationManagerInterface(
    Out<SharedPointer<IApplicationManagerInterface>> out_interface) {
    LOG_DEBUG(Service_NS, "called");
    *out_interface = std::make_shared<IApplicationManagerInterface>(system);
    R_SUCCEED();
}

Result IServiceGetterInterface::GetDownloadTaskInterface(
    Out<SharedPointer<IDownloadTaskInterface>> out_interface) {
    LOG_DEBUG(Service_NS, "called");
    *out_interface = std::make_shared<IDownloadTaskInterface>(system);
    R_SUCCEED();
}

Result IServiceGetterInterface::GetContentManagementInterface(
    Out<SharedPointer<IContentManagementInterface>> out_interface) {
    LOG_DEBUG(Service_NS, "called");
    *out_interface = std::make_shared<IContentManagementInterface>(system);
    R_SUCCEED();
}

Result IServiceGetterInterface::GetDocumentInterface(
    Out<SharedPointer<IDocumentInterface>> out_interface) {
    LOG_DEBUG(Service_NS, "called");
    *out_interface = std::make_shared<IDocumentInterface>(system);
    R_SUCCEED();
}

} // namespace Service::NS
