// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::NS {

class IDynamicRightsInterface;
class IReadOnlyApplicationControlDataInterface;
class IReadOnlyApplicationRecordInterface;
class IECommerceInterface;
class IApplicationVersionInterface;
class IFactoryResetInterface;
class IAccountProxyInterface;
class IApplicationManagerInterface;
class IDownloadTaskInterface;
class IContentManagementInterface;
class IDocumentInterface;

class IServiceGetterInterface : public ServiceFramework<IServiceGetterInterface> {
public:
    explicit IServiceGetterInterface(Core::System& system_, const char* name);
    ~IServiceGetterInterface() override;

public:
    Result GetDynamicRightsInterface(Out<SharedPointer<IDynamicRightsInterface>> out_interface);
    Result GetReadOnlyApplicationControlDataInterface(
        Out<SharedPointer<IReadOnlyApplicationControlDataInterface>> out_interface);
    Result GetReadOnlyApplicationRecordInterface(
        Out<SharedPointer<IReadOnlyApplicationRecordInterface>> out_interface);
    Result GetECommerceInterface(Out<SharedPointer<IECommerceInterface>> out_interface);
    Result GetApplicationVersionInterface(
        Out<SharedPointer<IApplicationVersionInterface>> out_interface);
    Result GetFactoryResetInterface(Out<SharedPointer<IFactoryResetInterface>> out_interface);
    Result GetAccountProxyInterface(Out<SharedPointer<IAccountProxyInterface>> out_interface);
    Result GetApplicationManagerInterface(
        Out<SharedPointer<IApplicationManagerInterface>> out_interface);
    Result GetDownloadTaskInterface(Out<SharedPointer<IDownloadTaskInterface>> out_interface);
    Result GetContentManagementInterface(
        Out<SharedPointer<IContentManagementInterface>> out_interface);
    Result GetDocumentInterface(Out<SharedPointer<IDocumentInterface>> out_interface);

    FunctionInfoBase const* FindRequest(u32 key) override {
        return HandlerTableGenerateWithFind(key, functions);
    }
    static constexpr auto functions = CreateStaticMap(
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
};

} // namespace Service::NS
